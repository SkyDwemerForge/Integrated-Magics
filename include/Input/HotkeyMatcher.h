#pragma once
#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <string>

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

    // Number of bound keys in a combo (entries != -1).
    [[nodiscard]] inline int ComboKeyCount(const std::array<int, 3>& c) {
        return static_cast<int>(std::ranges::count_if(c, [](int code) { return code != -1; }));
    }

    // Number of the combo's bound keys currently held.
    template <class DownArr>
    [[nodiscard]] int ComboHeldCount(const std::array<int, 3>& c, const DownArr& down) {
        return static_cast<int>(std::ranges::count_if(c, [&](int code) {
            return code >= 0 && code < kMaxCode && down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        }));
    }

    // Every bound key of `sub` is in `sup`.
    [[nodiscard]] inline bool ComboContainsAll(const std::array<int, 3>& sub, const std::array<int, 3>& sup) {
        return std::ranges::all_of(sub, [&](int code) { return code == -1 || ComboContains(sup, code); });
    }

    // `a` is fully contained in `b`, and `b` has more keys (spec 004, R4).
    [[nodiscard]] inline bool ComboStrictSubset(const std::array<int, 3>& a, const std::array<int, 3>& b) {
        const int ka = ComboKeyCount(a);
        return ka > 0 && ka < ComboKeyCount(b) && ComboContainsAll(a, b);
    }

    // Same set of bound keys, ignoring order; both non-empty (spec 004, R4/R5).
    [[nodiscard]] inline bool ComboSameKeys(const std::array<int, 3>& a, const std::array<int, 3>& b) {
        const int ka = ComboKeyCount(a);
        return ka > 0 && ka == ComboKeyCount(b) && ComboContainsAll(a, b);
    }

    // Log label for an input code: kb:0x<scan>, mouse:<id> or gp:<index> (spec 003 R7, spec 004 R6).
    [[nodiscard]] inline std::string DescribeHeldCode(int code, bool gamepad) {
        if (code < 0) return "none";
        if (gamepad) return std::format("gp:{}", code);
        if (code >= kMouseButtonBase) return std::format("mouse:{}", code - kMouseButtonBase);
        return std::format("kb:{:#04x}", code);
    }

    void LoadHotkeyCache_FromConfig(HotkeyCacheStore& cache, SlotEdgeStore& slots);
    void LoadModifierBinding_FromConfig(int& kbCode, int& gpCode);
    bool ConsumeHotkeyReloadRequest();

    [[nodiscard]] bool SlotComboDown(int slot, const HotkeyCacheStore& cache, const KeyStateStore& keys,
                                     const SlotEdgeStore& slots);
}