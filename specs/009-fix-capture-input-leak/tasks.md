---

description: "Task list for spec 009: the SKSE menu must not react to input pressed during a hotkey capture"
---

# Tasks: The SKSE menu must not react to input pressed during a hotkey capture

**Input**: design documents in `specs/009-fix-capture-input-leak/`: [plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md),
[contracts/capture-input-guard.md](contracts/capture-input-guard.md), [quickstart.md](quickstart.md).

**Tests**: no automated harness exists (Constitution VI). Validation is in game, through the quickstart and the tasks
marked **(in-game, user)**. They run on the **release** DLL, as approved by the user (spec Clarifications).

**Organization**:
- **Phase 2** builds every change: the guard, edge-triggered captures, the capture outcomes and the wiring. The three
  stories share them.
- **Each story phase** desk-checks its slice of the contract, then validates it in game.
- **Build**: one build (T008) serves every in-game task.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1, US2, US3, see spec.md)

## Conventions for every code task

- Identifiers, comments and log messages are in English. `.clang-format` applies (120 columns, 4-space indent).
- **Logging**: `MAGIC_DEBUG_LOG` with the prefixes `[UI] CaptureGuard:` and `[UI] Capture:` (contract §6).
- **SKSE menu pair rule** (`.claude/CLAUDE.md`, spec 006): in `src/UI/MENU.cpp`, keep every `BeginDisabled` /
  `EndDisabled`, `PushStyleColor` / `PopStyleColor` and `PushID` / `PopID` pair balanced on every path.
- **Shared ImGui context**: the guard changes only `ImGuiConfigFlags_NavEnableGamepad`,
  `ImGuiConfigFlags_NavEnableKeyboard` and `ImGuiConfigFlags_NoMouse`, plus the framework hotkey. It restores each to
  the value it found.
- **Out of scope**: no change in `src/Domain`, `include/Domain`, `src/Input`, `include/Input`, `src/Config`,
  `include/Config`, `src/Persistence` or the INI (spec FR-009).
