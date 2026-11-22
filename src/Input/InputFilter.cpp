#include "Input/InputFilter.h"

#include <array>
#include <optional>
#include <ranges>
#include <span>

#include "Config/ConfigAdapter.h"
#include "Input/ExclusiveTracker.h"
#include "Input/HotkeyMatcher.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "Shared/InputIntents.h"

namespace Input::detail {

    namespace {

        int GamepadIdToIndex(int idCode) {
            using Key = RE::BSWin32GamepadDevice::Key;
            switch (static_cast<Key>(idCode)) {
                case Key::kUp:
                    return 0;
                case Key::kDown:
                    return 1;
                case Key::kLeft:
                    return 2;
                case Key::kRight:
                    return 3;
                case Key::kStart:
                    return 4;
                case Key::kBack:
                    return 5;
                case Key::kLeftThumb:
                    return 6;
                case Key::kRightThumb:
                    return 7;
                case Key::kLeftShoulder:
                    return 8;
                case Key::kRightShoulder:
                    return 9;
                case Key::kA:
                    return 10;
                case Key::kB:
                    return 11;
                case Key::kX:
                    return 12;
                case Key::kY:
                    return 13;
                case Key::kLeftTrigger:
                    return 14;
                case Key::kRightTrigger:
                    return 15;
                default:
                    return -1;
            }
        }

        void UpdateDownState(RE::INPUT_DEVICE dev, int convertedCode, bool downNow, KeyStateStore& keys) {
            if (dev == RE::INPUT_DEVICE::kKeyboard)
                keys.kbDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
            else if (dev == RE::INPUT_DEVICE::kGamepad)
                keys.gpDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
        }

        // The wheel sends a press with no release, so it is never a held input nor a hotkey; ProcessButtonEvents
        // skips it before capture and before any held-input write (spec 003, research R2). Logged once per session.
        void NoteWheelIgnored([[maybe_unused]] const RE::ButtonEvent* btn, [[maybe_unused]] int id) {
            static bool s_logged = false;
            if (s_logged) return;
            s_logged = true;
            MAGIC_DEBUG_LOG(
                "[Input] ProcessButtonEvents: mouse wheel ignored for hotkeys (id={} value={:.2f} heldSecs={:.3f})", id,
                btn->Value(), btn->HeldDuration());
        }

        // Mouse ids 5..7 have no Windows key state, so the stuck-key recovery uses the last pressed event (down or
        // held) as a heartbeat. A held event never re-marks the button as down (spec 003, research R6).
        void RecordMouseHeartbeat(const RE::ButtonEvent* btn, KeyStateStore& keys) {
            if (btn->GetDevice() != RE::INPUT_DEVICE::kMouse) return;
            const int id = static_cast<int>(btn->GetIDCode());
            if (IsMouseNoVkId(id) && btn->IsPressed())
                keys.mouseNoVkLastPressedMs[static_cast<std::size_t>(id - kMouseFirstNoVkId)] = SteadyNowMs();

            // Premise check for the heartbeat: held events must reach this hook (research R6, T018 step 1).
            static bool s_heldLogged = false;
            if (!s_heldLogged && btn->IsHeld()) {
                s_heldLogged = true;
                MAGIC_DEBUG_LOG("[Input] ProcessButtonEvents: first held mouse event seen (id={} heldSecs={:.3f})", id,
                                btn->HeldDuration());
            }
        }

        bool TryHandleCapture(const RE::ButtonEvent* btn, CaptureState& cap, bool& wantCapture, RE::INPUT_DEVICE dev,
                              int convertedCode) {
            if (!wantCapture || !btn->IsDown()) return false;
            if (dev == RE::INPUT_DEVICE::kKeyboard && convertedCode == kDIK_Escape) return false;

            int encoded = -1;
            if (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kMouse)
                encoded = convertedCode;
            else if (dev == RE::INPUT_DEVICE::kGamepad)
                encoded = -(convertedCode + 2);
            if (encoded == -1) return false;

            MAGIC_DEBUG_LOG("[Input] TryHandleCapture: captured dev={} code={} encoded={}", static_cast<int>(dev),
                            convertedCode, encoded);
            cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
            cap.captureRequested.store(false, std::memory_order_relaxed);
            wantCapture = false;
            return true;
        }

