#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "Config/InputConstants.h"

namespace Input {

    [[nodiscard]] inline std::uint64_t SteadyNowMs() noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    struct KeyStateStore {
        std::array<std::atomic_bool, kMaxCode> kbDown{};
        std::array<std::atomic_bool, kMaxCode> gpDown{};
        // Last pressed event (down or held) for mouse ids 5..7, steady-clock ms. Main thread only (spec 003, R6).
        std::array<std::uint64_t, 3> mouseNoVkLastPressedMs{};

        [[nodiscard]] bool IsKbDown(int code) const noexcept {
            if (code < 0 || code >= kMaxCode) return false;
            return kbDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool IsGpDown(int code) const noexcept {
            if (code < 0 || code >= kMaxCode) return false;
            return gpDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        }

        void SetKbDown(int code, bool value) noexcept {
            if (code < 0 || code >= kMaxCode) return;
            kbDown[static_cast<std::size_t>(code)].store(value, std::memory_order_relaxed);
        }

        void SetGpDown(int code, bool value) noexcept {
            if (code < 0 || code >= kMaxCode) return;
            gpDown[static_cast<std::size_t>(code)].store(value, std::memory_order_relaxed);
        }
    };

}