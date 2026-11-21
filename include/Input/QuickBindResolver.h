#pragma once
#include <array>
#include <cstdint>

#include "Config/InputConstants.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/SlotEdgeStore.h"

namespace Input {

    // Quick bind in the Magic Menu: which slots receive the hovered entry (spec 008, research R13).
    struct QuickBindState {
        // Slots whose hotkey was fully held last frame.
        std::uint64_t wasDown{0};
        // Slots whose hotkey completed while a longer hotkey that contains it can still complete.
        std::uint64_t pending{0};
        std::array<float, kInputMaxSlots> pendingSecs{};
    };

}

namespace Input::detail {

    // The slots to assign this frame (contracts/magic-menu-hold.md §6):
    // - among hotkeys completed together, the longest wins, and the lower slot wins between identical ones;
    // - a hotkey contained in a longer one waits up to kMagicMenuHoldSec, or its release, for the longer one.
    // With `active` false (outside the Magic Menu, or the wheel open), nothing is assigned and pending slots drop.
    [[nodiscard]] std::uint64_t ResolveQuickBind(QuickBindState& st, bool active, float dt, int slotCount,
                                                 const HotkeyCacheStore& cache, const KeyStateStore& keys,
                                                 const SlotEdgeStore& slots);

    void ResetQuickBind(QuickBindState& st);
}
