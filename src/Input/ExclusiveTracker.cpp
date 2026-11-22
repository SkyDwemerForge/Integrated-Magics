#include "Input/ExclusiveTracker.h"

#include <array>
#include <utility>
#include <vector>

#include "Input/HotkeyMatcher.h"
#include "Input/InputFilter.h"
#include "Input/ReplaySystem.h"
#include "PCH.h"

namespace Input::detail {

    namespace {

        inline void AtomicFetchOrU64(std::atomic<std::uint64_t>& a, std::uint64_t bits,
                                     std::memory_order order = std::memory_order_relaxed) {
            std::uint64_t cur = a.load(order);
            while (!a.compare_exchange_weak(cur, (cur | bits), order, order));
        }

        template <class DownArr, class KeepArr, class CodesArr>
        inline void MarkKeepIfDown(const CodesArr& codes, const DownArr& down, KeepArr& keep) {
            for (int code : codes) {
                if (code < 0 || code >= kMaxCode) continue;
                const auto idx = static_cast<std::size_t>(code);
                if (down[idx].load(std::memory_order_relaxed)) keep[idx] = true;
            }
        }

        using SlotFlags = std::array<bool, kInputMaxSlots>;

        [[nodiscard]] bool IsGamepadEvent(const RetainedEvent& ev) { return ev.dev == RE::INPUT_DEVICE::kGamepad; }

        // Press or release (not a held repeat): the only events worth a log line.
        [[nodiscard]] bool IsPressOrRelease(const RetainedEvent& ev) { return ev.heldSecs <= 0.f || ev.value <= 0.f; }

        void CloseFilterWindow(std::size_t s, ExclusiveStore& excl) {
            excl.filterWindowActive[s] = false;
            excl.filterWindowTimer[s] = 0.f;
            excl.heldBackSecs[s] = 0.f;
            excl.contestedLogged[s] = false;
        }

        // Sets the press fate of a held-back event's code. A code whose event is its release has finished its press,
        // so its fate goes back to None (spec 004, research R11).
        void ResolveFate(const RetainedEvent& ev, PressFate fate, ExclusiveStore& excl) {
            const bool gp = IsGamepadEvent(ev);
            const int code = EffectiveComboCode(ev.dev, ev.rawIdCode);
            if (code < 0) return;
            FateOf(excl, gp, code) = (ev.value <= 0.f) ? PressFate::None : fate;
            OwnerOf(excl, gp, code) = -1;
        }

        // Applied in order, so the last event of each code decides.
        void ResolveFates(const std::vector<RetainedEvent>& events, PressFate fate, ExclusiveStore& excl) {
            for (const auto& ev : events) ResolveFate(ev, fate, excl);
        }

        void ResetAllFates(ExclusiveStore& excl) {
            excl.kbFate.fill(PressFate::None);
            excl.gpFate.fill(PressFate::None);
            excl.kbOwner.fill(-1);
            excl.gpOwner.fill(-1);
        }

        [[nodiscard]] bool ComboHasCode(const SlotHotkeys& hk, bool gp, int code) {
            return code >= 0 && ComboContains(gp ? hk.gp : hk.kb, code);
        }

        struct HoldReason {
            int slot{-1};
            const char* why{""};
        };

        // Why a held-back window may stay open after kComboToleranceSec (spec 004, research R3b/R3c): a slot that uses
        // one of its buttons is still pending (up to kMaxContestedHoldSec), or has 2+ of its buttons held (up to
        // kMaxChordHoldSec). 2+ held is a chord in progress, or a combo completed this frame: its slot only goes
        // pending later in this frame, so without it a button arriving in the frame the window ends would let the first
        // one leak. The chord cap counts the frame that crosses it, since input seen now arrived during the last dt.
        [[nodiscard]] HoldReason FindHoldReason(std::size_t s, float dt, const ExclusiveStore& excl,
                                                const HotkeyCacheStore& cache, const KeyStateStore& keys,
                                                const RetainedArr& retained, int n) {
            const bool allowPending = excl.heldBackSecs[s] < kMaxContestedHoldSec;
            const bool allowChord = excl.heldBackSecs[s] - dt < kMaxChordHoldSec;
            HoldReason chord{};
            if (!allowPending && !allowChord) return chord;
            for (const auto& ev : retained[s]) {
                const bool gp = IsGamepadEvent(ev);
                const int code = EffectiveComboCode(ev.dev, ev.rawIdCode);
                for (int t = 0; t < n; ++t) {
                    const auto& hk = cache.slots[static_cast<std::size_t>(t)];
                    if (!ComboHasCode(hk, gp, code)) continue;
                    if (allowPending && excl.pendingSrc[static_cast<std::size_t>(t)] != PendingSrc::None)
                        return {t, "pending"};
                    if (allowChord && chord.slot < 0) {
                        const auto& combo = gp ? hk.gp : hk.kb;
                        const auto& down = gp ? keys.gpDown : keys.kbDown;
                        const int held = ComboHeldCount(combo, down);
                        if (held >= 2) chord = {t, held < ComboKeyCount(combo) ? "chord" : "complete"};
                    }
                }
            }
            return chord;
        }

