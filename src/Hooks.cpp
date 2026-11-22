#include "Hooks.h"

#include "Adapters/Inbound/D3DInitHook.h"
#include "Adapters/Inbound/DXGIPresentHook.h"
#include "Adapters/Inbound/MagicCasterInterruptHook.h"
#include "Adapters/Inbound/MagicCasterStartCastHook.h"
#include "Adapters/Inbound/PlayerAnimGraphHook.h"
#include "Adapters/Inbound/PollInputDevicesHook.h"
#include "PCH.h"

namespace IntegratedMagic::Hooks {
    void Install_Hooks() {
        auto& trampoline = SKSE::GetTrampoline();
        if (trampoline.empty()) {
            // SKSE::Init only reserves when SKSE exposes its trampoline interface.
            spdlog::warn(
                "[Hooks] SKSE trampoline interface unavailable or reservation empty; allocating {} B near the game "
                "module",
                kHookSpaceBytes);
            trampoline.create(kHookSpaceBytes);
        }

        // Breadcrumbs (debug build): the last one in the log names the hook a start-up fault happened in (spec 010).
        MAGIC_DEBUG_LOG("[Hooks] installing PollInputDevicesHook");
        Inbound::PollInputDevicesHook::Install();
        MAGIC_DEBUG_LOG("[Hooks] installing PlayerAnimGraphHook");
        Inbound::PlayerAnimGraphHook::Install();
        MAGIC_DEBUG_LOG("[Hooks] installing D3DInitHook");
        Inbound::D3DInitHook::Install();
        MAGIC_DEBUG_LOG("[Hooks] installing DXGIPresentHook");
        Inbound::DXGIPresentHook::Install();
        MAGIC_DEBUG_LOG("[Hooks] installing MagicCasterInterruptHook");
        Inbound::MagicCasterInterruptHook::Install();
        MAGIC_DEBUG_LOG("[Hooks] installing MagicCasterStartCastHook");
        Inbound::MagicCasterStartCastHook::Install();

        const auto used = trampoline.allocated_size();
        const auto cap = trampoline.capacity();
        const double pct = cap ? 100.0 * static_cast<double>(used) / static_cast<double>(cap) : 0.0;
        spdlog::info("[Hooks] hook space: {} / {} B used ({:.0f}%), SKSE trampoline interface: {}", used, cap, pct,
                     SKSE::GetTrampolineInterface() ? "yes" : "no");
        if (used * 4 > cap * 3) {
            spdlog::warn("[Hooks] hook space usage above 75%: {} / {} B - raise kHookSpaceBytes", used, cap);
        }
    }
}