        bool HasTransformArchetype(const RE::MagicItem* item) {
            if (!item) return false;
            using ArchetypeID = RE::EffectArchetypes::ArchetypeID;
            return std::ranges::any_of(item->effects, [](const auto* effect) {
                if (!effect || !effect->baseEffect) return false;
                const auto arch = effect->baseEffect->GetArchetype();
                return arch == ArchetypeID::kWerewolf || arch == ArchetypeID::kVampireLord;
            });
        }

        bool IsTransformPowerEquipped(RE::PlayerCharacter* pc) {
            if (!pc) return false;
            const auto& rd = pc->GetActorRuntimeData();
            if (!rd.selectedPower) return false;
            return HasTransformArchetype(rd.selectedPower->As<RE::MagicItem>());
        }

        std::optional<int> ConsumeBitLocal(std::atomic<std::uint64_t>& mask, const SlotEdgeStore& slots) {
            while (true) {
                const int n = slots.ActiveSlots();
                const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);
                std::uint64_t curAll = mask.load(std::memory_order_relaxed);
                std::uint64_t cur = curAll & allowed;
                if (cur == 0uLL) {
                    if (curAll != 0uLL)
                        (void)mask.compare_exchange_weak(curAll, curAll & allowed, std::memory_order_relaxed);
                    return std::nullopt;
                }
                int idx = -1;
                for (int i = 0; i < n; ++i)
                    if (cur & (1uLL << i)) {
                        idx = i;
                        break;
                    }
                if (idx < 0) return std::nullopt;
                if (mask.compare_exchange_weak(curAll, curAll & ~(1uLL << idx), std::memory_order_relaxed)) return idx;
            }
        }

        void DrainWhenBlocked(const SlotEdgeStore& slots, std::atomic<std::uint64_t>& pressedMask,
                              std::atomic<std::uint64_t>& releasedMask) {
            int drained = 0;
            while (ConsumeBitLocal(pressedMask, slots).has_value()) ++drained;
            if (drained > 0) MAGIC_DEBUG_LOG("[Input] DrainWhenBlocked: discarded {} pressed slot(s)", drained);
            while (ConsumeBitLocal(releasedMask, slots).has_value());
        }

        // Held repeats and releases follow the fate of their own press (spec 004, research R11, FR-010).
        // Returns true to keep the event from the game.
        bool FollowPressFate(const std::span<const int> candidates, bool isGp, int code, bool isRelease,
                             const RetainedEvent& ev, int n, ExclusiveStore& excl, RetainedArr& retained) {
            if (isRelease) {
                for (const int c : candidates) excl.deactivatedThisPress[static_cast<std::size_t>(c)] = false;
            }
            PressFate& fate = FateOf(excl, isGp, code);
            switch (fate) {
                case PressFate::Consumed:
                    if (isRelease) fate = PressFate::None;
                    return true;
                case PressFate::HeldBack:
                    if (const int owner = OwnerOf(excl, isGp, code); owner >= 0 && owner < n) {
                        // Only the release is kept with the held-back press. Held repeats are dropped while it is held
                        // back: replay drains one event per frame, so stale repeats could reach the game after the
                        // live release and leave the button stuck. Live repeats pass once the press is handed over.
                        if (isRelease) retained[static_cast<std::size_t>(owner)].push_back(ev);
                        return true;
                    }
                    fate = PressFate::None;
                    return false;
                default:
                    if (isRelease) fate = PressFate::None;
                    return false;
            }
        }

