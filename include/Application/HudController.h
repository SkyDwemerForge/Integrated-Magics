#pragma once

namespace Application {

    class HudController {
    public:
        static HudController& Get();

        void OnFrame();
        void InitializeGraphics();
        void RenderFrame(float backbufferW, float backbufferH);
        void OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        // Hotkey capture input guard of the SKSE menu (spec 009), once per frame from the Present hook.
        void UpdateCaptureGuard(bool captureWaiting, bool anyInputHeld);
        // The guard waits for releases, so the caller should read the held state this frame.
        [[nodiscard]] bool CaptureGuardWantsHeldState() const;
        // The outer input hook hands the SKSE menu no events this frame (spec 009, R12).
        [[nodiscard]] bool ShouldHideMenuInput() const;

    private:
        HudController() = default;
    };

}