- **Build**: the scratchpad script
  `C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\build.ps1
  -Preset <debug|release> -LogFile <path>`. If it is gone, run `cmake --preset <p>` then `cmake --build build/<p>`.
  The post-build step copies the DLL to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`. `CMakeLists.txt`
  globs `src/`, so the new `.cpp` needs no CMake change.

---

## Phase 1: Setup

- [X] T001 Create `include/UI/CaptureInputGuard.h`, namespace `IntegratedMagic::CaptureGuard` (data-model), with:
  - **constants**: `inline constexpr float kCaptureGuardReleaseCapSec = 3.0f;` and
    `inline constexpr float kCapturePageTimeoutSec = 0.25f;`, each with a one-line comment (R3, R6);
  - **functions**, each with a one-line comment:
    - `void NoteCapturePageDrawn();` ("`DrawSettings` stamps every draw, R6");
    - `[[nodiscard]] bool CaptureGuardWantsHeldState();` ("true while the guard waits for releases, so the caller
      reads the held state only then");
    - `void UpdateCaptureInputGuard(bool captureWaiting, bool anyInputHeld);` ("once per frame, contract §3–§4").

---

## Phase 2: Foundational (every change, then the build)

**⚠️ CRITICAL**: no story validation can start before T008 builds.

- [X] T002 [P] Create `src/UI/CaptureInputGuard.cpp` (contract §3, §4 path 4, §6; data-model). Includes:
  `UI/CaptureInputGuard.h`, `<chrono>`, `PCH.h`, `SKSEMenuFramework.h`, `Shared/CaptureState.h`.
  - **File-local state** (`namespace {}`):
    - `on`, `savedNavGamepad`, `savedNavKeyboard`, `savedNoMouse`, `savedHotkeyEnabled`, `reEnableLogged` and
      `pageEverDrawn` (`bool`);
    - `releaseWaitSecs` (`float`);
    - `onSince`, `lastUpdate` and `lastPageDraw` (`std::chrono::steady_clock::time_point`).
  - **`FrameworkIO()`**: `nullptr` when `GetMenuFrameworkModule()` is null or the export `igGetIO` is missing.
    Otherwise the result of calling it, with the function pointer cached through
    `GetMenuFrameworkFunction<ImGuiMCP::ImGuiIO* (*)()>("igGetIO")`. Never call `ImGuiMCP::GetIO()` directly, since it
    does not check for null.
  - **`ApplyBits(io)`**: clear `ImGuiMCP::ImGuiConfigFlags_NavEnableGamepad | ImGuiMCP::ImGuiConfigFlags_NavEnableKeyboard`
    and set `ImGuiMCP::ImGuiConfigFlags_NoMouse`. **`BitsApplied(io)`**: true when both nav bits are clear and
    `NoMouse` is set.
  - **`TurnOn()`**:
    1. if `FrameworkIO()` is null, return;
    2. save the three bits from `io->ConfigFlags` and `SKSEMenuFramework::IsHotkeyEnabled()`;
    3. call `ApplyBits`, then `SKSEMenuFramework::SetHotkeyEnabled(false)`;
    4. set `on = true`, `releaseWaitSecs = 0`, `reEnableLogged = false` and `onSince = now`;
    5. log `[UI] CaptureGuard: on (nav off, mouse off, hotkey off)`.
  - **`TurnOff(const char* why)`**:
    1. if `FrameworkIO()` is not null, set or clear each of the three bits to its saved value, touching no other bit;
    2. call `SKSEMenuFramework::SetHotkeyEnabled(savedHotkeyEnabled)`;
    3. log `[UI] CaptureGuard: off after {:.2f}s ({})`, with the seconds since `onSince`;
    4. set `on = false`.
  - **`Reapply()`**: when `FrameworkIO()` is not null and `!BitsApplied(*io)`, log once per guard period
    (`reEnableLogged`) `[UI] CaptureGuard: nav flags were re-enabled by the host`, then call `ApplyBits`.
  - **`NoteCapturePageDrawn()`**: `lastPageDraw = now`, `pageEverDrawn = true`.
  - **`CaptureGuardWantsHeldState()`**: returns `on`.
  - **`UpdateCaptureInputGuard(captureWaiting, anyInputHeld)`**:
    1. **Time**: `dt` is the seconds since `lastUpdate`, clamped to `[0, 0.25]` and 0 on the first call. Then
       `lastUpdate = now`.
    2. **Page timeout (R6)**: if `captureWaiting` and the page was not drawn within `kCapturePageTimeoutSec` (or never),
       call `CaptureState::Get().Cancel()` and `captureActive.store(false)`, log
       `[UI] Capture: page not shown -> cancelled`, and set `captureWaiting = false`.
    3. **Capture waiting**: `TurnOn()` if `!on`, otherwise `Reapply()`. Set `releaseWaitSecs = 0` and return.
    4. **Off**: if `!on`, return.
    5. **Release wait**: `releaseWaitSecs += dt`. If `!anyInputHeld`, call `TurnOff("released")`. Else, if
       `releaseWaitSecs >= kCaptureGuardReleaseCapSec`, call `TurnOff("cap")`. Else call `Reapply()`.
- [X] T003 [P] Add `void UpdateCaptureGuard(bool captureWaiting, bool anyInputHeld);` to
  `include/Application/HudController.h`. In `src/Application/HudController.cpp`, implement it as a call to
  `IntegratedMagic::CaptureGuard::UpdateCaptureInputGuard`, including `UI/CaptureInputGuard.h`.
- [X] T004 In `src/Adapters/Inbound/DXGIPresentHook.cpp` (contract §1, R3, R4; depends on T001, T003):
  - **`ReadGamepadMask()`**: replaces `PollGamepadCapture`. It returns a `std::uint32_t` with one bit per plugin
    index:
    - XInput buttons through the existing `kMap`, as bits 0–13;
    - `bLeftTrigger > 64` as bit 14, and `bRightTrigger > 64` as bit 15;
    - `0` when `XInputGetState(0, ...)` fails.
  - **`ReadMouseHeld(bool out[5])`**: `GetAsyncKeyState` of `VK_LBUTTON`, `VK_RBUTTON`, `VK_MBUTTON`, `VK_XBUTTON1` and
    `VK_XBUTTON2`.
  - **`AnyInputHeld(std::uint32_t gpMask)`**: true when `gpMask != 0` or any VK in `1..255` has
    `GetAsyncKeyState(vk) & 0x8000`. That range covers the mouse buttons.
  - **File-local state**: `std::uint32_t s_prevGamepad`, `bool s_prevMouse[5]`, `bool s_wasWaiting`.
  - **`PollCapturedInput()`** becomes:
    1. `waiting = CaptureState::Get().captureRequested || captureActive` (include `Shared/CaptureState.h`);
    2. `needDevices = waiting || CaptureGuard::CaptureGuardWantsHeldState()`;
    3. if `needDevices`: `gp = ReadGamepadMask()` and read the mouse;
    4. **First waiting frame** (`waiting && !s_wasWaiting`): set `s_prevGamepad = gp` and `s_prevMouse = mouse`,
       capture nothing (the priming of R4);
    5. **Later waiting frames**:
       - each mouse button that goes from up to down calls `InjectCapturedScancode(kMouseButtonBase + i)`;
       - the lowest bit of `gp & ~s_prevGamepad` calls `InjectCapturedGamepad(index)`;
       - then the previous states are updated;
    6. `s_wasWaiting = waiting`;
    7. `held = !waiting && CaptureGuard::CaptureGuardWantsHeldState() && AnyInputHeld(gp)`;
    8. call `Application::HudController::Get().UpdateCaptureGuard(waiting, held)`.
  - Keep the call site in `Impl::thunk` (after `ImGui_ImplWin32_NewFrame` and `QueryBackbufferSize`), so it runs once
    per frame.
- [X] T005 [P] In `src/Adapters/Inbound/WndProcHook.cpp`, inside `if (IsCaptureModeActive())`, for `WM_KEYDOWN` /
  `WM_SYSKEYDOWN` (contract §1, §2; R4, R7):
  - if `lParam & (1 << 30)` (auto-repeat of a key held from before), do nothing;
  - else if `sc == kDIK_Escape`, call `InputController::Get().CancelHotkeyCapture()` and `SetCaptureModeActive(false)`,
    and log `[UI] Capture: Escape -> cancelled`;
  - else inject as today. Mouse messages are unchanged.
- [X] T006 [P] In `src/UI/MENU.cpp` (contract §2, §4; R5, R6; depends on T001):
  - **`DrawKeyValueCapture`, the wrong-device branch**: replace `CaptureState::Get().Request();` +
    `captureActive.store(true)` with:
    - a comment: "The other device cancels the capture and leaves the field unchanged (spec 009, FR-010).";
    - `MAGIC_DEBUG_LOG("[UI] Capture: {} input during a {} capture -> cancelled", gotKb ? "kb" : "gp", wantKeyboard ? "kb" : "gp");`;
    - `CancelFieldCapture();`.
  - **Same function, `isThis` branch**: when `Poll()` returns `-1` and neither `captureRequested` nor `captureActive` is
    set, clear `g_fieldCapture`, log `[UI] Capture: stale field capture cleared`, and draw the field as idle this frame
    (keep the pairs balanced).
  - **`DrawSettings`**: first statement, `IntegratedMagic::CaptureGuard::NoteCapturePageDrawn();`. Include
    `UI/CaptureInputGuard.h`.
- [X] T007 [P] In `docs/TEST_MATRIX.md`, section 10, after 10.17, add the spec 009 cases (research R10), in Portuguese
  and in the file's style:
  - **Intro line**: "Captura de hotkey no SKSE menu sem o menu reagir ao input capturado (spec
    `specs/009-fix-capture-input-leak/`):"
  - **10.18**: "Campo de gamepad: capturar cada um dos 16 botões/gatilhos (D-pad, Start, Back, L3, R3, LB, RB, A, B,
    X, Y, LT, RT) → cada valor gravado; o menu não move o foco, não ativa nada, não volta, não fecha, não abre o
    seletor de janelas; D-pad segurado não move o foco até soltar"
  - **10.19**: "Campo de teclado: capturar setas, `Enter`, `Space`, `Tab` e os botões esquerdo, direito e do meio do
    mouse sobre outro controle → valor gravado e nenhum controle reage"
  - **10.20**: "Ativar \"Cap\" com `A` do gamepad, e com `Enter`/`Space` no teclado → esse mesmo botão/tecla não é
    capturado; a captura continua esperando"
  - **10.21**: "Outro dispositivo durante a captura (gamepad num campo de teclado, tecla num campo de gamepad), 5×
    cada → captura cancelada, campo sem mudança, menu sem reação; `Escape` cancela a captura (e o SKSE menu fecha como
    sempre)"
  - **10.22**: "Depois de cada captura, ao soltar tudo, o menu reage normalmente no primeiro aperto; fechar o SKSE
    menu durante uma captura e reabrir → nada capturado e navegação normal"
- [X] T008 Build debug, then release **last**, so MO2 holds the release DLL (research R11). Depends on T001–T007.
  - Expect 0 errors and 0 new compiler warnings in both presets.
  - Check that the MO2 DLL hash matches `build\release\IntegratedMagic.dll`.
  - Delete any `IntegratedMagic.pdb` left in the MO2 plugin folder.

**Checkpoint**: the guard is built; story validation can start.

---

## Phase 3: User Story 1 - Capturing a gamepad button does not drive the SKSE menu (Priority: P1) 🎯 MVP

**Goal**: each gamepad button or trigger is captured, and the SKSE menu does not react to it, while it is held or at
its release.

**Independent Test**: [quickstart.md](quickstart.md) §1 and §3 step 1 (SC-001, SC-005).

- [X] T009 [US1] Desk-check the gamepad path against contract §1–§3. Trace each case and record the result in the
  implementation log:
  - **`A` on "Cap"**:
    - ImGui activates on the press, and the capture starts in that frame;
    - on the next frame, `PollCapturedInput` sees `waiting && !s_wasWaiting`, primes `s_prevGamepad` with `A`, and
      captures nothing;
    - `A` counts only after it is released and pressed again.
  - **Another button**: it goes from up to down, so `InjectCapturedGamepad` stores it. The guard is on from that frame
    and stays on until every input is released (`AnyInputHeld`).
  - **Held D-pad**: navigation stays off while it is held, and comes back only after the release.
  - **Gamepad press in a keyboard field**: `DrawKeyValueCapture` cancels, and the guard keeps the press from the menu
    until it is released.
  - **No gamepad connected**: `ReadGamepadMask` returns 0, and XInput is read only while a capture waits or the guard
    is on.
- [X] T010 [US1] **(in-game, user)** [quickstart.md](quickstart.md) §1 and §3 step 1 on the T008 build. Check
  `skse64.log` first: the `IntegratedMagic` line must load. Then send `IntegratedMagic.log`.

### Key ownership (found in T010, research R2b)

- [X] T019 [US1] In `src/UI/CaptureInputGuard.cpp`, replace the config-flag changes with key ownership:
  - while on, once per frame, call `igSetKeyOwner` (through `GetMenuFrameworkFunction`, null-checked) for every key
    from `ImGuiKey_NamedKey_BEGIN` to `ImGuiKey_NamedKey_END`, with owner `0x494D4347` and
    `ImGuiInputFlags_LockUntilRelease`;
  - log `<n> ImGui keys claimed` once per guard period;
  - keep the hotkey disable and restore. Remove the flag save, restore and re-apply code.
- [X] T020 [US1] Update research (R2 outcome, R2b, R8 outcome), data-model, contract §3/§6, quickstart and
  `.claude/CLAUDE.md`.
- [X] T021 [US1] Build as in T008, with 0 errors and 0 new warnings, and the hash check. Then clang-format the changed
  files.

### Framework input callback (found in the second T010 run, research R8 fallback)

- [X] T022 [US1] In `include/UI/CaptureInputGuard.h` / `src/UI/CaptureInputGuard.cpp`, add
  `RegisterFrameworkInputBlock()`.
  - It registers once `SKSEMenuFramework::AddInputEvent(OnFrameworkInput)`. The callback returns true for every event
    while the guard is on.
  - It logs the first blocked event per guard period, and the `off` line reports the count.
  - Call it from `MENU::Register` in `src/UI/MENU.cpp`.
- [X] T023 [US1] Add diagnostics:
  - the `owner check` line (`igGetKeyOwner` of `GamepadFaceRight` and `GamepadDpadDown`), in `CaptureInputGuard.cpp`;
  - `waiting for release of <input>`, from `FirstHeldInput`, in `src/Adapters/Inbound/DXGIPresentHook.cpp`.
- [X] T024 [US1] Build as in T008, with the hash check and clang-format on the changed ranges.

### Outer input hook (found in the third T010 run, research R12)

- [X] T025 [US1] Create `include/Adapters/Inbound/MenuInputShieldHook.h` / `src/Adapters/Inbound/MenuInputShieldHook.cpp`.
  - It is installed lazily from `PollCapturedInput` on the first waiting frame, at the `PollInputDevicesHook` site.
  - It passes `{nullptr}` on while `HudController::ShouldHideMenuInput()` holds.
  - Add `CaptureGuard::ShouldHideMenuInput()`, and bump `kCallHookCount` to 4 in `include/Hooks.h`.
  - Remove the input callback and the owner check.
- [X] T026 [US1] Skip undefined and gamepad virtual keys in `FirstHeldInput` (`src/Adapters/Inbound/DXGIPresentHook.cpp`).
  Update `.claude/CLAUDE.md`, TEST_MATRIX 0.5 / 10.21, research R12, the spec and the quickstart.
- [X] T027 [US1] Build as in T008, with the hash check and clang-format.

---

## Phase 4: User Story 2 - Capturing a key or mouse button does not drive the SKSE menu (Priority: P2)

**Goal**: keys and mouse buttons are captured into keyboard fields with no menu reaction, `Enter`/`Space` used on
"Cap" is not captured, and `Escape` cancels.

**Independent Test**: [quickstart.md](quickstart.md) §2 and §3 steps 2–3 (SC-002, SC-005).

- [X] T011 [US2] Desk-check the keyboard and mouse path against contract §1–§3, and record it in the log:
  - **Auto-repeat**: an auto-repeat `WM_KEYDOWN` (bit 30) is ignored.
  - **`Escape`**: it cancels, and the MENU reconcile clears `g_fieldCapture` on the next draw.
  - **A mouse click**: it is captured by `WM_LBUTTONDOWN`, or by the mouse edge in `PollCapturedInput`. With `NoMouse`
    set, ImGui does not press the control under the cursor.
  - **A key in a gamepad field**: it cancels.
- [ ] T012 [US2] **(in-game, user)** [quickstart.md](quickstart.md) §2 and §3 steps 2–3 on the T008 build.

---

## Phase 5: User Story 3 - The SKSE menu works as before outside captures (Priority: P2)

**Goal**: outside a capture, and after one ends, the SKSE menu reacts as before. No capture leaves its input disabled.

**Independent Test**: [quickstart.md](quickstart.md) §4 (SC-003, SC-004).

- [X] T013 [US3] Desk-check restoration and the ending paths (contract §3–§5), and record it in the log:
  - **Restore**: `TurnOff` writes back only the three saved bits and the saved hotkey state.
  - **Bounded**: the guard ends at most 3 s after the capture.
  - **Page timeout**: it cancels a capture when the SKSE menu is closed or another section is shown.
  - **Ending paths**: a slot change still calls `CancelFieldCapture`, and every path in contract §4 ends the guard.
  - **Gameplay**: `git diff main -- src/Input src/Application/InputController.cpp` must be empty.
- [ ] T014 [US3] **(in-game, user)** [quickstart.md](quickstart.md) §4 on the T008 build, including TEST_MATRIX 10.7
  and 10.9 as regression. Then tick TEST_MATRIX 10.18–10.22.

---

## Phase 6: Polish & cross-cutting concerns

- [X] T015 [P] In `.claude/CLAUDE.md`:
  - **HUD / UI section**: after the "SKSE menu pair rule" block, add a "Hotkey capture input guard (spec 009)" block:
    - why: SKSE Menu Framework hooks the same input call site as the plugin, as the outer hook, so the plugin's event
      filtering never reaches the menu;
    - the guard: nav gamepad and keyboard off, `NoMouse` on and the framework hotkey off while a capture waits and
      until everything is released, at most 3 s, with exact restore;
    - edge-triggered captures, primed on the first waiting frame;
    - the other device and `Escape` cancel, and a capture whose page is not drawn for 0.25 s is cancelled;
    - the log lines.
  - **In "Common tasks"**: the row "The SKSE menu reacts during a hotkey capture, or a capture takes the wrong input" →
    `src/UI/CaptureInputGuard.cpp`, `src/Adapters/Inbound/DXGIPresentHook.cpp` (`PollCapturedInput`),
    `src/Adapters/Inbound/WndProcHook.cpp`, `src/UI/MENU.cpp` (`DrawKeyValueCapture`).
- [X] T016 Run clang-format in dry-run mode on the new files and on the changed line ranges of each changed `.cpp` / `.h`
  only, and fix any violation. The binary is
  `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe --dry-run`, with
  `--lines` passed through `@()` in PowerShell. If a fix changes code, rebuild as in T008.
- [X] T017 Final scope check:
  - `git diff --stat main -- src/Domain include/Domain src/Input include/Input src/Config include/Config src/Persistence SKSE`
    must be empty;
  - `git diff --stat main` must show only this spec's files, `docs/TEST_MATRIX.md`, `.claude/CLAUDE.md` and the
    source files of T001–T006;
  - the MO2 DLL hash must match `build\release\IntegratedMagic.dll`.
- [X] T018 Pre-commit review:
  - set `**Status**` in `specs/009-fix-capture-input-leak/spec.md`;
  - complete the implementation log;
  - review `git diff`.

  Commit and push only when the user asks.

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1** (T001): first.
- **Phase 2**:
  - T002 after T001;
  - T003, T005 and T007 at any time (different files);
  - T004 after T001 and T003;
  - T006 after T001;
  - T008 after T001–T007.
- **Phases 3–5**:
  - desk checks (T009, T011, T013) after T008;
  - in-game tasks (T010, T012, T014) on the T008 build, in any order. T014 ticks the TEST_MATRIX cases.
- **Phase 6**: T015 at any time after T006. Then T016 → T017 → T018, last.

### User story dependencies

- **US1 (P1)**: the MVP. It depends only on Phase 2.
- **US2 (P2)**: independent of US1. It shares the guard.
- **US3 (P2)**: validates restoration on the same build. It has no code of its own unless T013 finds a gap.

### Same-file sequencing

| File | Order |
|---|---|
| `include/UI/CaptureInputGuard.h` | T001 |
| `src/UI/CaptureInputGuard.cpp` | T002 |
| `src/Adapters/Inbound/DXGIPresentHook.cpp` | T004 |
| `src/Adapters/Inbound/WndProcHook.cpp` | T005 |
| `src/UI/MENU.cpp` | T006 |
| `docs/TEST_MATRIX.md` | T007 → T014 (ticks) |

---

## Parallel Example

```text
# Phase 2, after T001:
T002  src/UI/CaptureInputGuard.cpp          (the guard)
T003  HudController.h + .cpp                (pass-through)
T005  src/Adapters/Inbound/WndProcHook.cpp  (repeat bit, Escape)
T006  src/UI/MENU.cpp                       (other device, reconcile, page stamp)
T007  docs/TEST_MATRIX.md                   (10.18–10.22)
# then T004 (after T003)

