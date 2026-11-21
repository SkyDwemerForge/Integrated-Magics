#include "Input/QuickBindResolver.h"

#include <algorithm>

#include "Input/HotkeyMatcher.h"
#include "PCH.h"

namespace Input::detail {

    namespace {

        struct SlotDown {
            bool kb{false};
            bool gp{false};
        };

        [[nodiscard]] constexpr std::uint64_t Bit(int s) noexcept { return 1uLL << static_cast<unsigned>(s); }

        // `t` has a strictly longer hotkey that contains the one `s` completed, on that device. With `requireDown`,
        // `t` must be fully held on it too.
        [[nodiscard]] bool LongerContains(const SlotHotkeys& hs, const SlotHotkeys& ht, const SlotDown& ds,
                                          const SlotDown& dt, bool requireDown) {
            if (ds.kb && (!requireDown || dt.kb) && ComboStrictSubset(hs.kb, ht.kb)) return true;
            if (ds.gp && (!requireDown || dt.gp) && ComboStrictSubset(hs.gp, ht.gp)) return true;
            return false;
        }

        [[nodiscard]] bool SameKeysHeld(const SlotHotkeys& hs, const SlotHotkeys& ht, const SlotDown& ds,
                                        const SlotDown& dt) {
            return (ds.kb && dt.kb && ComboSameKeys(hs.kb, ht.kb)) || (ds.gp && dt.gp && ComboSameKeys(hs.gp, ht.gp));
        }

        struct Frame {
            int n{0};
            const HotkeyCacheStore& cache;
            const std::array<SlotDown, kInputMaxSlots>& down;
            std::uint64_t now{0};

            [[nodiscard]] const SlotHotkeys& Hk(int s) const { return cache.slots[static_cast<std::size_t>(s)]; }
            [[nodiscard]] const SlotDown& Down(int s) const { return down[static_cast<std::size_t>(s)]; }

            // A fully held slot whose hotkey strictly contains s's, or -1.
            [[nodiscard]] int LongerHeld(int s) const {
                for (int t = 0; t < n; ++t)
                    if (t != s && (now & Bit(t)) && LongerContains(Hk(s), Hk(t), Down(s), Down(t), true)) return t;
                return -1;
            }

            // Any slot, held or not, whose hotkey strictly contains s's, or -1.
            [[nodiscard]] int LongerExists(int s) const {
                for (int t = 0; t < n; ++t)
                    if (t != s && LongerContains(Hk(s), Hk(t), Down(s), Down(t), false)) return t;
                return -1;
            }

            // A lower slot rising now with the same keys, or -1.
            [[nodiscard]] int IdenticalLowerRising(int s, std::uint64_t rising) const {
                for (int t = 0; t < s; ++t)
                    if ((rising & Bit(t)) && SameKeysHeld(Hk(s), Hk(t), Down(s), Down(t))) return t;
                return -1;
            }
        };
    }

    std::uint64_t ResolveQuickBind(QuickBindState& st, bool active, float dt, int slotCount,
                                   const HotkeyCacheStore& cache, const KeyStateStore& keys,
                                   const SlotEdgeStore& slots) {
        const int n = std::clamp(std::min(slotCount, slots.ActiveSlots()), 0, kInputMaxSlots);
        std::array<SlotDown, kInputMaxSlots> down{};
        std::uint64_t now = 0;
        for (int s = 0; s < n; ++s) {
            const auto& hk = cache.slots[static_cast<std::size_t>(s)];
            auto& d = down[static_cast<std::size_t>(s)];
            d = {ComboDown(hk.kb, keys.kbDown), ComboDown(hk.gp, keys.gpDown)};
            if (d.kb || d.gp) now |= Bit(s);
        }

        if (!active) {
            if (st.pending) MAGIC_DEBUG_LOG("[Input] QuickBind: pending slot(s) dropped (mask={:#x})", st.pending);
            st.pending = 0;
            st.wasDown = now;
            return 0;
        }

        const Frame f{n, cache, down, now};
        const std::uint64_t rising = now & ~st.wasDown;
        const float step = dt > 0.f ? dt : 0.f;
        std::uint64_t assign = 0;

        // 1. Pending slots: a longer hotkey completed, the keys were released, or the window ended.
        for (int s = 0; s < n; ++s) {
            if (!(st.pending & Bit(s))) continue;
            auto& secs = st.pendingSecs[static_cast<std::size_t>(s)];
            if (const int t = f.LongerHeld(s); t >= 0) {
                st.pending &= ~Bit(s);
                MAGIC_DEBUG_LOG("[Input] QuickBind: slot={} SUPPRESSED by slot={} (longer combo)", s, t);
                continue;
            }
            secs += step;
            const bool released = !(now & Bit(s));
            if (!released && secs < kMagicMenuHoldSec) continue;
            st.pending &= ~Bit(s);
            assign |= Bit(s);
            MAGIC_DEBUG_LOG("[Input] QuickBind: slot={} assigned after {:.2f}s ({})", s, secs,
                            released ? "released" : "window");
        }

        // 2. Slots completed this frame: the longest wins; a hotkey that a longer one contains waits for it.
        for (int s = 0; s < n; ++s) {
            if (!(rising & Bit(s))) continue;
            if (const int t = f.LongerHeld(s); t >= 0) {
                MAGIC_DEBUG_LOG("[Input] QuickBind: slot={} SUPPRESSED by slot={} (longer combo)", s, t);
                continue;
            }
            if (const int t = f.IdenticalLowerRising(s, rising); t >= 0) {
                MAGIC_DEBUG_LOG("[Input] QuickBind: slot={} SUPPRESSED by slot={} (identical combo)", s, t);
                continue;
            }
            if (const int t = f.LongerExists(s); t >= 0) {
                st.pending |= Bit(s);
                st.pendingSecs[static_cast<std::size_t>(s)] = 0.f;
                MAGIC_DEBUG_LOG("[Input] QuickBind: slot={} waiting for a longer combo (slot={})", s, t);
                continue;
            }
            assign |= Bit(s);
        }

        st.wasDown = now;
        return assign;
    }

    void ResetQuickBind(QuickBindState& st) { st = {}; }
}