        // Owner of a held-back press: the candidate with the most of its combo held, then the longest combo, then
        // the lowest slot (spec 004, research R2 phase 4).
        int PickHoldBackOwner(const std::span<const int> candidates, bool isGp, const HotkeyCacheStore& cache,
                              const KeyStateStore& keys) {
            int owner = candidates.front();
            int bestHeld = -1;
            int bestLen = -1;
            const auto& down = isGp ? keys.gpDown : keys.kbDown;
            for (const int c : candidates) {
                const auto& hk = cache.slots[static_cast<std::size_t>(c)];
                const auto& combo = isGp ? hk.gp : hk.kb;
                const int held = ComboHeldCount(combo, down);
                const int len = ComboKeyCount(combo);
                if (held > bestHeld || (held == bestHeld && len > bestLen)) {
                    owner = c;
                    bestHeld = held;
                    bestLen = len;
                }
            }
            return owner;
        }

        // Decides whether a button event is kept from the game (spec 004, research R2 and R11). Fresh presses are
        // evaluated against every candidate slot (every slot whose combo contains the button), so the slot number
        // never decides; held repeats and releases follow their press.
        bool ShouldFilterAndSave(RE::INPUT_DEVICE dev, int convertedCode, std::uint32_t rawIdCode,
                                 const RE::BSFixedString& userEvent, float value, float heldSecs,
                                 const KeyStateStore& keys, const SlotEdgeStore& slots, const HotkeyCacheStore& cache,
                                 ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained) {
            const bool isGp = dev == RE::INPUT_DEVICE::kGamepad;
            const int code = (dev == RE::INPUT_DEVICE::kMouse) ? (kMouseButtonBase + convertedCode) : convertedCode;
            if (code < 0 || code >= kMaxCode) return false;

            std::array<int, kInputMaxSlots> candArr{};
            std::size_t count = 0;
            const int n = slots.ActiveSlots();
            for (int slot = 0; slot < n; ++slot) {
                const auto& hk = cache.slots[static_cast<std::size_t>(slot)];
                if (ComboContains(isGp ? hk.gp : hk.kb, code)) candArr[count++] = slot;
            }
            if (count == 0) return false;
            const std::span<const int> candidates(candArr.data(), count);

            const bool isRelease = value <= 0.f;
            const bool isFresh = !isRelease && heldSecs <= 0.f;
            const RetainedEvent ev{dev, rawIdCode, userEvent, value, heldSecs};

            // Phase 0: held repeat or release.
            if (!isFresh) return FollowPressFate(candidates, isGp, code, isRelease, ev, n, excl, retained);

            PressFate& fate = FateOf(excl, isGp, code);

            // Phase 1: a candidate is active or has its combo fully held -> consumed. slotWasAccepted is deliberately
            // not used here: it stays set for a whole session and would block e.g. a manual RT cast (finding H3).
            const auto& down = isGp ? keys.gpDown : keys.kbDown;
            for (const int c : candidates) {
                const auto s = static_cast<std::size_t>(c);
                const auto& hk = cache.slots[s];
                if (slots.slotDown[s].load(std::memory_order_relaxed) || ComboDown(isGp ? hk.gp : hk.kb, down)) {
                    fate = PressFate::Consumed;
                    return true;
                }
            }

            // Phase 2: live continuation of an event being replayed (existing replay rule).
            for (const int c : candidates) {
                const auto s = static_cast<std::size_t>(c);
                if (ReplayMatchesEvent(s, dev, rawIdCode, userEvent, value, replay)) {
                    ResetReplayState(s, replay);
                    fate = PressFate::Passed;
                    return false;
                }
            }

            // Phase 4: hold the press back under one owner until an activation consumes it or the window releases it.
            const int owner = PickHoldBackOwner(candidates, isGp, cache, keys);
            const auto o = static_cast<std::size_t>(owner);
            if (!excl.filterWindowActive[o]) {
                excl.filterWindowActive[o] = true;
                excl.filterWindowTimer[o] = kComboToleranceSec;
                excl.heldBackSecs[o] = 0.f;
                excl.contestedLogged[o] = false;
            }
            retained[o].push_back(ev);
            fate = PressFate::HeldBack;
            OwnerOf(excl, isGp, code) = owner;
            MAGIC_DEBUG_LOG("[Input] HoldBack: {} held for slot={} (candidates={})", DescribeHeldCode(code, isGp),
                            owner, count);
            return true;
        }

