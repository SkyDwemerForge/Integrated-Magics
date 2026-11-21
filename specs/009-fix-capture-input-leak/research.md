# Research: The SKSE menu must not react to input pressed during a hotkey capture

Facts come from the code on `main` (`c4fc06f`), from the public source of SKSE Menu Framework
(https://github.com/Thiago099/SKSE-Menu-Framework, `main`), and from the exports of the installed
`SKSEMenuFramework.dll` (MO2 mod "SKSE Menu Framework", 2026-04-11).

## R1. Why the SKSE menu sees the captured input

**Facts**:
- **Shared hook site**: SKSE Menu Framework hooks the same call site as the plugin's `PollInputDevicesHook`:
  `src/Renderer.cpp`, `builder->AddCall<ProcessInputQueueHook, 5, 14>(67315, 0x7B, 68617, 0x7B, 0xC519E0, 0x81)`, and
  the plugin's hook uses `RelocationID(67315, 68617)` + `VariantOffset(0x7B, 0x7B, 0x81)`. Each `write_call` wraps the
  previous one, so the outer hook depends on install order.
- **The framework is the outer hook**: while one of its windows is open, its thunk passes `{nullptr}` to the next hook
  (`IsAnyWindowOpen()`, `dummy[] = {nullptr}`) and feeds the events to ImGui. The plugin therefore gets no input
  events while the SKSE menu is open. That is why gamepad captures read XInput directly (`PollGamepadCapture` in
  `src/Adapters/Inbound/DXGIPresentHook.cpp`), and why keys and mouse clicks come from window messages
  (`src/Adapters/Inbound/WndProcHook.cpp`).
- **What the framework does with the gamepad**: `TranslateButtonEvent` (`src/Input.cpp`) maps each gamepad
  `ButtonEvent` to an ImGui gamepad key (`io.AddKeyEvent(ParseKeyFromGamepad(...), button->IsPressed())`), and
  `ImGuiConfigFlags_NavEnableGamepad` is set. So the reactions are ImGui navigation:
  - D-pad moves the focus;
  - A activates the focused control;
  - B cancels or goes back;
  - X opens the window switcher;
  - Y opens the menu or text input;
  - LB/RB tweak values or switch windows.
- **Keyboard and mouse**: they go through the same translation (`io.AddKeyEvent`, `io.AddMouseButtonEvent`), so a
  captured key or mouse button also reaches ImGui navigation and widgets.
- **The plugin's current guard does not reach the menu**: `InputController::ProcessAndFilter` removes button events
  while a capture waits (`if (wantCaptureBefore && !blocked)`). It runs inside the inner hook, after the framework has
  already consumed the events, and it does not run when an input-blocking menu is open.

**Decision**: the fix must act on the framework's ImGui state, not on the plugin's event list.

## R2. How to keep input from the SKSE menu during a capture

> **Outcome of the first in-game test (2026-11-21, R8)**: the flags approach below did not hold. Every capture logged
> `nav flags were re-enabled by the host` about 17 ms after `on`, so the framework rewrites its ImGui config flags
> every frame, before ImGui processes input. The guard now uses **ImGui key ownership** instead (R2b). The text below
> is kept as the record of the first design.

**First decision (replaced by R2b)**: a **capture input guard** in the UI layer (`include/UI/CaptureInputGuard.h`,
`src/UI/CaptureInputGuard.cpp`).
- **While it is on**, it changes the framework's shared ImGui IO (`ImGuiMCP::GetIO()`, export `igGetIO`):
  - clears `ImGuiConfigFlags_NavEnableGamepad` and `ImGuiConfigFlags_NavEnableKeyboard`;
  - sets `ImGuiConfigFlags_NoMouse`;
  - calls `SKSEMenuFramework::SetHotkeyEnabled(false)` (export `SetHotkeyEnabled`), so a captured key that is the
    framework's own open/close hotkey does not close the menu.
- **When it turns off**, it restores exactly the bits it changed, and the hotkey state it found.

**Rationale**:
- **Standard ImGui behavior**: these are documented ImGui flags. With gamepad and keyboard navigation off, none of the
  reactions of R1 can happen, and with `NoMouse` a captured click does not press the control under the cursor.
- **Captured values unchanged**: the capture itself never goes through ImGui. It reads XInput, `GetAsyncKeyState` and
  window messages, so its values are untouched (FR-003).
- **Shared context**: the change affects the whole SKSE menu, other mods' pages included, but only while a capture
  waits and until its input is released. Spec 006 showed this context must be left exactly as found, so the guard
  saves and restores only its own bits.

**Alternatives considered**:
- **The framework's input callback** (`SKSEMenuFramework::AddInputEvent`, export `RegisterInpoutEvent`): its return
  value is undocumented. Neither the framework repository nor the SDK repository has it, and the Nexus page refused
  the fetch. It is kept only as the fallback of R8.
- **ImGui key ownership** (`SetKeyOwner` with `LockUntilRelease`): the header does not wrap it, and it depends on when
  ownership is applied relative to `NewFrame`.
- **Filtering the plugin's event list**: it cannot reach the framework (R1).
- **Hiding the window or making the page `NoInputs`**: gamepad back and switching windows still act on other windows.

## R2b. Key ownership (the shipped design)

**Facts**:
- **Exported**: the installed `SKSEMenuFramework.dll` exports `igSetKeyOwner`, and the header wraps it as
  `ImGuiMCP::SetKeyOwner(ImGuiKey, ImGuiID, ImGuiInputFlags)`.
- **Who honors ownership**: ImGui navigation tests every key with `ImGuiKeyOwner_NoOwner`: activate, cancel, move,
  window switcher and tweak. Widgets test the mouse buttons with their own id. A key owned by another id fails both
  tests.
- **How ownership carries over**: `SetKeyOwner` sets the owner for the current frame and the next `NewFrame`. An
  unheld key drops its owner one `NewFrame` after the last claim. `ImGuiInputFlags_LockUntilRelease` keeps a held key
  owned until it is released.
- **The framework leaves it alone**: it rewrites its config flags each frame, but it does not touch key ownership.

**Decision**:
- **Claim**: while the guard is on, it claims every named ImGui key (`ImGuiKey_NamedKey_BEGIN` to `_END`, so keyboard,
  gamepad and mouse) once per frame, with owner `0x494D4347` and `LockUntilRelease`.
- **Hotkey**: it still disables the framework hotkey.
- **Restore**: nothing is needed for the keys. Once they are no longer claimed, unheld keys lose the owner by
  themselves, and the hotkey state is restored.
- **Flags**: no config flag is touched any more.

**Rationale**:
- **Same effect**: it blocks the same reactions as R2, with no fight against a value the host writes every frame.
- **FR-002 for free**: `LockUntilRelease` keeps a held key owned until its release.

## R12. The outer input hook (shipped after the third in-game test)

**Facts from the third run (2026-11-21, 16:14)**:
- **The callback was called**: `framework input blocked (first: type=0 device=2 id=8192)`, which is gamepad `B`
  (`0x2000`), with 106 and 125 events blocked in two captures.
- **Ownership held**: `owner check FaceRight=0x494d4347 DpadDown=0x494d4347`.
- **The menu still reacted**: so the framework acts on the raw event in its own thunk, before its input callbacks and
  before ImGui. Its `ProcessOpenClose`, for example, runs first in the public source.
- **`vk:0x07` was reported held** for seconds after each capture. VK `0x07` is undefined, and a driver reports it.
  That kept the guard on until its 3 s cap.

**Decision**:
- **`MenuInputShieldHook`**: a second `write_call` at the same site as `PollInputDevicesHook` and the framework
  (`RelocationID(67315, 68617)` + `0x7B`).
  - **When it is installed**: lazily, on the first frame a capture waits. By then the framework has installed its own
    hook, so the new thunk wraps it and is outermost. The install logs the module the call pointed to before.
  - **What it does**: while `CaptureGuard::ShouldHideMenuInput()` holds, it passes `{nullptr}` on. That holds while
    the guard is on or a capture waits, with the plugin's page drawn within 0.25 s.
- **Key ownership stays**: it covers input ImGui reads by itself, such as its backend's gamepad polling.
- **The input callback is removed**: the outer hook supersedes it.
- **The held-state scan** skips undefined virtual keys and the gamepad VK range 0xC3–0xDA, which XInput already
  covers.
- **Trampoline**: `kCallHookCount` becomes 4, which uses 56 of the 128 B.

**Consequences**:
- **`Escape` during a capture**: the framework no longer sees it, so the SKSE menu stays open, and `Escape` only
  cancels the capture.
- **Bounded hiding**: it ends as soon as the guard ends, or once the page is no longer drawn. So the game and the
  menu are never left without input.
- **Coexistence (Constitution V)**: the hook always calls the original, and it hides input only during a capture on
  the plugin's own page.

**Outcome** (fourth run, 2026-11-21, 16:26, on the T027 build): it works, as reported by the user.
- **The install line**: `MenuInputShieldHook installed (outer, previous target in InputManager.dll)`.
  - The outermost hook before this one was the Input Manager mod's (`InputManager.dll`), not the framework's.
  - SKSE Menu Framework's hook is further in, so it got the empty list too. The hook does not depend on which module
    is outermost.
- **Captures**: 12 gamepad captures (`B` ×3, `LT` ×9).
  - Each one: `CaptureGuard: on`, then `waiting for release of gp:<n>`, then `off ... (released)` after 0.35–1.23 s.
  - 21–74 input frames were hidden per capture.
- **The cap**: none of the captures reached it, so the VK `0x07` skip holds.

## R3. How long the guard stays on

**Decision**:
- **On**: from the frame a capture is requested (`CaptureState::captureRequested || captureActive`).
- **Off**: once the capture is no longer waiting, and every gamepad button and trigger, every mouse button and every
  keyboard key is released.
- **Cap**: at most `kCaptureGuardReleaseCapSec` = 3 s after the capture ended, so a stuck reading never leaves the
  menu without navigation.
- **Re-applied every frame**: the guard writes its flags again each frame while on (R8).
- **Updated** once per frame from `DXGIPresentHook` (`PollCapturedInput`), through `HudController`.

**Rationale**:
- **FR-002**: the capture ends on the press. If navigation came back right away, a held D-pad would start repeating
  moves after ImGui's repeat delay, about 0.25 s.
- **Releases are harmless**: ImGui acts on presses, not on releases, so turning the guard off at the release is
  enough.
- **Simple rule**: "everything released" is simpler than tracking each captured code, and it also covers the button
  that started the capture (R4).

## R4. The input that starts the capture (FR-004)

**Facts**:
- **Gamepad**: ImGui gamepad activation happens on the **press** of A. So clicking "Cap" with A starts the capture in
  the same frame A goes down. `PollGamepadCapture` is level-triggered (`if (state.Gamepad.wButtons & mask) return
  idx;`), so it captures the A that is still held.
- **Keyboard**: `Space` or `Enter`, held after activating "Cap", send auto-repeat `WM_KEYDOWN` messages, which
  `WndProcHook` captures.

**Decision**: captures are edge-triggered.
- **Gamepad and mouse**: on the first frame a capture waits, `PollCapturedInput` records the buttons already held and
  captures nothing. After that, it accepts only a button that goes from up to down. A button held when the capture
  started counts only after it is released and pressed again.
- **When XInput is read**: only while a capture waits or the guard is on. `XInputGetState` is slow when no controller
  is connected, so reading it every frame would cost players with no gamepad.
- **Keyboard**: `WndProcHook` ignores a `WM_KEYDOWN` whose previous-state bit (`lParam` bit 30) is set.

## R5. The other device cancels the capture (FR-010)

**Decision**: in `DrawKeyValueCapture` (`src/UI/MENU.cpp`), a polled value from the other device calls
`CancelFieldCapture()` instead of `Request()`. The field is left unchanged. Log
`[UI] Capture: <kb|gp> input during a <gp|kb> capture -> cancelled`. The guard (R3) already keeps that input from the
menu until it is released.

## R6. A capture must end when its page is not shown (FR-006)

**Facts**:
- **No cancel on close**: today `CancelFieldCapture` runs only when the selected slot changes. Closing the SKSE menu,
  or showing another section, leaves the capture waiting, and with the guard of R2 navigation would stay off.
- **Gameplay leak**: a waiting capture also keeps `PollCapturedInput` reading the gamepad during gameplay.

**Decision**:
- **Heartbeat**: `DrawSettings` stamps the time on every draw (`NoteCapturePageDrawn()`).
- **Cancel**: if a capture waits and the page has not been drawn for `kCapturePageTimeoutSec` = 0.25 s, the guard
  cancels it (`CaptureState::Cancel()`, `captureActive = false`). Log `[UI] Capture: page not shown -> cancelled`.
- **Reconcile**: `DrawKeyValueCapture` clears its own `g_fieldCapture` when it finds no capture waiting and nothing
  to poll.

## R7. Escape and the framework's own keys

**Facts**:
- **Escape is captured today**: `WndProcHook` injects any scan code `1..255`, so `Escape` (`1`) is captured into a
  keyboard field. Only `TryHandleCapture`, the game-event path, skips it. That corrects the spec's assumption that
  Escape is never captured.
- **The framework closes on Escape** in `ProcessOpenClose`, before ImGui sees the key. The plugin cannot block that.

**Decision**:
- **Escape is never captured**, on any path. During a capture it cancels the capture, logged as
  `[UI] Capture: Escape -> cancelled`, and the framework closes the menu as usual.
- **The framework's open/close hotkey** is disabled while the guard is on (R2). If the installed version ignores
  `SetHotkeyEnabled`, the quickstart shows it. The menu would then close, and R6 would cancel the capture.

## R8. Checking that the flags hold

**Risk**: the framework might write `io.ConfigFlags` every frame, which would undo the guard.

**Decision**:
- **Re-apply**: the guard writes its bits every frame while on.
- **Log once**: if it finds navigation re-enabled, it logs `[UI] CaptureGuard: nav flags were re-enabled by the
  host`.
- **Fallback**: if the in-game test still shows menu reactions with that line in the log, register an
  `AddInputEvent` callback that returns true for button events while the guard is on, and validate its effect in game.

**Outcome (2026-11-21)**: the risk happened. The line appeared in every capture, and the menu still reacted. The
key-ownership design of R2b replaced the flags before the input callback was tried. It relies on documented ImGui
behavior, while the callback's return value is undocumented. The callback stays as the next fallback if key ownership
also fails in game.

**Second outcome (2026-11-21)**:
- **Ownership did not stop it**: the claim ran (`154 ImGui keys claimed`), and the menu still reacted. The reaction
  most likely comes from the framework's own input handling, before ImGui, in a version newer than the public source.
- **What was added**: the input callback (`AddInputEvent`), returning true for every event while the guard is on,
  plus two diagnostics: the owner check, and which input keeps the guard waiting.
- **What the next log shows**:
  - whether the callback is called (`framework input blocked`, blocked count);
  - whether ownership holds (`owner check`).

## R9. Logs (Constitution VI)

| Prefix | Lines |
|---|---|
| `[UI] CaptureGuard:` | `on (keys claimed, hotkey off)`; `<n> ImGui keys claimed`; `off after <s>s (released\|cap)` |
| `[UI] Capture:` | `<kb\|gp> input during a <gp\|kb> capture -> cancelled`; `Escape -> cancelled`; `page not shown -> cancelled`; `stale field capture cleared` |

## R10. TEST_MATRIX cases (FR-008)

New cases in section 10, in Portuguese:
- **10.18**: each gamepad button and trigger captured, with no menu reaction;
- **10.19**: navigation keys and mouse buttons captured, with no menu reaction;
- **10.20**: clicking "Cap" with A, or with Enter/Space, does not capture that same input;
- **10.21**: input from the other device cancels the capture;
- **10.22**: the menu reacts normally right after a capture, and closing the menu during a capture cancels it.

**Regression**: 10.7 and 10.9 (repeated captures, the menu never fades).

## R11. Validation build

Release DLL, approved by the user (spec Clarifications). The DEBUG DLL still faults in `Hooks::Install_Hooks`.
