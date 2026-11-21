#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "Config/InputConstants.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/SlotEdgeStore.h"
#include "PCH.h"
#include "Shared/ReplayTypes.h"

namespace Input {

    // Fate of a code's latest press in the Magic Menu (spec 008, data-model): None = not tracked or its release was
    // sent; Held = its press is in the queue; Handing = its press is in the outbox, not sent yet; Passed = its press
    // reached the menu; Consumed = its hotkey completed, so its held repeats and release are dropped.
    enum class MenuFate : std::uint8_t { None = 0, Held, Handing, Passed, Consumed };

    struct MenuHeldEvent {
        RetainedEvent ev{};
        int code{-1};
        bool gp{false};
        bool isPress{false};
        float ageSecs{0.f};
    };

    struct MagicMenuHoldStore {
        // Held back, in arrival order.
        std::vector<MenuHeldEvent> queue;
        // Handed to the menu, one sent per frame.
        std::deque<MenuHeldEvent> outbox;
        std::array<MenuFate, kMaxCode> kbFate{};
        std::array<MenuFate, kMaxCode> gpFate{};
    };

    // Menu fate by device family (keyboard and mouse share kbFate). Codes out of range map to a dummy entry.
    [[nodiscard]] inline MenuFate& MenuFateOf(MagicMenuHoldStore& h, bool gp, int code) noexcept {
        static MenuFate s_dummy{MenuFate::None};
        if (code < 0 || code >= kMaxCode) return s_dummy = MenuFate::None;
        return gp ? h.gpFate[static_cast<std::size_t>(code)] : h.kbFate[static_cast<std::size_t>(code)];
    }

}

namespace Input::detail {

    // Owner of a combo in the Magic Menu hold: a slot index >= 0, this value for the wheel, or -1 for none.
    inline constexpr int kMenuHoldWheelOwner = -2;

    // Classifies this frame's button events while the Magic Menu is open (contracts/magic-menu-hold.md §2).
    void FilterMagicMenuEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots,
                               const HotkeyCacheStore& cache, MagicMenuHoldStore& hold);

    // Consumes completed hotkeys, ages the queue and hands due events to the outbox (§3).
    void TickMagicMenuHold(float dt, const KeyStateStore& keys, const SlotEdgeStore& slots,
                           const HotkeyCacheStore& cache, MagicMenuHoldStore& hold);

    // The next event to send to the menu, one per frame (§3, send).
    [[nodiscard]] std::optional<RetainedEvent> PopMagicMenuOutbox(MagicMenuHoldStore& hold);

    // Drops everything queued and resets the fates (§4).
    void ClearMagicMenuHold(MagicMenuHoldStore& hold, const char* reason);
}
