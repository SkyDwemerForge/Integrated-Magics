#pragma once

// Outer input hook for hotkey captures in the SKSE menu (spec 009, research R12).
//
// SKSE Menu Framework hooks the same input call site as PollInputDevicesHook and acts on raw input events in its own
// thunk, before ImGui and before its input callbacks. This hook is installed at that call site *after* the framework,
// on the first capture, so it becomes the outermost thunk: while a capture waits (and until its input is released)
// with the plugin's page shown, it hands an empty event list on, as the framework itself does to the game.

namespace IntegratedMagic::Inbound::MenuInputShieldHook {
    // Installs the hook once; later calls do nothing. Call it from a frame where a capture waits.
    void InstallOnce();
}