        // When a slot activates, every held-back or queued event whose button belongs to its combo is dropped: the
        // game never sees it, and its fate becomes Consumed (spec 004, research R3a and R11).
        void ConsumeHeldBackForCombo([[maybe_unused]] int winner, const SlotHotkeys& hk, int n, ExclusiveStore& excl,
                                     RetainedArr& retained, DeferredVec& deferred) {
            auto consumeIfOwned = [&](const RetainedEvent& ev) {
                const bool gp = IsGamepadEvent(ev);
                const int code = EffectiveComboCode(ev.dev, ev.rawIdCode);
                if (!ComboHasCode(hk, gp, code)) return false;
                ResolveFate(ev, PressFate::Consumed, excl);
                if (IsPressOrRelease(ev))
                    MAGIC_DEBUG_LOG("[Input] HoldBack: {} consumed by slot={}", DescribeHeldCode(code, gp), winner);
                return true;
            };
            for (int t = 0; t < n; ++t) {
                const auto ts = static_cast<std::size_t>(t);
                const auto removed = std::erase_if(retained[ts], consumeIfOwned);
                if (removed > 0 && retained[ts].empty() && excl.filterWindowActive[ts]) CloseFilterWindow(ts, excl);
            }
            std::erase_if(deferred, [&](const DeferredReplayEvent& d) { return consumeIfOwned(d.ev); });
        }

        // Does `b` beat `a` on one device? A strictly longer containing combo wins; for identical combos the lower slot
        // wins, but only between two slots rising in the same frame (spec 004, research R4, FR-009).
        [[nodiscard]] bool BeatsOnDevice(const std::array<int, 3>& ca, const std::array<int, 3>& cb, bool bothDown,
                                         bool bRising, bool bLower, bool& identical) {
            if (!bothDown) return false;
            if (ComboStrictSubset(ca, cb)) {
                identical = false;
                return true;
            }
            if (bRising && bLower && ComboSameKeys(ca, cb)) {
                identical = true;
                return true;
            }
            return false;
        }

        // Pass 2 of RecomputeSlotEdges: among slots rising this frame, the longest combo completed at that moment wins;
        // a loser is suppressed until its combo is released (spec 004, research R4, FR-009).
        void ResolveOverlaps(int n, const HotkeyCacheStore& cache, ExclusiveStore& excl, const SlotFlags& kb,
                             const SlotFlags& gp, const SlotFlags& prev, SlotFlags& acc, ReplayArr& replay,
                             RetainedArr& retained, DeferredVec& deferred) {
            for (int a = 0; a < n; ++a) {
                const auto sa = static_cast<std::size_t>(a);
                if (!acc[sa] || prev[sa]) continue;
                const auto& ha = cache.slots[sa];
                for (int b = 0; b < n; ++b) {
                    if (b == a) continue;
                    const auto sb = static_cast<std::size_t>(b);
                    const bool bRising = acc[sb] && !prev[sb];
                    if (!bRising && excl.pendingSrc[sb] == PendingSrc::None) continue;
                    const auto& hb = cache.slots[sb];
                    bool identical = false;
                    const bool lost = BeatsOnDevice(ha.kb, hb.kb, kb[sa] && kb[sb], bRising, b < a, identical) ||
                                      BeatsOnDevice(ha.gp, hb.gp, gp[sa] && gp[sb], bRising, b < a, identical);
                    if (!lost) continue;
                    acc[sa] = false;
                    excl.suppressedUntilRelease[sa] = true;
                    DiscardExclusivePending(sa, excl, replay, retained, deferred);
                    MAGIC_DEBUG_LOG("[Input] RecomputeSlotEdges: slot={} SUPPRESSED by slot={} ({})", a, b,
                                    identical ? "identical combo" : "longer combo");
                    break;
                }
            }
        }

