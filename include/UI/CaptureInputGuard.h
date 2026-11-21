#pragma once

// Hotkey capture input guard (spec 009, contracts/capture-input-guard.md): while a capture waits in the SKSE menu, and
// until its input is released, the menu gets no input events (MenuInputShieldHook) and its ImGui keys are claimed.

namespace IntegratedMagic::CaptureGuard {

    // Longest wait after the capture ended for every input to be released (R3).
    inline constexpr float kCaptureGuardReleaseCapSec = 3.0f;
    // A waiting capture whose page was not drawn for this long is cancelled: the menu closed, or another section is
    // shown (R6).
    inline constexpr float kCapturePageTimeoutSec = 0.25f;

    // DrawSettings stamps every draw (R6).
    void NoteCapturePageDrawn();

    // True while the guard waits for releases, so the caller reads the held state only then.
    [[nodiscard]] bool CaptureGuardWantsHeldState();

    // Once per frame (contract §3, §4): turns the guard on, keeps it, or restores the menu's state.
    void UpdateCaptureInputGuard(bool captureWaiting, bool anyInputHeld);

    // True while the outer input hook must hand the SKSE menu an empty event list: a capture waits or the guard waits
    // for releases, and the plugin's page is on screen (research R12).
    [[nodiscard]] bool ShouldHideMenuInput();
}
