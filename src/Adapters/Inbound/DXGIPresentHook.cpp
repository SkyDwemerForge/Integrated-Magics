#include "Adapters/Inbound/DXGIPresentHook.h"

#include <Windows.h>
#include <Xinput.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

#include "Adapters/Inbound/HookContext.h"
#include "Adapters/Inbound/HoveredForm.h"
#include "Adapters/Inbound/MenuInputShieldHook.h"
#include "Application/HudController.h"
#include "Application/InputController.h"
#include "Shared/CaptureState.h"
#include "Shared/InputConstants.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace IntegratedMagic::Inbound::DXGIPresentHook {
    namespace {
        std::pair<float, float> QueryBackbufferSize() {
            ID3D11RenderTargetView* rtv = nullptr;
            HookContext::g_deviceContext->OMGetRenderTargets(1, &rtv, nullptr);
            if (!rtv) {
                spdlog::warn("[HUD] No RTV bound");
                return {0.f, 0.f};
            }

            float w = 0.f, h = 0.f;
            ID3D11Resource* res = nullptr;
            rtv->GetResource(&res);
            if (res) {
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
                    D3D11_TEXTURE2D_DESC desc{};
                    tex->GetDesc(&desc);
                    w = static_cast<float>(desc.Width);
                    h = static_cast<float>(desc.Height);
                    tex->Release();
                }
                res->Release();
            }
            rtv->Release();
            return {w, h};
        }

        constexpr int kMouseButtonCount = 5;

        // Gamepad buttons held now, one bit per plugin index: XInput buttons as 0..13, triggers as 14 and 15.
        // 0 when no controller answers.
        std::uint32_t ReadGamepadMask() {
            static constexpr std::pair<WORD, int> kMap[] = {
                {XINPUT_GAMEPAD_DPAD_UP, 0},      {XINPUT_GAMEPAD_DPAD_DOWN, 1},
                {XINPUT_GAMEPAD_DPAD_LEFT, 2},    {XINPUT_GAMEPAD_DPAD_RIGHT, 3},
                {XINPUT_GAMEPAD_START, 4},        {XINPUT_GAMEPAD_BACK, 5},
                {XINPUT_GAMEPAD_LEFT_THUMB, 6},   {XINPUT_GAMEPAD_RIGHT_THUMB, 7},
                {XINPUT_GAMEPAD_LEFT_SHOULDER, 8},{XINPUT_GAMEPAD_RIGHT_SHOULDER, 9},
                {XINPUT_GAMEPAD_A, 10},           {XINPUT_GAMEPAD_B, 11},
                {XINPUT_GAMEPAD_X, 12},           {XINPUT_GAMEPAD_Y, 13},
            };
            XINPUT_STATE state{};
            if (XInputGetState(0, &state) != ERROR_SUCCESS) return 0u;
            std::uint32_t mask = 0u;
            for (const auto& [button, idx] : kMap)
                if (state.Gamepad.wButtons & button) mask |= (1u << idx);
            if (state.Gamepad.bLeftTrigger > 64) mask |= (1u << 14);
            if (state.Gamepad.bRightTrigger > 64) mask |= (1u << 15);
            return mask;
        }

        void ReadMouseHeld(std::array<bool, kMouseButtonCount>& out) {
            constexpr int kMouseVKs[kMouseButtonCount] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            for (int i = 0; i < kMouseButtonCount; ++i)
                out[static_cast<std::size_t>(i)] = (GetAsyncKeyState(kMouseVKs[i]) & 0x8000) != 0;
        }

        constexpr int kHeldGamepadBase = 1000;

        // Virtual keys that are undefined or reserved, or that Windows maps from the gamepad (0xC3..0xDA, covered by
        // XInput). Some drivers report them as held all the time: in game, VK 0x07 stayed "down" for seconds, which
        // kept the guard waiting until its cap.
        [[nodiscard]] bool IsIgnoredVirtualKey(int vk) {
            return vk == 0x07 || vk == 0x0A || vk == 0x0B || vk == 0x0E || vk == 0x0F || (vk >= 0x3A && vk <= 0x40) ||
                   vk == 0x5E || (vk >= 0x88 && vk <= 0x8F) || (vk >= 0x97 && vk <= 0x9F) || vk == 0xB8 || vk == 0xB9 ||
                   (vk >= 0xC1 && vk <= 0xDA) || vk == 0xE0 || vk == 0xE8 || vk == 0xFF;
        }

        // The first gamepad button or trigger (kHeldGamepadBase + index), or virtual key held (VKs 1..254 cover the
        // mouse buttons), or -1 when nothing is held.
        int FirstHeldInput(std::uint32_t gpMask) {
            if (gpMask != 0u) return kHeldGamepadBase + std::countr_zero(gpMask);
            for (int vk = 1; vk < 0xFF; ++vk)
                if (!IsIgnoredVirtualKey(vk) && (GetAsyncKeyState(vk) & 0x8000)) return vk;
            return -1;
        }

        // Captures are edge-triggered: the first waiting frame records what is already held and captures nothing, so
        // the input that started the capture (e.g. gamepad A on "Cap") is not taken (spec 009, R4). Devices are read
        // only while a capture waits or the guard waits for releases: XInput is slow with no controller connected.
        void PollCapturedInput() {
            auto& input = Application::InputController::Get();
            const auto& cap = CaptureState::Get();
            const bool waiting = cap.captureRequested.load(std::memory_order_relaxed) ||
                                 cap.captureActive.load(std::memory_order_relaxed);
            const bool guardWaits = Application::HudController::Get().CaptureGuardWantsHeldState();

            static std::uint32_t s_prevGamepad = 0u;
            static std::array<bool, kMouseButtonCount> s_prevMouse{};
            static bool s_wasWaiting = false;

            // The first capture installs the outer input hook: by then SKSE Menu Framework has installed its own, so
            // this one wraps it (spec 009, R12).
            if (waiting) MenuInputShieldHook::InstallOnce();

            std::uint32_t gp = 0u;
            std::array<bool, kMouseButtonCount> mouse{};
            if (waiting || guardWaits) {
                gp = ReadGamepadMask();
                ReadMouseHeld(mouse);
            }

            if (waiting && s_wasWaiting) {
                for (int i = 0; i < kMouseButtonCount; ++i) {
                    const auto s = static_cast<std::size_t>(i);
                    if (mouse[s] && !s_prevMouse[s]) input.InjectCapturedScancode(kMouseButtonBase + i);
                }
                if (const std::uint32_t fresh = gp & ~s_prevGamepad; fresh != 0u)
                    input.InjectCapturedGamepad(std::countr_zero(fresh));
            }
            if (waiting) {
                s_prevGamepad = gp;
                s_prevMouse = mouse;
            }
            s_wasWaiting = waiting;

            const int heldInput = (!waiting && guardWaits) ? FirstHeldInput(gp) : -1;
            // Diagnostic: which input keeps the guard waiting after the capture (logged when it changes).
            static int s_loggedHeld = -1;
            if (heldInput != s_loggedHeld) {
                if (heldInput >= kHeldGamepadBase)
                    MAGIC_DEBUG_LOG("[UI] CaptureGuard: waiting for release of gp:{}", heldInput - kHeldGamepadBase);
                else if (heldInput >= 0)
                    MAGIC_DEBUG_LOG("[UI] CaptureGuard: waiting for release of vk:{:#04x}", heldInput);
                s_loggedHeld = heldInput;
            }
            Application::HudController::Get().UpdateCaptureGuard(waiting, heldInput >= 0);
        }

        struct Impl {
            using FuncType = void (*)(std::uint32_t);
            static inline REL::Relocation<FuncType> func;
            static constexpr auto id = REL::RelocationID(75461, 77246, 0xDBBDD0);
            static constexpr auto offset = REL::VariantOffset(0x9, 0x9, 0x15);

            static void thunk(std::uint32_t a_p1) {
                func(a_p1);

                if (!HookContext::g_renderInitialized.load()) return;

                ImGui::SetCurrentContext(HookContext::g_imguiContext);
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();

                const auto [w, h] = QueryBackbufferSize();
                PollCapturedInput();
                HoveredForm::UpdateCachedState();
                Application::HudController::Get().RenderFrame(w, h);
            }
        };
    }

    void Install() {
        Hook::stl::write_call<Impl>(Impl::id, Impl::offset);
        spdlog::info("[Hooks] DXGIPresentHook installed");
    }
}