        bool ComputeAcceptedExclusive(int slot, const SlotHotkeys& hk, bool prevAccepted, bool kbNow, bool gpNow,
                                      bool rawNow, float dt, ExclusiveStore& excl, SlotEdgeStore& slots,
                                      const KeyStateStore& keys, ReplayArr& replay, RetainedArr& retained,
                                      DeferredVec& deferred, const IntegratedMagic::Config::IPatchSettings& patches) {
            const auto s = static_cast<std::size_t>(slot);

            // Rising edge of the raw combo, read before the previous state is overwritten: an exclusive refusal is
            // logged once per press, not every frame (spec 003, research R7).
            const bool kbRose = kbNow && !excl.prevRawKbDown[s];
            const bool gpRose = gpNow && !excl.prevRawGpDown[s];

            excl.prevRawKbDown[s] = kbNow;
            excl.prevRawGpDown[s] = gpNow;

            const bool requireExcl = patches.RequireExclusiveHotkey();

            {
                const bool anyComboNow = AnyComboKeyDown(hk.kb, keys.kbDown) || AnyComboKeyDown(hk.gp, keys.gpDown);
                const bool prevAnyDown = excl.prevAnyKeyDown[s];
                excl.prevAnyKeyDown[s] = anyComboNow;

                if (!anyComboNow) {
                    excl.simWindowActive[s] = false;
                    excl.simWindowRemaining[s] = 0.f;
                } else if (anyComboNow && !prevAnyDown) {
                    excl.simWindowActive[s] = true;
                    excl.simWindowRemaining[s] = kPressBothAtSameTimeWindowSec;

                    MAGIC_DEBUG_LOG("[Input] ComputeAcceptedExclusive: slot={} sim-window OPENED ({:.2f}s)", slot,
                                    kPressBothAtSameTimeWindowSec);

                } else if (excl.simWindowActive[s]) {
                    excl.simWindowRemaining[s] -= dt;
                    if (excl.simWindowRemaining[s] <= 0.f) {
                        excl.simWindowActive[s] = false;

                        MAGIC_DEBUG_LOG("[Input] ComputeAcceptedExclusive: slot={} sim-window EXPIRED", slot);
                    }
                }
            }

            if (prevAccepted) {
                const bool wasDeactivated = excl.deactivatedThisPress[s];
                DiscardExclusivePending(s, excl, replay, retained, deferred);
                if (wasDeactivated) return false;
                return rawNow;
            }

            if (HasExclusivePending(s, excl)) {
                const auto src = excl.pendingSrc[s];
                const bool stillDown = (src == PendingSrc::Kb) ? kbNow : gpNow;
                const bool srcIsMulti = (src == PendingSrc::Kb) ? slots.slotIsKbMultiKey[s] : slots.slotIsGpMultiKey[s];
                const bool simPatch = patches.PressBothAtSame() && srcIsMulti;

                if (requireExcl) {
                    const bool srcIsKb = (src == PendingSrc::Kb);
                    const int extra = srcIsKb
                                          ? FindExtraHeldCode(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                                          : FindExtraHeldCode(hk.gp, keys.gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                    const bool stillExcl = AnyEnabled(srcIsKb ? hk.kb : hk.gp) && extra < 0;
                    if (!stillExcl) {
                        MAGIC_DEBUG_LOG(
                            "[Input] ComputeAcceptedExclusive: slot={} pending CANCELLED (no longer exclusive) "
                            "extra={}",
                            slot, DescribeHeldCode(extra, !srcIsKb));

                        ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                        return false;
                    }
                }

                if (srcIsMulti) {
                    if (stillDown && !excl.fullComboSeen[s]) {
                        if (simPatch && !excl.simWindowActive[s]) {
                            MAGIC_DEBUG_LOG(
                                "[Input] ComputeAcceptedExclusive: slot={} full combo REJECTED by sim-window (expired)",
                                slot);

                            ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                            return false;
                        }
                        excl.fullComboSeen[s] = true;
                        excl.pendingTimer[s] = kComboToleranceSec;

                        MAGIC_DEBUG_LOG(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key full combo seen, timer reset to "
                            "{:.3f}s",
                            slot, kComboToleranceSec);
                    }

                    if (!stillDown) {
                        if (excl.fullComboSeen[s]) {
                            MAGIC_DEBUG_LOG(
                                "[Input] ComputeAcceptedExclusive: slot={} multi-key released after full combo -> "
                                "ACCEPTED",
                                slot);

                            DiscardExclusivePending(s, excl, replay, retained, deferred);
                            return true;
                        }

                        const bool anyHeld = (src == PendingSrc::Gp) ? AnyComboKeyDown(hk.gp, keys.gpDown)
                                                                     : AnyComboKeyDown(hk.kb, keys.kbDown);
                        if (anyHeld) {
                            if (simPatch) {
                                excl.pendingTimer[s] -= dt;
                                if (excl.pendingTimer[s] <= 0.0f) {
                                    MAGIC_DEBUG_LOG(
                                        "[Input] ComputeAcceptedExclusive: slot={} multi-key partial hold TIMEOUT -> "
                                        "Cancelled",
                                        slot);

                                    ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                                }
                            }
                            return false;
                        }

                        MAGIC_DEBUG_LOG(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key all released without full combo -> "
                            "Cancelled",
                            slot);

                        ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                        return false;
                    }

                    excl.pendingTimer[s] -= dt;
                    if (excl.pendingTimer[s] <= 0.0f) {
                        MAGIC_DEBUG_LOG(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key timer elapsed -> Success (held down)",
                            slot);

                        ClearExclusivePending(s, ClearReason::Success, excl, replay, retained, deferred);
                        return true;
                    }
                    return false;

                } else {
                    if (!stillDown) {
                        const bool wasDeactivated = excl.deactivatedThisPress[s];
                        DiscardExclusivePending(s, excl, replay, retained, deferred);
                        if (wasDeactivated) {
                            MAGIC_DEBUG_LOG(
                                "[Input] ComputeAcceptedExclusive: slot={} single-key released -> IGNORED "
                                "(deactivatedThisPress)",
                                slot);

                            return false;
                        }

                        MAGIC_DEBUG_LOG("[Input] ComputeAcceptedExclusive: slot={} single-key released -> ACCEPTED",
                                        slot);

                        return true;
                    }
                    excl.pendingTimer[s] -= dt;
                    if (excl.pendingTimer[s] <= 0.0f) {
                        MAGIC_DEBUG_LOG(
                            "[Input] ComputeAcceptedExclusive: slot={} single-key timer elapsed -> ACCEPTED (held)",
                            slot);
                        ClearExclusivePending(s, ClearReason::Success, excl, replay, retained, deferred);
                        return true;
                    }
                    return false;
                }
            }

            if (!rawNow) return false;

            if (kbNow) {
                const bool kbIsMulti = slots.slotIsKbMultiKey[s];
                if (!requireExcl || ComboExclusiveNow(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)) {
                    if (const bool kbSimPatch = patches.PressBothAtSame() && kbIsMulti;
                        kbSimPatch && !excl.simWindowActive[s]) {
                        MAGIC_DEBUG_LOG("[Input] ComputeAcceptedExclusive: slot={} kb REJECTED sim-window not active",
                                        slot);

                        return false;
                    }
                    excl.pendingSrc[s] = PendingSrc::Kb;
                    excl.pendingTimer[s] = kbIsMulti ? kComboToleranceSec : kSingleKeyAcceptDelaySec;
                    excl.fullComboSeen[s] = false;
                    return false;
                }
                if (kbRose) {
                    MAGIC_DEBUG_LOG(
                        "[Input] ComputeAcceptedExclusive: slot={} kb REFUSED (not exclusive) extra={}", slot,
                        DescribeHeldCode(FindExtraHeldCode(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera),
                                         false));
                }
            }

            if (gpNow) {
                const bool gpIsMulti = slots.slotIsGpMultiKey[s];
                if (!requireExcl || ComboExclusiveNow(hk.gp, keys.gpDown, IsAllowedExtra_Gamepad_MoveOrCamera)) {
                    if (const bool gpSimPatch = patches.PressBothAtSame() && gpIsMulti;
                        gpSimPatch && !excl.simWindowActive[s]) {
                        MAGIC_DEBUG_LOG("[Input] ComputeAcceptedExclusive: slot={} gp REJECTED sim-window not active",
                                        slot);

                        return false;
                    }
                    excl.pendingSrc[s] = PendingSrc::Gp;
                    excl.pendingTimer[s] = gpIsMulti ? kComboToleranceSec : kSingleKeyAcceptDelaySec;
                    excl.fullComboSeen[s] = false;
                    return false;
                }
                if (gpRose) {
                    MAGIC_DEBUG_LOG(
                        "[Input] ComputeAcceptedExclusive: slot={} gp REFUSED (not exclusive) extra={}", slot,
                        DescribeHeldCode(FindExtraHeldCode(hk.gp, keys.gpDown, IsAllowedExtra_Gamepad_MoveOrCamera),
                                         true));
                }
            }

            return false;
        }

        // Pass 1 of RecomputeSlotEdges for one slot: acceptance as before. A slot that lost an overlap stays inactive
        // until its combo is released (spec 004, research R4).
        bool ComputeSlotAcceptance(int slot, const SlotHotkeys& hk, bool kbNow, bool gpNow, bool prevAcc, float dt,
                                   ExclusiveStore& excl, SlotEdgeStore& slots, const KeyStateStore& keys,
                                   const IntegratedMagic::Config::IPatchSettings& patches, ReplayArr& replay,
                                   RetainedArr& retained, DeferredVec& deferred) {
            const auto s = static_cast<std::size_t>(slot);
            const bool rawNow = kbNow || gpNow;
            if (excl.suppressedUntilRelease[s]) {
                if (rawNow) {
                    excl.prevRawKbDown[s] = kbNow;
                    excl.prevRawGpDown[s] = gpNow;
                    DiscardExclusivePending(s, excl, replay, retained, deferred);
                    return false;
                }
                excl.suppressedUntilRelease[s] = false;
            }
            if (patches.RequireExclusiveHotkey() || slots.slotIsMultiKey[s]) {
                return ComputeAcceptedExclusive(slot, hk, prevAcc, kbNow, gpNow, rawNow, dt, excl, slots, keys, replay,
                                                retained, deferred, patches);
            }
            excl.prevRawKbDown[s] = kbNow;
            excl.prevRawGpDown[s] = gpNow;
            DiscardExclusivePending(s, excl, replay, retained, deferred);
            return rawNow;
        }
    }

    void DiscardExclusivePending(std::size_t s, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                                 DeferredVec& deferred) {
        if (excl.pendingSrc[s] != PendingSrc::None || !retained[s].empty()) {
            MAGIC_DEBUG_LOG("[Input] DiscardExclusivePending: slot={} (had pending src={} retained={})", s,
                            static_cast<int>(std::to_underlying(excl.pendingSrc[s])), retained[s].size());
        }

        CloseFilterWindow(s, excl);
        // Discarded presses never reach the game, so their held repeats and releases must not reach it alone (R11).
        ResolveFates(retained[s], PressFate::Consumed, excl);
        retained[s].clear();
        ClearDeferredReplayEventsForSlot(s, deferred);
        excl.pendingSrc[s] = PendingSrc::None;
        excl.pendingTimer[s] = 0.0f;
        excl.fullComboSeen[s] = false;
        ResetReplayState(s, replay);
    }

    void ClearExclusivePending(std::size_t s, ClearReason reason, ExclusiveStore& excl, ReplayArr& replay,
                               RetainedArr& retained, DeferredVec& deferred) {
#ifdef MAGIC_DEBUG_LOGGING
        const char* reasonStr = (reason == ClearReason::Success)   ? "Success"
                                : (reason == ClearReason::Timeout) ? "Timeout"
                                                                   : "Cancelled";
#endif
        MAGIC_DEBUG_LOG("[Input] ClearExclusivePending: slot={} reason={} retainedEvents={}", s, reasonStr,
                        retained[s].size());

        if (reason != ClearReason::Success) {
            ClearDeferredReplayEventsForSlot(s, deferred);
            ResetReplayState(s, replay);
            if (excl.filterWindowActive[s]) {
                // Expire the window now: TickFilterWindows decides next frame whether another slot still needs these
                // buttons or they go to the game (spec 004, research R3d). Costs about one frame on this path only.
                excl.filterWindowTimer[s] = 0.f;
                MAGIC_DEBUG_LOG("[Input] ClearExclusivePending: slot={} {} held-back event(s) deferred to window", s,
                                retained[s].size());
            } else {
                ResolveFates(retained[s], PressFate::Passed, excl);
                retained[s].clear();
            }
            excl.simWindowActive[s] = false;
            excl.simWindowRemaining[s] = 0.f;
        } else {
            CloseFilterWindow(s, excl);
            ClearDeferredReplayEventsForSlot(s, deferred);
            ResetReplayState(s, replay);
            ResolveFates(retained[s], PressFate::Consumed, excl);
            retained[s].clear();
        }
        excl.pendingSrc[s] = PendingSrc::None;
        excl.pendingTimer[s] = 0.0f;
        excl.fullComboSeen[s] = false;
    }

    void ClearEdgeStateOnly(SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                            DeferredVec& deferred) {
        MAGIC_DEBUG_LOG(
            "[Input] ClearEdgeStateOnly: clearing edge state without discarding pending or resetting replay state");

        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            slots.slotDown[s].store(false, std::memory_order_relaxed);
            slots.slotWasAccepted[s] = false;
            slots.slotWasAccepted[s] = false;
            excl.fullComboSeen[s] = false;
            excl.prevRawKbDown[s] = false;
            excl.prevRawGpDown[s] = false;
            excl.prevAnyKeyDown[s] = false;
            excl.simWindowActive[s] = false;
            excl.simWindowRemaining[s] = 0.f;
            excl.filterWindowActive[s] = false;
            excl.filterWindowTimer[s] = 0.f;
            DiscardExclusivePending(s, excl, replay, retained, deferred);
            excl.suppressedUntilRelease[s] = false;
            excl.heldBackSecs[s] = 0.f;
            excl.contestedLogged[s] = false;
        }
        ResetAllFates(excl);
        slots.pressedMask.store(0uLL, std::memory_order_relaxed);
        slots.releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void ClearLikelyStuckKeysAfterMenuClose(KeyStateStore& keys, SlotEdgeStore& slots, ExclusiveStore& excl,
                                            const HotkeyCacheStore& cache, ReplayArr& replay, RetainedArr& retained,
                                            DeferredVec& deferred) {
        std::array<bool, kMaxCode> keepKb{};
        std::array<bool, kMaxCode> keepGp{};
        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = cache.slots[static_cast<std::size_t>(slot)];
            MarkKeepIfDown(hk.kb, keys.kbDown, keepKb);
            MarkKeepIfDown(hk.gp, keys.gpDown, keepGp);
        }
        for (int i = 0; i < kMaxCode; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (!keepKb[idx]) keys.kbDown[idx].store(false, std::memory_order_relaxed);
            if (!keepGp[idx]) keys.gpDown[idx].store(false, std::memory_order_relaxed);
        }
        keys.kbDown[static_cast<std::size_t>(kDIK_Escape)].store(false, std::memory_order_relaxed);
        ClearEdgeStateOnly(slots, excl, replay, retained, deferred);
    }