        // The hand whose attack/block action an event drives, by its user event: one rule for the mouse, gamepad
        // triggers, keyboard binds and remaps (spec 005, research R2).
        std::optional<IntegratedMagic::Hand> HandOfUserEvent(const RE::BSFixedString& ue) {
            static const RE::BSFixedString kLeftAttackBlock{"Left Attack/Block"};
            static const RE::BSFixedString kRightAttackBlock{"Right Attack/Block"};
            if (ue == kLeftAttackBlock) return IntegratedMagic::Hand::Left;
            if (ue == kRightAttackBlock) return IntegratedMagic::Hand::Right;
            return std::nullopt;
        }

        HandEventPhase PhaseOf(float value, float heldSecs) {
            if (value > 0.f) return heldSecs <= 0.f ? HandEventPhase::Press : HandEventPhase::Held;
            return HandEventPhase::Release;
        }

        [[maybe_unused]] const char* HandName(IntegratedMagic::Hand h) {
            return h == IntegratedMagic::Hand::Left ? "Left" : "Right";
        }

    }

    bool GateHandEvent(const RE::BSFixedString& userEvent, float value, float heldSecs, HandButtonStore& hands,
                       const HandGateFlags& flags) {
        const auto hand = HandOfUserEvent(userEvent);
        if (!hand) return false;
        using enum IntegratedMagic::Hand;
        const auto other = (*hand == Left) ? Right : Left;
        const auto d = DecideHandEvent(hands.For(*hand), flags.PluginCast(*hand), flags.PluginCast(other),
                                       flags.mayCancel, PhaseOf(value, heldSecs), heldSecs);
        if (d.requestCancel) hands.cancelRequested = true;

        switch (d.outcome) {
            using enum HandGateOutcome;
            case KeptCancel:
                MAGIC_DEBUG_LOG("[Input] HandGate: {} press during plugin cast -> cancel session", HandName(*hand));
                break;
            case KeptIgnored:
                MAGIC_DEBUG_LOG("[Input] HandGate: {} press during plugin cast -> ignored (Press mode)",
                                HandName(*hand));
                break;
            case PassCancel:
                MAGIC_DEBUG_LOG("[Input] HandGate: {} other-hand press -> passes, cancel session", HandName(*hand));
                break;
            case Cleared:
                MAGIC_DEBUG_LOG("[Input] HandGate: {} button released -> gate cleared", HandName(*hand));
                break;
            default:
                break;
        }
        return d.filter;
    }

    int EffectiveComboCode(RE::INPUT_DEVICE dev, std::uint32_t rawIdCode) {
        const auto raw = static_cast<int>(rawIdCode);
        if (dev == RE::INPUT_DEVICE::kGamepad) return GamepadIdToIndex(raw);
        if (dev == RE::INPUT_DEVICE::kMouse) return kMouseButtonBase + raw;
        return raw;
    }

    bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
        static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        static const RE::BSFixedString statsMenu{"StatsMenu"};
        static const RE::BSFixedString mapMenu{"MapMenu"};
        static const RE::BSFixedString journalMenu{"Journal Menu"};
        static const RE::BSFixedString favoritesMenu{"FavoritesMenu"};
        static const RE::BSFixedString containerMenu{"ContainerMenu"};
        static const RE::BSFixedString barterMenu{"BarterMenu"};
        static const RE::BSFixedString trainingMenu{"Training Menu"};
        static const RE::BSFixedString craftingMenu{"Crafting Menu"};
        static const RE::BSFixedString giftMenu{"GiftMenu"};
        static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
        static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
        static const RE::BSFixedString loadingMenu{"Loading Menu"};
        static const RE::BSFixedString mainMenu{"Main Menu"};
        static const RE::BSFixedString console{"Console"};
        static const RE::BSFixedString mcm{"Mod Configuration Menu"};
        static const RE::BSFixedString tweenMenu{"Tween Menu"};
        static const RE::BSFixedString dialogueMenu{"Dialogue Menu"};
        static const RE::BSFixedString dialogueTopicMenu{"Dialogue Topic Menu"};
        static const RE::BSFixedString bestiary{"BestiaryMenu"};
        static const RE::BSFixedString ostim{"OstimSceneMenu"};

        return ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
               ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
               ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
               ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
               ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
               ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) || ui->IsMenuOpen(tweenMenu) ||
               ui->IsMenuOpen(dialogueMenu) || ui->IsMenuOpen(dialogueTopicMenu) || ui->IsMenuOpen(bestiary) ||
               ui->IsMenuOpen(ostim);
    }

    bool IsMagicMenuOpen() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        return ui->IsMenuOpen(magicMenu);
    }

    ProcessButtonEventsResult ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap, bool& wantCapture,
                                                  KeyStateStore& keys) {
        ProcessButtonEventsResult result{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        for (auto* e = *a_evns; e; e = e->next) {
            const auto* btn = e->AsButtonEvent();
            if (!btn) continue;
            RecordMouseHeartbeat(btn, keys);
            if (!btn->IsDown() && !btn->IsUp()) continue;

            const auto dev = btn->GetDevice();
            auto code = static_cast<int>(btn->GetIDCode());
            if (dev == RE::INPUT_DEVICE::kGamepad) code = GamepadIdToIndex(code);

            if (dev == RE::INPUT_DEVICE::kMouse) {
                if (IsMouseWheelId(code)) {
                    NoteWheelIgnored(btn, code);
                    continue;
                }
                const int mouseCode = kMouseButtonBase + code;
                if (mouseCode >= 0 && mouseCode < kMaxCode) {
                    (void)TryHandleCapture(btn, cap, wantCapture, RE::INPUT_DEVICE::kMouse, mouseCode);
                    keys.kbDown[static_cast<std::size_t>(mouseCode)].store(btn->IsDown(), std::memory_order_relaxed);
                }
                continue;
            }

            if (code < 0 || code >= kMaxCode) continue;
            (void)TryHandleCapture(btn, cap, wantCapture, dev, code);
            UpdateDownState(dev, code, btn->IsDown(), keys);

            if (btn->IsDown() && player && btn->QUserEvent() == "Shout"sv) {
                if (IsTransformPowerEquipped(player)) {
                    MAGIC_DEBUG_LOG("[Input] Shout pressed with transform -> ForceExitNoRestore");
                    result.forceExit = true;
                }
            }
        }

        return result;
    }

    void FilterMouseForPopup(RE::InputEvent** a_evns) {
        if (!Input::detail::g_popupOpenForInput.load(std::memory_order_relaxed)) return;

        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;
        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (cur->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                auto const* mm = static_cast<RE::MouseMoveEvent*>(cur);
                Input::detail::PushPopupInput({Input::detail::PopupInputKind::MouseDelta,
                                               static_cast<float>(mm->mouseInputX),
                                               static_cast<float>(mm->mouseInputY)});
                remove = true;
            } else if (cur->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto const* ts = static_cast<RE::ThumbstickEvent*>(cur);
                if (ts->IsLeft()) {
                    constexpr float kDeadzone = 0.15f;
                    constexpr float kSensitivity = 12.f;
                    const float ax = (std::abs(ts->xValue) > kDeadzone) ? ts->xValue : 0.f;
                    const float ay = (std::abs(ts->yValue) > kDeadzone) ? ts->yValue : 0.f;
                    if (ax != 0.f || ay != 0.f) {
                        Input::detail::PushPopupInput(
                            {Input::detail::PopupInputKind::StickDelta, ax * kSensitivity, -ay * kSensitivity});
                    }
                }
                remove = true;
            } else if (const auto* btn = cur->AsButtonEvent()) {
                using Key = RE::BSWin32GamepadDevice::Key;
                const auto btnDev = btn->GetDevice();
                const auto btnID = btn->GetIDCode();
                if (btnDev == RE::INPUT_DEVICE::kMouse && btnID == 0) {
                    if (btn->IsDown()) Input::detail::PushPopupInput({Input::detail::PopupInputKind::Click, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kMouse && btnID == 1) {
                    if (btn->IsDown())
                        Input::detail::PushPopupInput({Input::detail::PopupInputKind::RightClick, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kX)) {
                    if (btn->IsDown()) Input::detail::PushPopupInput({Input::detail::PopupInputKind::Click, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kB)) {
                    if (btn->IsDown())
                        Input::detail::PushPopupInput({Input::detail::PopupInputKind::RightClick, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kY)) {
                    if (btn->IsDown()) Input::detail::PushPopupInput({Input::detail::PopupInputKind::Close, 0.f, 0.f});
                    remove = true;
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }

    void FilterEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots,
                      const HotkeyCacheStore& cache, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                      HandButtonStore& hands, const HandGateFlags& flags) {
        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;
        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (const auto* btn = cur->AsButtonEvent()) {
                const auto dev = btn->GetDevice();
                auto code = static_cast<int>(btn->GetIDCode());
                const auto rawCode = btn->GetIDCode();
                if (dev == RE::INPUT_DEVICE::kGamepad) code = GamepadIdToIndex(code);
                // Wheel events always reach the game: never withheld, retained or replayed (spec 003, R3).
                const bool isWheel = dev == RE::INPUT_DEVICE::kMouse && IsMouseWheelId(code);

                if (!isWheel && code >= 0 && code < kMaxCode) {
                    remove = ShouldFilterAndSave(dev, code, rawCode, btn->QUserEvent(), btn->Value(),
                                                 btn->HeldDuration(), keys, slots, cache, excl, replay, retained);
#ifdef MAGIC_DEBUG_LOGGING
                    if (!remove && (dev == RE::INPUT_DEVICE::kMouse || dev == RE::INPUT_DEVICE::kKeyboard)) {
                        const int effCode = (dev == RE::INPUT_DEVICE::kMouse) ? kMouseButtonBase + code : code;
                        const int n = slots.ActiveSlots();
                        for (int slot = 0; slot < n; ++slot) {
                            if (ComboContains(cache.slots[static_cast<std::size_t>(slot)].kb, effCode)) {
                                MAGIC_DEBUG_LOG("[Input] FilterEvents: slot={} code={} PASSING", slot, effCode);
                                break;
                            }
                        }
                    }
#endif
                }
                // Only events that passed the combo rules reach the hand-button gate (spec 005, R3).
                if (!remove && !isWheel)
                    remove = GateHandEvent(btn->QUserEvent(), btn->Value(), btn->HeldDuration(), hands, flags);
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }

    void UpdateSlotsIfAllowed(bool blocked, float dt, SlotEdgeStore& slots, ExclusiveStore& excl,
                              const HotkeyCacheStore& cache, const KeyStateStore& keys, RetainedArr& retained,
                              DeferredVec& deferred, ReplayArr& replay, bool spellSystemActive, int activeSlot) {
        const auto& patches = IntegratedMagic::Config::MagicConfigAdapter::Get();
        if (!blocked)
            RecomputeSlotEdges(dt, slots, excl, cache, keys, patches, replay, retained, deferred, spellSystemActive,
                               activeSlot);
        else
            DrainWhenBlocked(slots, slots.pressedMask, slots.releasedMask);
    }

}