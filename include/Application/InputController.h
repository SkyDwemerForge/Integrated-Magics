#pragma once
#include <atomic>
#include <optional>

#include "Input/ExclusiveStore.h"
#include "Input/HandButtonStore.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/ReplaySystem.h"
#include "Input/SlotEdgeStore.h"
#include "PCH.h"
#include "Shared/CaptureState.h"
#include "Shared/StateExitResult.h"

namespace Application {

    class InputController {
    public:
        static InputController& Get();

        void ProcessAndFilter(RE::InputEvent** a_evns);
        void OnConfigChanged();
        void ResetInputState();

        [[nodiscard]] std::optional<int> ConsumePressedSlot();
        [[nodiscard]] std::optional<int> ConsumeReleasedSlot();
        [[nodiscard]] std::optional<IntegratedMagic::StateExitResult> ConsumeForceExit();

        // Hand-button gate (spec 005). A fresh attack/block press asked to end the session (R6).
        [[nodiscard]] bool ConsumeHandCancelRequest() noexcept;
        struct TakenHandButtons {
            std::optional<float> left;
            std::optional<float> right;
        };
        // At the plugin's first press: the held attack/block buttons the game sees, now kept until released (R5).
        [[nodiscard]] TakenHandButtons TakeHeldHandButtons();

        [[nodiscard]] std::optional<int> GetDownSlotForSelection() const;
        [[nodiscard]] bool IsSlotHotkeyDown(int slot) const;
        [[nodiscard]] bool IsModifierHeld();
        [[nodiscard]] bool ConsumeHudToggle() const;

        void RequestHotkeyCapture();
        void CancelHotkeyCapture();
        [[nodiscard]] int PollCapturedHotkey();

        void SetCaptureModeActive(bool active);
        [[nodiscard]] bool IsCaptureModeActive() const;
        void InjectCapturedScancode(int scancode);
        void InjectCapturedGamepad(int buttonIndex);

        [[nodiscard]] Input::KeyStateStore& Keys() noexcept { return m_keys; }
        [[nodiscard]] Input::SlotEdgeStore& Slots() noexcept { return m_slots; }
        [[nodiscard]] Input::ExclusiveStore& Exclusive() noexcept { return m_exclusive; }
        [[nodiscard]] Input::HotkeyCacheStore& Hotkeys() noexcept { return m_hotkeys; }
        [[nodiscard]] float GetDeltaTime() const noexcept { return m_lastDt; }
        [[nodiscard]] bool IsInputBlocked() const;

        void SetSlotDeactivatedThisPress(int slot) noexcept {
            if (slot >= 0 && slot < kInputMaxSlots)
                m_exclusive.deactivatedThisPress[static_cast<std::size_t>(slot)] = true;
        }

    private:
        InputController() = default;

        bool m_prevBlocked{false};
        bool m_cacheInitialized{false};
        int m_modifierKbCode{-1};
        int m_modifierGpCode{-1};
        Input::KeyStateStore m_keys{};
        Input::SlotEdgeStore m_slots{};
        Input::ExclusiveStore m_exclusive{};
        Input::HotkeyCacheStore m_hotkeys{};
        Input::detail::ReplayArr m_replay{};
        Input::detail::RetainedArr m_retained{};
        Input::detail::DeferredVec m_deferred{};
        std::optional<IntegratedMagic::StateExitResult> m_pendingForceExit{};
        Input::HandButtonStore m_handButtons{};
        Input::HandGateFlags m_handFlags{};
        float m_lastDt{0.f};

        static float CalculateDeltaTime();
        std::optional<int> ConsumeBit(std::atomic<std::uint64_t>& mask) const;
    };

}