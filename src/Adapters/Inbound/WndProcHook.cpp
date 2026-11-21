#include "Adapters/Inbound/WndProcHook.h"

#include <imgui.h>

#include "Adapters/Inbound/HookContext.h"
#include "Application/HudController.h"
#include "Application/InputController.h"
#include "Shared/InputConstants.h"
#include "PCH.h"

namespace IntegratedMagic::Inbound::WndProcHook {
    namespace {
        WNDPROC g_originalWndProc{nullptr};

        LRESULT Thunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            if (HookContext::g_renderInitialized.load()) {
                ImGui::SetCurrentContext(HookContext::g_imguiContext);

                if (auto& input = Application::InputController::Get(); input.IsCaptureModeActive()) {
                    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
                        const UINT sc = (lParam >> 16) & 0x7F;
                        // Bit 30: the key was already down, so this is an auto-repeat of a key held from before the
                        // capture (e.g. Enter/Space used on "Cap"); it is not a new press (spec 009, R4).
                        if (const bool repeat = (lParam & (LPARAM{1} << 30)) != 0; !repeat) {
                            if (sc == static_cast<UINT>(kDIK_Escape)) {
                                // Escape is never a hotkey value: it cancels the capture (spec 009, FR-011).
                                input.CancelHotkeyCapture();
                                input.SetCaptureModeActive(false);
                                MAGIC_DEBUG_LOG("[UI] Capture: Escape -> cancelled");
                            } else if (sc > 0 && sc < static_cast<UINT>(kMouseButtonBase)) {
                                input.InjectCapturedScancode(static_cast<int>(sc));
                            }
                        }
                    } else if (uMsg == WM_LBUTTONDOWN) {
                        Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + 0);
                    } else if (uMsg == WM_RBUTTONDOWN) {
                        Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + 1);
                    } else if (uMsg == WM_MBUTTONDOWN) {
                        Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + 2);
                    }
                }

                if (!Application::InputController::Get().IsCaptureModeActive()) {
                    Application::HudController::Get().OnWindowMessage(hWnd, uMsg, wParam, lParam);
                }
            }
            return g_originalWndProc(hWnd, uMsg, wParam, lParam);
        }
    }

    void Install(HWND hwnd) {
        g_originalWndProc =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Thunk)));
        if (!g_originalWndProc) {
            spdlog::error("[Hooks] WndProcHook::Install: SetWindowLongPtrA failed");
        }
    }
}