    void ResetExclusiveState(SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                             DeferredVec& deferred) {
        MAGIC_DEBUG_LOG("[Input] ResetExclusiveState: resetting all exclusive pending and edge state");

        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            excl.prevRawKbDown[s] = false;
            excl.prevRawGpDown[s] = false;
            excl.prevAnyKeyDown[s] = false;
            excl.simWindowActive[s] = false;
            excl.simWindowRemaining[s] = 0.f;
            DiscardExclusivePending(s, excl, replay, retained, deferred);
            slots.slotDown[s].store(false, std::memory_order_relaxed);
            slots.slotWasAccepted[s] = false;
            excl.filterWindowActive[s] = false;
            excl.filterWindowTimer[s] = 0.f;
            excl.deactivatedThisPress[s] = false;
            excl.suppressedUntilRelease[s] = false;
            excl.heldBackSecs[s] = 0.f;
            excl.contestedLogged[s] = false;
        }
        ResetAllFates(excl);
        slots.pressedMask.store(0uLL, std::memory_order_relaxed);
        slots.releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void RecomputeSlotEdges(float dt, SlotEdgeStore& slots, ExclusiveStore& excl, const HotkeyCacheStore& cache,
                            const KeyStateStore& keys, const IntegratedMagic::Config::IPatchSettings& patches,
                            ReplayArr& replay, RetainedArr& retained, DeferredVec& deferred, bool spellSystemActive,
                            int activeSlot) {
        TickFilterWindows(dt, excl, slots, cache, keys, retained, deferred);

        const int n = slots.ActiveSlots();
        SlotFlags kbArr{};
        SlotFlags gpArr{};
        SlotFlags rawArr{};
        SlotFlags prevArr{};
        SlotFlags accArr{};

        // Pass 1: per-slot acceptance.
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            const auto& hk = cache.slots[s];
            kbArr[s] = ComboDown(hk.kb, keys.kbDown);
            gpArr[s] = ComboDown(hk.gp, keys.gpDown);
            rawArr[s] = kbArr[s] || gpArr[s];
            prevArr[s] = slots.slotDown[s].load(std::memory_order_relaxed);
            accArr[s] = ComputeSlotAcceptance(slot, hk, kbArr[s], gpArr[s], prevArr[s], dt, excl, slots, keys, patches,
                                              replay, retained, deferred);
        }

