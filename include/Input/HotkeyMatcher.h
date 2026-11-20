#pragma once
#include <algorithm>
#include <array>
#include <ranges>

#include "Config/InputConstants.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/SlotEdgeStore.h"

namespace Input::detail {

    [[nodiscard]] inline bool AnyEnabled(const std::array<int, 3>& a) {
        return (a[0] != -1) || (a[1] != -1) || (a[2] != -1);
    }

    [[nodiscard]] inline bool ComboContains(const std::array<int, 3>& combo, int code) {
        return std::ranges::find(combo, code) != combo.end();
    }

    template <class DownArr>
    [[nodiscard]] bool ComboDown(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) return false;
        return std::ranges::all_of(combo, [&](int code) {
            if (code == -1) return true;
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    template <class DownArr>
    [[nodiscard]] bool AnyComboKeyDown(const std::array<int, 3>& combo, const DownArr& down) {
        return std::ranges::any_of(combo, [&](int code) {
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    // First held code that breaks exclusivity for `combo` (not in it, not an allowed extra), or -1 if none.
    template <class DownArr, class AllowedFn>
    [[nodiscard]] int FindExtraHeldCode(const std::array<int, 3>& combo, const DownArr& down,
                                        AllowedFn isAllowedExtra) {
        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) continue;
            if (ComboContains(combo, code)) continue;
            if (isAllowedExtra(code)) continue;
            return code;
        }
        return -1;
    }

    template <class DownArr, class AllowedFn>
    [[nodiscard]] bool ComboExclusiveNow(const std::array<int, 3>& combo, const DownArr& down,
                                         AllowedFn isAllowedExtra) {
        return AnyEnabled(combo) && FindExtraHeldCode(combo, down, isAllowedExtra) < 0;
    }

    void LoadHotkeyCache_FromConfig(HotkeyCacheStore& cache, SlotEdgeStore& slots);
    void LoadModifierBinding_FromConfig(int& kbCode, int& gpCode);
    bool ConsumeHotkeyReloadRequest();

    [[nodiscard]] bool SlotComboDown(int slot, const HotkeyCacheStore& cache, const KeyStateStore& keys,
                                     const SlotEdgeStore& slots);
}