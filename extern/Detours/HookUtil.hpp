#pragma once
#include <format>
#include <source_location>

#include "REL/Relocation.h"
#include "SKSE/SKSE.h"
#include "detours.hpp"

namespace Hook::stl {

    namespace detail {
        // Hook space is reserved once in SKSEPluginLoad (see include/Hooks.h); helpers only check and consume it.
        // Fails naming the calling Install() instead of the library's generic "Failed to handle allocation request".
        inline void EnsureHookSpace(std::size_t a_bytes, std::source_location a_loc) {
            auto& t = SKSE::GetTrampoline();
            if (t.empty() || t.free_size() < a_bytes) {
                SKSE::stl::report_and_fail(
                    std::format("Integrated Magic: not enough hook space to install hook at {}: need {} B, free {} B "
                                "of {} B reserved",
                                a_loc.function_name(), a_bytes, t.free_size(), t.capacity()),
                    a_loc);
            }
        }
    }

    template <class T, std::size_t Size = 5>
    void write_call(std::uintptr_t a_src, std::source_location a_loc = std::source_location::current()) {
        detail::EnsureHookSpace(Size == 6 ? 8 : 14, a_loc);
        auto& trampoline = SKSE::GetTrampoline();
        if constexpr (Size == 6) {
            T::func = *reinterpret_cast<uintptr_t*>(trampoline.write_call<6>(a_src, T::thunk));
        } else {
            T::func = trampoline.write_call<Size>(a_src, T::thunk);
        }
    }

    template <class T, std::size_t Size = 5>
    void write_call(REL::VariantID a_varId, REL::VariantOffset a_Offs = VariantOffset(0x0, 0x0, 0x0),
                    std::source_location a_loc = std::source_location::current()) {
        detail::EnsureHookSpace(Size == 6 ? 8 : 14, a_loc);
        auto& trampoline = SKSE::GetTrampoline();
        const auto address = a_varId.address() + a_Offs.offset();
        if constexpr (Size == 6) {
            T::func = *reinterpret_cast<uintptr_t*>(trampoline.write_call<6>(address, T::thunk));
        } else {
            T::func = trampoline.write_call<Size>(address, T::thunk);
        }
    }

    template <class T, std::size_t Size = 5>
    void write_call(REL::RelocationID a_RelId, REL::VariantOffset a_Offs = VariantOffset(0x0, 0x0, 0x0),
                    std::source_location a_loc = std::source_location::current()) {
        detail::EnsureHookSpace(Size == 6 ? 8 : 14, a_loc);
        auto& trampoline = SKSE::GetTrampoline();
        const auto address = a_RelId.address() + a_Offs.offset();
        if constexpr (Size == 6) {
            T::func = *reinterpret_cast<uintptr_t*>(trampoline.write_call<6>(address, T::thunk));
        } else {
            T::func = trampoline.write_call<Size>(address, T::thunk);
        }
    }

    template <class T>
    void write_jmp(std::uintptr_t a_src, std::source_location a_loc = std::source_location::current()) {
        detail::EnsureHookSpace(14, a_loc);
        auto& trampoline = SKSE::GetTrampoline();
        T::func = trampoline.write_branch<5>(a_src, T::thunk);
    }

    template <class T>
    void write_jmp(REL::VariantID a_Id, REL::VariantOffset a_Offs = VariantOffset(0x0, 0x0, 0x0),
                   std::source_location a_loc = std::source_location::current()) {
        detail::EnsureHookSpace(14, a_loc);
        auto& trampoline = SKSE::GetTrampoline();
        T::func = trampoline.write_branch<5>(a_Id.address() + a_Offs.offset(), T::thunk);
    }

    template <class F, size_t index, class T>
    void write_vfunc() {
        REL::Relocation<std::uintptr_t> vtbl{F::VTABLE[index]};
        T::func = vtbl.write_vfunc(T::size, T::thunk);
    }

    template <std::size_t idx, class T>
    void write_vfunc(REL::VariantID id) {
        REL::Relocation<std::uintptr_t> vtbl{id};
        T::func = vtbl.write_vfunc(idx, T::thunk);
    }

    template <class F, class T>
    void write_vfunc() {
        write_vfunc<F, 0, T>();
    }

    template <class T>
    void write_detour(REL::RelocationID a_relId) {
        REL::Relocation<std::uintptr_t> target{a_relId};

        if (!target.address()) {
            SKSE::stl::report_and_fail(
                fmt::format("Invalid target address for detour [{} + 0x{:X}]", a_relId.id(), a_relId.offset()));
        }

        T::func = reinterpret_cast<decltype(T::func)>(target.address());

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (LONG attachResult = DetourAttach(&reinterpret_cast<PVOID&>(T::func), T::thunk); attachResult != NO_ERROR) {
            DetourTransactionAbort();
            SKSE::stl::report_and_fail(fmt::format("Detour Attach Failed [{} + 0x{:X}] - Error: {}", a_relId.id(),
                                                   a_relId.offset(), attachResult));
        }

        LONG error = DetourTransactionCommit();
        if (error != NO_ERROR) {
            SKSE::stl::report_and_fail(
                fmt::format("Detour Write Failed [{} + 0x{:X}] - Error: {}", a_relId.id(), a_relId.offset(), error));
        }
    }

    template <class T>
    void write_detour_replace(REL::RelocationID a_relId) {
        REL::Relocation<std::uintptr_t> target{a_relId};

        if (!target.address()) {
            SKSE::stl::report_and_fail(
                fmt::format("Invalid target address for detour replace [{} + 0x{:X}]", a_relId.id(), a_relId.offset()));
        }

        auto targetFunc = reinterpret_cast<PVOID>(target.address());

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (LONG attachResult = DetourAttach(&targetFunc, T::thunk); attachResult != NO_ERROR) {
            DetourTransactionAbort();
            SKSE::stl::report_and_fail(fmt::format("Detour Replace Attach Failed [{} + 0x{:X}] - Error: {}",
                                                   a_relId.id(), a_relId.offset(), attachResult));
        }

        LONG error = DetourTransactionCommit();
        if (error != NO_ERROR) {
            SKSE::stl::report_and_fail(fmt::format("Detour Replace Write Failed [{} + 0x{:X}] - Error: {}",
                                                   a_relId.id(), a_relId.offset(), error));
        }
    }

    template <std::size_t idx, class T>
    void write_detour_vfunc(void* target) {
        if (!target) {
            SKSE::stl::report_and_fail("Detour vFunc Write Failed - Null target object");
        }

        void** vtable = *static_cast<void***>(target);
        if (!vtable || !vtable[idx]) {
            SKSE::stl::report_and_fail(
                fmt::format("Detour vFunc Write Failed - Invalid vtable or function at index {}", idx));
        }

        T::func = reinterpret_cast<decltype(T::func)>(vtable[idx]);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (LONG attachResult = DetourAttach(&reinterpret_cast<PVOID&>(vtable[idx]), T::thunk);
            attachResult != NO_ERROR) {
            DetourTransactionAbort();
            SKSE::stl::report_and_fail(
                fmt::format("Detour vFunc Attach Failed at index {} - Error: {}", idx, attachResult));
        }

        LONG error = DetourTransactionCommit();
        if (error != NO_ERROR) {
            SKSE::stl::report_and_fail(fmt::format("Detour vFunc Write Failed at index {} - Error: {}", idx, error));
        }
    }
}