        // Pass 2: longest combo completed at the same moment wins (FR-009).
        ResolveOverlaps(n, cache, excl, kbArr, gpArr, prevArr, accArr, replay, retained, deferred);

        // Pass 3: publish edges; an activation consumes every held-back button of its combo (R3a).
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            const bool accNow = accArr[s];
            if (accNow != prevArr[s]) {
                MAGIC_DEBUG_LOG("[Input] RecomputeSlotEdges: slot={} EDGE {} (kb={} gp={} exclusive={} multiKey={})",
                                slot, accNow ? "PRESSED" : "RELEASED", kbArr[s], gpArr[s],
                                patches.RequireExclusiveHotkey(), slots.slotIsMultiKey[s]);

                slots.slotDown[s].store(accNow, std::memory_order_relaxed);
                AtomicFetchOrU64(accNow ? slots.pressedMask : slots.releasedMask, (1uLL << slot));
                if (accNow) ConsumeHeldBackForCombo(slot, cache.slots[s], n, excl, retained, deferred);
            }

            if (accNow)
                slots.slotWasAccepted[s] = true;
            else if (!rawArr[s]) {
                if (!spellSystemActive || activeSlot != slot) slots.slotWasAccepted[s] = false;
            }
        }
    }

    void TickFilterWindows(float dt, ExclusiveStore& excl, SlotEdgeStore& slots, const HotkeyCacheStore& cache,
                           const KeyStateStore& keys, RetainedArr& retained, DeferredVec& deferred) {
        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            if (!excl.filterWindowActive[s]) continue;
            excl.heldBackSecs[s] += dt;
            excl.filterWindowTimer[s] -= dt;
            if (excl.filterWindowTimer[s] > 0.f) continue;

            // Keep holding while a slot that uses one of these buttons is pending, or a chord is in progress (R3b/R3c).
            if (const auto reason = FindHoldReason(s, dt, excl, cache, keys, retained, n); reason.slot >= 0) {
                excl.filterWindowTimer[s] = 0.f;
                if (!excl.contestedLogged[s]) {
                    excl.contestedLogged[s] = true;
                    MAGIC_DEBUG_LOG("[Input] HoldBack: slot={} window extended (slot={} {})", slot, reason.slot,
                                    reason.why);
                }
                continue;
            }

            // Nothing needs them: hand the held-back events to the game, as before (FR-003). Their held repeats and
            // releases follow them (R11).
            ResolveFates(retained[s], PressFate::Passed, excl);
            for (auto const& ev : retained[s]) {
                if (IsPressOrRelease(ev)) {
                    MAGIC_DEBUG_LOG("[Input] HoldBack: {} handed to game (slot={})",
                                    DescribeHeldCode(EffectiveComboCode(ev.dev, ev.rawIdCode), IsGamepadEvent(ev)),
                                    slot);
                }
                QueueDeferredReplayEvent(s, ev, deferred);
            }
            retained[s].clear();
            CloseFilterWindow(s, excl);
        }
    }
}