#pragma once
#include <array>
#include <cstdint>

#include "Config/InputConstants.h"

namespace Input {

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };
    enum class ClearReason { Success, Timeout, Cancelled };
    // Fate of a button's current press; its held repeats and release follow it (spec 004, research R11).
    enum class PressFate : std::uint8_t { None = 0, HeldBack, Consumed, Passed };

    struct ExclusiveStore {
        std::array<PendingSrc, kInputMaxSlots> pendingSrc{};
        std::array<float, kInputMaxSlots> pendingTimer{};
        std::array<bool, kInputMaxSlots> fullComboSeen{};
        std::array<bool, kInputMaxSlots> prevRawKbDown{};
        std::array<bool, kInputMaxSlots> prevRawGpDown{};
        std::array<bool, kInputMaxSlots> prevAnyKeyDown{};
        std::array<bool, kInputMaxSlots> simWindowActive{};
        std::array<float, kInputMaxSlots> simWindowRemaining{};
        std::array<bool, kInputMaxSlots> isKbMultiKey{};
        std::array<bool, kInputMaxSlots> isGpMultiKey{};
        std::array<bool, kInputMaxSlots> filterWindowActive{};
        std::array<float, kInputMaxSlots> filterWindowTimer{};
        std::array<bool, kInputMaxSlots> deactivatedThisPress{};

        // Lost an overlap resolution; cannot activate until its combo is released (spec 004, R4).
        std::array<bool, kInputMaxSlots> suppressedUntilRelease{};
        // Time since the slot's current filter window opened; drives the contested/chord hold caps (R3b, R3c).
        std::array<float, kInputMaxSlots> heldBackSecs{};
        // The "window extended" line was already logged for the current window.
        std::array<bool, kInputMaxSlots> contestedLogged{};
        // Press fate per keyboard/mouse code and per gamepad index (R11).
        std::array<PressFate, kMaxCode> kbFate{};
        std::array<PressFate, kMaxCode> gpFate{};
        // Owner slot of a HeldBack press, so its held repeats and release join the same retained list; -1 otherwise.
        std::array<int, kMaxCode> kbOwner{};
        std::array<int, kMaxCode> gpOwner{};
    };

    // Press-fate accessors by device kind (keyboard and mouse share kbFate). Codes out of range map to a dummy slot.
    [[nodiscard]] inline PressFate& FateOf(ExclusiveStore& excl, bool gamepad, int code) noexcept {
        static PressFate s_dummy{PressFate::None};
        if (code < 0 || code >= kMaxCode) return s_dummy = PressFate::None;
        return gamepad ? excl.gpFate[static_cast<std::size_t>(code)] : excl.kbFate[static_cast<std::size_t>(code)];
    }

    [[nodiscard]] inline int& OwnerOf(ExclusiveStore& excl, bool gamepad, int code) noexcept {
        static int s_dummy{-1};
        if (code < 0 || code >= kMaxCode) return s_dummy = -1;
        return gamepad ? excl.gpOwner[static_cast<std::size_t>(code)] : excl.kbOwner[static_cast<std::size_t>(code)];
    }

}
