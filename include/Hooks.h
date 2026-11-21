#pragma once

#include <cstddef>

namespace IntegratedMagic::Hooks {
    // Trampoline bytes consumed by one 5-byte call/jump patch (Hook::stl::write_call<T, 5> / write_jmp).
    inline constexpr std::size_t kCallHookBytes = 14;

    // Hooks that consume trampoline space: PollInputDevicesHook, D3DInitHook and DXGIPresentHook (Install_Hooks), and
    // MenuInputShieldHook (installed on the first hotkey capture, spec 009).
    inline constexpr std::size_t kCallHookCount = 4;

    // Reserved once in SKSEPluginLoad via SKSE::Init(..., {.trampoline = true, .trampolineSize = kHookSpaceBytes}).
    inline constexpr std::size_t kHookSpaceBytes = 128;

    static_assert(kCallHookCount * kCallHookBytes * 4 <= kHookSpaceBytes * 3,
                  "hook space usage must stay <= 75% of kHookSpaceBytes; raise kHookSpaceBytes");

    void Install_Hooks();
}
