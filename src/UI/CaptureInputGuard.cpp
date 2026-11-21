#include "UI/CaptureInputGuard.h"

#include <algorithm>
#include <chrono>

#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "Shared/CaptureState.h"

namespace IntegratedMagic::CaptureGuard {

    namespace {

        using Clock = std::chrono::steady_clock;

        // Owner the guard gives every ImGui key while on. ImGui navigation and widgets only react to keys with no
        // owner (or owned by themselves), which covers input ImGui reads by itself, such as its backend's gamepad
        // polling (spec 009, R2b). Any id other than ImGuiKeyOwner_Any / ImGuiKeyOwner_NoOwner works.
        constexpr ImGuiMCP::ImGuiID kGuardOwnerId = 0x494D4347u;  // "IMCG"

        struct GuardState {
            bool on{false};
            bool savedHotkeyEnabled{false};
            bool claimLogged{false};
            bool pageEverDrawn{false};
            int hiddenFrames{0};
            float releaseWaitSecs{0.f};
            Clock::time_point onSince{};
            Clock::time_point lastUpdate{};
            Clock::time_point lastPageDraw{};
        };

        GuardState g_guard{};

        [[nodiscard]] float SecondsBetween(Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<float>(b - a).count();
        }

        [[nodiscard]] bool CaptureWaiting() {
            const auto& cap = CaptureState::Get();
            return cap.captureRequested.load(std::memory_order_relaxed) ||
                   cap.captureActive.load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool PageShownRecently(Clock::time_point now) {
            return g_guard.pageEverDrawn && SecondsBetween(g_guard.lastPageDraw, now) <= kCapturePageTimeoutSec;
        }

        using SetKeyOwnerFn = void (*)(ImGuiMCP::ImGuiKey, ImGuiMCP::ImGuiID, ImGuiMCP::ImGuiInputFlags);

        // The framework's igSetKeyOwner, or nullptr when the framework or the export is missing. ImGuiMCP::SetKeyOwner
        // is not used because it calls the export without a null check.
        SetKeyOwnerFn FrameworkSetKeyOwner() {
            if (!GetMenuFrameworkModule()) return nullptr;
            static const auto func = GetMenuFrameworkFunction<SetKeyOwnerFn>("igSetKeyOwner");
            return func;
        }

        // Claims every named key (keyboard, gamepad, mouse buttons and wheel) for this frame and the next NewFrame.
        // LockUntilRelease keeps a held key owned until it is released, even after the guard ends (FR-002).
        void ClaimKeys() {
            const auto setOwner = FrameworkSetKeyOwner();
            if (!setOwner) return;
            for (int k = ImGuiMCP::ImGuiKey_NamedKey_BEGIN; k < ImGuiMCP::ImGuiKey_NamedKey_END; ++k)
                setOwner(static_cast<ImGuiMCP::ImGuiKey>(k), kGuardOwnerId, ImGuiMCP::ImGuiInputFlags_LockUntilRelease);
            if (!g_guard.claimLogged) {
                g_guard.claimLogged = true;
                MAGIC_DEBUG_LOG("[UI] CaptureGuard: {} ImGui keys claimed",
                                ImGuiMCP::ImGuiKey_NamedKey_END - ImGuiMCP::ImGuiKey_NamedKey_BEGIN);
            }
        }

        void TurnOn(Clock::time_point now) {
            if (!GetMenuFrameworkModule()) return;
            g_guard.savedHotkeyEnabled = SKSEMenuFramework::IsHotkeyEnabled();
            SKSEMenuFramework::SetHotkeyEnabled(false);
            g_guard.on = true;
            g_guard.releaseWaitSecs = 0.f;
            g_guard.claimLogged = false;
            g_guard.hiddenFrames = 0;
            g_guard.onSince = now;
            MAGIC_DEBUG_LOG("[UI] CaptureGuard: on (menu input hidden, keys claimed, hotkey off)");
            ClaimKeys();
        }

        void TurnOff(Clock::time_point now, [[maybe_unused]] const char* why) {
            // Nothing to restore for the keys: once no longer claimed, an unheld key loses its owner at the next
            // NewFrame. The outer hook stops hiding the input as soon as `on` is false.
            SKSEMenuFramework::SetHotkeyEnabled(g_guard.savedHotkeyEnabled);
            MAGIC_DEBUG_LOG("[UI] CaptureGuard: off after {:.2f}s ({}), input frames hidden from the menu={}",
                            SecondsBetween(g_guard.onSince, now), why, g_guard.hiddenFrames);
            g_guard.on = false;
        }
    }

    void NoteCapturePageDrawn() {
        g_guard.lastPageDraw = Clock::now();
        g_guard.pageEverDrawn = true;
    }

    bool CaptureGuardWantsHeldState() { return g_guard.on; }

    bool ShouldHideMenuInput() {
        // Only while the plugin's page is on screen: once the SKSE menu closes, the game gets its input back at once,
        // even if the guard still waits for a release (R12).
        if (!(g_guard.on || CaptureWaiting()) || !PageShownRecently(Clock::now())) return false;
        ++g_guard.hiddenFrames;
        return true;
    }

    void UpdateCaptureInputGuard(bool captureWaiting, bool anyInputHeld) {
        const auto now = Clock::now();
        const float dt = (g_guard.lastUpdate == Clock::time_point{})
                             ? 0.f
                             : std::clamp(SecondsBetween(g_guard.lastUpdate, now), 0.f, 0.25f);
        g_guard.lastUpdate = now;

        // A capture whose page is not shown any more is cancelled: the menu closed or another section is open (R6).
        if (captureWaiting && !PageShownRecently(now)) {
            auto& cap = CaptureState::Get();
            cap.Cancel();
            cap.captureActive.store(false, std::memory_order_relaxed);
            MAGIC_DEBUG_LOG("[UI] Capture: page not shown -> cancelled");
            captureWaiting = false;
        }

        if (captureWaiting) {
            if (!g_guard.on)
                TurnOn(now);
            else
                ClaimKeys();
            g_guard.releaseWaitSecs = 0.f;
            return;
        }

        if (!g_guard.on) return;

        // After the capture: keep the guard until everything is released, for at most the cap (R3).
        g_guard.releaseWaitSecs += dt;
        if (!anyInputHeld) {
            TurnOff(now, "released");
        } else if (g_guard.releaseWaitSecs >= kCaptureGuardReleaseCapSec) {
            TurnOff(now, "cap");
        } else {
            ClaimKeys();
        }
    }
}
