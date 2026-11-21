#include "Adapters/Inbound/MenuInputShieldHook.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include "Application/HudController.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace IntegratedMagic::Inbound::MenuInputShieldHook {
    namespace {
        // The same call site as PollInputDevicesHook (CLAUDE.md, Hooks table).
        const REL::RelocationID kCallSiteId{67315, 68617, 0xC519E0};
        const REL::VariantOffset kCallSiteOffset{0x7B, 0x7B, 0x81};

        struct Impl {
            using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
            static inline std::uintptr_t func{0};

            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
                if (func == 0) return;
                if (a_events && Application::HudController::Get().ShouldHideMenuInput()) {
                    // The framework, the plugin's inner hook and the game all see no events this frame. The capture
                    // itself reads XInput and window messages, so it is not affected.
                    RE::InputEvent* const empty[2]{nullptr, nullptr};
                    reinterpret_cast<Fn*>(func)(a_dispatcher, empty);
                    return;
                }
                reinterpret_cast<Fn*>(func)(a_dispatcher, a_events);
            }
        };

        // Diagnostic: the module the call site pointed to before this hook (the trampoline stub is
        // `jmp [rip+0]` followed by the absolute target).
        std::string DescribeCallTarget(std::uintptr_t callSite) {
            const auto* code = reinterpret_cast<const std::uint8_t*>(callSite);
            if (code[0] != 0xE8) return "not a call";
            std::int32_t rel = 0;
            std::memcpy(&rel, code + 1, sizeof(rel));
            auto target = callSite + 5 + static_cast<std::intptr_t>(rel);
            const auto* stub = reinterpret_cast<const std::uint8_t*>(target);
            if (stub[0] == 0xFF && stub[1] == 0x25 && stub[2] == 0 && stub[3] == 0 && stub[4] == 0 && stub[5] == 0)
                std::memcpy(&target, stub + 6, sizeof(target));
            HMODULE mod = nullptr;
            constexpr DWORD kFlags =
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
            if (!GetModuleHandleExW(kFlags, reinterpret_cast<LPCWSTR>(target), &mod) || !mod) return "unknown";
            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(mod, path, MAX_PATH) == 0) return "unknown";
            return std::filesystem::path(path).filename().string();
        }
    }

    void InstallOnce() {
        static bool s_installed = false;
        if (s_installed) return;
        s_installed = true;
        const auto callSite = kCallSiteId.address() + kCallSiteOffset.offset();
        const auto previous = DescribeCallTarget(callSite);
        Hook::stl::write_call<Impl>(kCallSiteId, kCallSiteOffset);
        spdlog::info("[Hooks] MenuInputShieldHook installed (outer, previous target in {})", previous);
    }
}
