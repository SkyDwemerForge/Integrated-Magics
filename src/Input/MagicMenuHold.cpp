#include "Input/MagicMenuHold.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "Input/HotkeyMatcher.h"
#include "Input/InputFilter.h"
#include "PCH.h"

namespace Input::detail {

    namespace {

        using Combo = std::array<int, 3>;

        [[nodiscard]] bool IsKeyHeld(const KeyStateStore& keys, bool gp, int code) {
            return gp ? keys.IsGpDown(code) : keys.IsKbDown(code);
        }

        // Visits every combo the Magic Menu hold cares about: each active slot, then the wheel (research R2). The
        // visitor returns true to stop.
        template <class Fn>
        void ForEachHoldCombo(const HotkeyCacheStore& cache, const SlotEdgeStore& slots, bool gp, Fn&& fn) {
            const int n = slots.ActiveSlots();
            for (int slot = 0; slot < n; ++slot) {
                const auto& hk = cache.slots[static_cast<std::size_t>(slot)];
                if (fn(slot, gp ? hk.gp : hk.kb)) return;
            }
            (void)fn(kMenuHoldWheelOwner, gp ? cache.hud.gp : cache.hud.kb);
        }

        [[nodiscard]] bool IsHoldCode(const HotkeyCacheStore& cache, const SlotEdgeStore& slots, bool gp, int code) {
            bool found = false;
            ForEachHoldCombo(cache, slots, gp, [&](int, const Combo& combo) {
                found = ComboContains(combo, code);
                return found;
            });
            return found;
        }

        // First owner whose combo contains the code and is fully held now, or -1.
        [[nodiscard]] int FullyHeldOwnerWith(const HotkeyCacheStore& cache, const SlotEdgeStore& slots,
                                             const KeyStateStore& keys, bool gp, int code) {
            const auto& down = gp ? keys.gpDown : keys.kbDown;
            int owner = -1;
            ForEachHoldCombo(cache, slots, gp, [&](int o, const Combo& combo) {
                if (!ComboContains(combo, code) || !ComboDown(combo, down)) return false;
                owner = o;
                return true;
            });
            return owner;
        }

        [[maybe_unused]] std::string OwnerLabel(int owner) {
            return owner == kMenuHoldWheelOwner ? std::string("wheel") : std::format("slot={}", owner);
        }

        // A code's fate follows where its events are: Held while any is queued, Handing while any waits in the outbox,
        // otherwise the fallback.
        void RefreshFate(MagicMenuHoldStore& hold, bool gp, int code, MenuFate fallback) {
            const auto same = [&](const MenuHeldEvent& e) { return e.gp == gp && e.code == code; };
            auto& fate = MenuFateOf(hold, gp, code);
            if (std::ranges::any_of(hold.queue, same))
                fate = MenuFate::Held;
            else if (std::ranges::any_of(hold.outbox, same))
                fate = MenuFate::Handing;
            else
                fate = fallback;
        }

        // A fully held combo takes every queued or outbox event of its codes. The outbox holds the older events, so it
        // goes first and the last erased event of each code decides its fate (contract §3 step 1).
        void ConsumeForCombo([[maybe_unused]] int owner, const Combo& combo, bool gp, MagicMenuHoldStore& hold) {
            std::vector<std::pair<int, bool>> erased;  // code, last erased event was a release
            const auto take = [&](const MenuHeldEvent& e) {
                if (e.gp != gp || !ComboContains(combo, e.code)) return false;
                MAGIC_DEBUG_LOG("[Input] MenuHold: {} {}consumed by {}", DescribeHeldCode(e.code, e.gp),
                                e.isPress ? "" : "release ", OwnerLabel(owner));
                const auto it = std::ranges::find(erased, e.code, &std::pair<int, bool>::first);
                if (it == erased.end())
                    erased.emplace_back(e.code, !e.isPress);
                else
                    it->second = !e.isPress;
                return true;
            };
            std::erase_if(hold.outbox, take);
            std::erase_if(hold.queue, take);
            for (const auto& [code, lastRelease] : erased)
                MenuFateOf(hold, gp, code) = lastRelease ? MenuFate::None : MenuFate::Consumed;
        }