# After T008, the three desk checks can run together:
T009  gamepad   T011  keyboard/mouse   T013  restore and ending paths
```

---

## Implementation Strategy

### MVP first (US1)

1. Phases 1–2: the guard, edge-triggered captures, outcomes, wiring and the build (T001–T008).
2. US1: desk check, then the in-game gamepad captures (T009–T010).
3. **Stop and validate**: gamepad captures with no menu reaction.

### Incremental delivery

1. US2 (T011–T012): keyboard and mouse, `Escape`, on the same build.
2. US3 (T013–T014): normal use afterwards, closing the menu, regression 10.7 and 10.9.
3. Polish (T015–T018).

## Notes

- Commit or push only when the user asks.
- **If the menu still reacts in game**: look for `CaptureGuard: nav flags were re-enabled by the host` before changing
  the design. If it is there, the fallback of research R8 (`AddInputEvent`) is the next step.

## Implementation log

### 2026-11-21: the guard, captures, build, desk checks (T001–T009, T011, T013, T015, T016)

- **T001–T003**: `include/UI/CaptureInputGuard.h`, `src/UI/CaptureInputGuard.cpp` and `HudController::UpdateCaptureGuard`.
  - **Addition**: `HudController::CaptureGuardWantsHeldState()`, so the Present hook (an inbound adapter) does not
    include the UI module directly (Constitution I).
- **T004**: `ReadGamepadMask`, `ReadMouseHeld` and `AnyInputHeld`, with `PollCapturedInput` edge-triggered and primed
  on the first waiting frame.
- **T005**: `WndProcHook` ignores auto-repeat `WM_KEYDOWN` and turns `Escape` into a cancel.
- **T006**: `MENU.cpp`:
  - the other device cancels;
  - a stale `g_fieldCapture` is cleared. On that frame the field still draws "..." once, as on the existing
    value-stored path;
  - `DrawSettings` stamps the page.
- **T007**: TEST_MATRIX 10.18–10.22.
- **T008**:
  - debug and release built with exit 0 and no compiler warnings;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `54523789…0DA5`), and the PDB was deleted;
  - one comment line in `CaptureInputGuard.h` was re-wrapped after the build. It is a comment only, with no code
    change.
- **T009 desk check** (gamepad):
  - **`A` on "Cap"**: ImGui activates on the press. The next Present sees `waiting && !s_wasWaiting`, primes
    `s_prevGamepad` with `A`, and captures nothing. A press of another button sets a bit in `gp & ~s_prevGamepad`, and
    `InjectCapturedGamepad(countr_zero)` stores it.
  - **The guard**: it turns on in the first waiting frame (`TurnOn`). After the value, `waiting` is false and
    `AnyInputHeld` keeps it on until the release, so a held D-pad cannot repeat.
  - **Gamepad in a keyboard field**: it cancels through `DrawKeyValueCapture`, and the guard keeps it off until the
    release.
  - **No controller**: `ReadGamepadMask` returns 0, and it runs only while a capture waits or the guard is on.
    Result: OK.
- **T011 desk check** (keyboard and mouse):
  - **Auto-repeat**: an auto-repeat `WM_KEYDOWN` is ignored (bit 30).
  - **`Escape`**: it calls `CancelHotkeyCapture` and `SetCaptureModeActive(false)`, and the next draw clears
    `g_fieldCapture` (stale path).
  - **A click**: `WM_LBUTTONDOWN` and the mouse edge inject it. ImGui does not press the control under the cursor,
    because `NoMouse` is set from the frame the capture started.
  - **A key in a gamepad field**: it cancels. Result: OK.
- **T013 desk check** (restoration and ending paths):
  - **Restore**: `TurnOff` writes back only the three saved bits and the saved hotkey state.
  - **Ending paths**: the 3 s cap and the 0.25 s page timeout end every wait, and the slot-change `CancelFieldCapture`
    is unchanged.
  - **Gameplay**: `git diff main -- src/Input include/Input src/Application/InputController.*` is empty. Result: OK.
- **T015**: `.claude/CLAUDE.md` has the "Hotkey capture input guard" block under HUD / UI, and the Common tasks row.
- **T016**:
  - clang-format is clean on the new files and every changed range;
  - two violations remain in `DXGIPresentHook.cpp`, the include order and the `kMap` alignment. Both exist on `main`
    too, so they were left as they are.

**Next**: T010, T012 and T014, in game, by the user, on the T008 build.

### 2026-11-21: first in-game run, the flags did not hold; key ownership (T019–T021)

- **User report**: the SKSE menu still reacts to the button pressed during a capture.
- **Log** (15:48:01–15:48:32):
  - every capture logged `CaptureGuard: on`, then `nav flags were re-enabled by the host` one frame (about 17 ms)
    later. The framework rewrites its ImGui config flags every frame, before ImGui processes input. That is the risk
    of research R8;
  - the other paths worked: `kb input during a gp capture -> cancelled` three times, `Escape -> cancelled`, `stale
    field capture cleared`, and `off after <s>s (released)` after each capture.
- **T019**: `src/UI/CaptureInputGuard.cpp` now claims every named ImGui key once per frame while on, with
  `igSetKeyOwner` (owner `0x494D4347`, `ImGuiInputFlags_LockUntilRelease`, research R2b). No config flag is touched
  any more. The hotkey disable and restore are unchanged. New log: `<n> ImGui keys claimed`.
- **T020**: research (R2 outcome, R2b, R8 outcome), data-model, contract §3/§6, quickstart and `.claude/CLAUDE.md`
  were updated.
- **T021**:
  - debug and release built with exit 0 and no compiler warnings;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `CBA4B8B2…5BF3`), and the PDB was deleted;
  - clang-format is clean on `CaptureInputGuard.cpp` / `.h`.

**Next**: T010, T012 and T014 again, on the T021 build.

### 2026-11-21: second in-game run, key ownership did not stop it; input callback (T022–T024)

- **User report**: the SKSE menu still reacts to the button pressed during a capture.
- **Log** (16:01:25–16:01:35):
  - `CaptureGuard: on (keys claimed, hotkey off)`, then `154 ImGui keys claimed`, so the claim ran;
  - then `off after 4.11s (cap)`: some input stayed reported as held for the whole 3 s release wait.
- **Reading**:
  - ImGui navigation and the public key queries would ignore owned keys, so the reaction most likely comes from the
    framework's own input handling, before ImGui;
  - the installed version is newer than the public repository (it has `SetHotkeyEnabled` and `RegisterInpoutEvent`),
    so that code was never read.
- **T022**: the research R8 fallback, built.
  - `RegisterFrameworkInputBlock()` (called from `MENU::Register`) registers `SKSEMenuFramework::AddInputEvent`.
  - While the guard is on, the callback returns true for every event (block). It logs the first blocked event per
    guard period, and the `off` line now reports the count.
  - The key claim stays.
- **T023**: diagnostics.
  - `owner check FaceRight=… DpadDown=…` a few frames after the guard turns on, to see whether the claim holds in the
    framework's context.
  - `waiting for release of <gp:n|vk:0x..>` when the input that keeps the guard on changes, to explain the 3 s cap.
- **T024**:
  - debug and release built with exit 0 and no compiler warnings;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `6FC0EAFD…4910`), and the PDB was deleted;
  - clang-format is clean on the changed ranges.

**Next**: in game, by the user. The log tells which mechanism is acting: `framework input blocked` and the blocked
count show whether the callback is called and whether blocking works.

### 2026-11-21: third in-game run, callback and ownership both acting, menu still reacts; outer hook (T025–T027)

- **User report**: still not working.
- **Log** (16:13:33–16:14:31):
  - `framework input callback registered`;
  - `owner check FaceRight=0x494d4347 DpadDown=0x494d4347`, so ownership holds;
  - `framework input blocked (first: type=0 device=2 id=8192)`, which is gamepad `B`, with 106 and 125 events
    blocked;
  - `waiting for release of gp:11`, then `vk:0x07` for seconds, ending at `off ... (cap)`.
- **Reading** (research R12): the framework acts on raw events in its own thunk, before its callbacks and before
  ImGui. VK `0x07` (undefined) was reported held by a driver.
- **T025**: `include/Adapters/Inbound/MenuInputShieldHook.h` / `src/Adapters/Inbound/MenuInputShieldHook.cpp`.
  - A second `write_call` at the `PollInputDevicesHook` site, installed lazily on the first waiting frame from
    `PollCapturedInput`, so it wraps the framework's hook.
  - While `CaptureGuard::ShouldHideMenuInput()` holds (guard on or capture waiting, page drawn within 0.25 s), it
    passes `{nullptr}` on. The install logs the previous target's module.
  - `include/Hooks.h`: `kCallHookCount` is now 4.
  - The `AddInputEvent` callback and the owner-check diagnostic were removed. Key ownership stays.
- **T026**: `FirstHeldInput` skips undefined virtual keys and the gamepad VK range (`IsIgnoredVirtualKey`).
  `.claude/CLAUDE.md` (hooks table, guard block), TEST_MATRIX 0.5 and 10.21, research R12, the spec's `Escape` edge
  case and the quickstart §3.3 were updated. `Escape` during a capture now only cancels, and the menu stays open.
- **T027**:
  - debug and release built with exit 0 and no compiler warnings;
  - the first release configure failed with a transient vcpkg compiler-detection error, after the debug build had
    already copied the debug DLL to MO2. The rebuild (debug, then release) succeeded;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `902237D3…9EB8`), and the PDB was deleted;
  - clang-format was applied to the flagged ranges.

**Next**: in game, by the user, on the T027 build. Check `[Hooks] MenuInputShieldHook installed (outer, previous
target in SKSEMenuFramework.dll)` first.

### 2026-11-21: fourth in-game run, it works; closing (T010, T017, T018)

- **User report**: "Funcionou".
- **Log** (16:26:13–16:26:31, T027 build):
  - `[Hooks] MenuInputShieldHook installed (outer, previous target in InputManager.dll)`, once. The outermost hook
    before this one was the Input Manager mod's, not SKSE Menu Framework's. The framework's hook is further in, so it
    got the empty list too (research R12, outcome);
  - 12 gamepad captures, `B` (`gp:11`) ×3 and `LT` (`gp:14`) ×9. Each one went `on`, then `waiting for release`, then
    `off after 0.35–1.23s (released)`, with 21–74 input frames hidden;
  - no capture reached the 3 s cap, so the VK `0x07` skip (T026) holds;
  - no `[Hooks]` warning or critical line.
- **Docs**: the expected module name was replaced by `<module>` in TEST_MATRIX 0.5, the quickstart, research R12 and
  `.claude/CLAUDE.md`. The contract (§2 `Escape`, §3, §5, §6), data model and plan now describe the outer hook. The
  `CaptureInputGuard.h` header comment was updated, a comment-only change, so the tested binary is unchanged.
- **TEST_MATRIX**:
  - ticked: 0.5;
  - still open: 10.18–10.22. The log shows only `B` and `LT` captures into gamepad fields: no keyboard capture, no
    other-device or `Escape` cancel and no menu close during a capture. T012 and T014 stay open for them.
- **T017**:
  - nothing changed in `src/Domain`, `include/Domain`, `src/Input`, `include/Input`, `src/Config`, `include/Config`,
    `src/Persistence` or `SKSE`;
  - the changes are this spec, `docs/TEST_MATRIX.md`, `.claude/CLAUDE.md`, the source files of T001–T006 and
    T025–T026, and the user's own `.claude/tasks/integrated_magic_bugs_e_melhorias.md`, which goes in a separate
    commit;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `902237D3…9EB8`).
- **T018**: spec `Status` set to Implemented, and `git diff` reviewed.