        // Contract §2 for one button event. Returns true to keep it from the menu.
        bool ClassifyMenuButton(const RE::ButtonEvent* btn, const KeyStateStore& keys, const SlotEdgeStore& slots,
                                const HotkeyCacheStore& cache, MagicMenuHoldStore& hold) {
            const auto dev = btn->GetDevice();
            const auto raw = btn->GetIDCode();
            // The mouse wheel keeps the spec 003 rule: never held, retained or replayed.
            if (dev == RE::INPUT_DEVICE::kMouse && IsMouseWheelId(static_cast<int>(raw))) return false;
            const int code = EffectiveComboCode(dev, raw);
            if (code < 0 || code >= kMaxCode) return false;

            const bool gp = dev == RE::INPUT_DEVICE::kGamepad;
            const float value = btn->Value();
            const float heldSecs = btn->HeldDuration();
            const bool isRelease = value <= 0.f;
            const bool isFresh = !isRelease && heldSecs <= 0.f;
            const MenuHeldEvent entry{RetainedEvent{dev, raw, btn->QUserEvent(), value, heldSecs}, code, gp, isFresh,
                                      0.f};
            auto& fate = MenuFateOf(hold, gp, code);

            if (isFresh) {
                if (!IsHoldCode(cache, slots, gp, code)) return false;
                if (const int owner = FullyHeldOwnerWith(cache, slots, keys, gp, code); owner != -1) {
                    fate = MenuFate::Consumed;
                    MAGIC_DEBUG_LOG("[Input] MenuHold: {} consumed by {}", DescribeHeldCode(code, gp),
                                    OwnerLabel(owner));
                    return true;
                }
                hold.queue.push_back(entry);
                fate = MenuFate::Held;
                MAGIC_DEBUG_LOG("[Input] MenuHold: {} held (queue={})", DescribeHeldCode(code, gp), hold.queue.size());
                return true;
            }

            // Held repeats and releases follow their press. A repeat is never stored.
            switch (fate) {
                case MenuFate::Held:
                    if (isRelease) hold.queue.push_back(entry);
                    return true;
                case MenuFate::Handing:
                    if (isRelease) hold.outbox.push_back(entry);
                    return true;
                case MenuFate::Consumed:
                    if (isRelease) {
                        fate = MenuFate::None;
                        MAGIC_DEBUG_LOG("[Input] MenuHold: {} release consumed", DescribeHeldCode(code, gp));
                    }
                    return true;
                default:
                    if (isRelease) fate = MenuFate::None;
                    return false;
            }
        }
    }

    void FilterMagicMenuEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots,
                               const HotkeyCacheStore& cache, MagicMenuHoldStore& hold) {
        if (!a_evns) return;
        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;
        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;
            if (const auto* btn = cur->AsButtonEvent()) remove = ClassifyMenuButton(btn, keys, slots, cache, hold);

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

    void TickMagicMenuHold(float dt, const KeyStateStore& keys, const SlotEdgeStore& slots,
                           const HotkeyCacheStore& cache, MagicMenuHoldStore& hold) {
        // 1. A completed hotkey takes its keys, wherever they wait.
        if (!hold.queue.empty() || !hold.outbox.empty()) {
            for (const bool gp : {false, true}) {
                const auto& down = gp ? keys.gpDown : keys.kbDown;
                ForEachHoldCombo(cache, slots, gp, [&](int owner, const Combo& combo) {
                    if (ComboDown(combo, down)) ConsumeForCombo(owner, combo, gp, hold);
                    return false;
                });
            }
        }

        // 2. Age the queued presses.
        const float step = dt > 0.f ? dt : 0.f;
        for (auto& e : hold.queue)
            if (e.isPress) e.ageSecs += step;

        // 3. Hand over in arrival order: a press when its window ended or its key is released, a release at once.
        while (!hold.queue.empty()) {
            const MenuHeldEvent front = hold.queue.front();
            if (front.isPress) {
                const bool windowEnded = front.ageSecs >= kMagicMenuHoldSec;
                if (!windowEnded && IsKeyHeld(keys, front.gp, front.code)) break;
                MAGIC_DEBUG_LOG("[Input] MenuHold: {} handed to menu after {:.2f}s ({})",
                                DescribeHeldCode(front.code, front.gp), front.ageSecs,
                                windowEnded ? "window" : "released");
            }
            hold.queue.erase(hold.queue.begin());
            hold.outbox.push_back(front);
            RefreshFate(hold, front.gp, front.code, MenuFate::Handing);
        }
    }

    std::optional<RetainedEvent> PopMagicMenuOutbox(MagicMenuHoldStore& hold) {
        if (hold.outbox.empty()) return std::nullopt;
        const MenuHeldEvent sent = hold.outbox.front();
        hold.outbox.pop_front();
        RefreshFate(hold, sent.gp, sent.code, sent.isPress ? MenuFate::Passed : MenuFate::None);
        return sent.ev;
    }

    void ClearMagicMenuHold(MagicMenuHoldStore& hold, [[maybe_unused]] const char* reason) {
        const auto n = hold.queue.size() + hold.outbox.size();
        if (n > 0) MAGIC_DEBUG_LOG("[Input] MenuHold: {} queued event(s) dropped ({})", n, reason);
        hold.queue.clear();
        hold.outbox.clear();
        hold.kbFate.fill(MenuFate::None);
        hold.gpFate.fill(MenuFate::None);
    }
}
