# Implementation Plan: The SKSE menu must not react to input pressed during a hotkey capture

**Branch**: `fix/capture-input-leak` | **Date**: 2026-11-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/009-fix-capture-input-leak/spec.md`

## Summary

While a hotkey field captures input in the SKSE menu, the gamepad button pressed for it also drives the menu: focus,
activate, back, window switcher.

**Cause (research R1)**:
- **The same hook site**: SKSE Menu Framework hooks the same input call site as the plugin, as the outer hook.
- **It consumes the events first**: while its window is open, it feeds every event to its ImGui context and passes an
  empty list on. So the plugin's existing event filtering never reaches the menu.
- **Where the capture reads**: the capture itself reads XInput and window messages.

**Approach**:
1. **A capture input guard** in the UI layer (R2, R3):
   - **While a capture waits, and until its input is released**: it turns off ImGui gamepad and keyboard navigation,
     makes ImGui ignore the mouse in the framework's shared context, and disables the framework's hotkey.
   - **Afterwards**: it restores exactly what it changed.
2. **Edge-triggered captures** (R4): the button or key that started the capture is not captured.
3. **Capture outcomes**:
   - the other device cancels (R5);
   - `Escape` cancels (R7);
   - a capture whose page is not shown is cancelled (R6).
4. **Ship with it**: logs (R9), TEST_MATRIX cases (R10), release-build validation (R11).

**What shipped** (changed by the in-game runs): the guard of item 1 did not hold, because the framework rewrites its
flags every frame (R2, R8). It became key ownership (R2b), and then an outer input hook, `MenuInputShieldHook`, that
hands the framework an empty event list during a capture (R12). The contract and `tasks.md` describe the shipped
design.

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**:
- SKSE Menu Framework through `extern/SKSEMenuFramework/SKSEMenuFramework.h`: `ImGuiMCP::GetIO` (`igGetIO`),
  `SKSEMenuFramework::SetHotkeyEnabled` / `IsHotkeyEnabled`, `GetMenuFrameworkModule`. All are exported by the
  installed DLL (checked with `dumpbin /exports`).
- XInput and Win32 `GetAsyncKeyState`, already used by the capture.

**Storage**: N/A. No settings, INI or persisted data changes (FR-009).

**Testing**: no automated harness. In-game validation following [quickstart.md](quickstart.md), on the release DLL
(approved).

**Target Platform**: Windows x64, Skyrim SE / AE

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**:
- **Per frame**: one guard update, with a few flag writes while on.
- **While the guard waits for releases**: at most 256 `GetAsyncKeyState` calls and one XInput read per frame, for at
  most 3 s.
- **When no capture waits**: one boolean check.

**Constraints**:
- **Shared context**: the framework's ImGui context must be left exactly as found (spec 006).
- **No stuck state**: no capture may leave navigation, the mouse or the hotkey disabled (FR-006).
- **Outside captures**: the menu's input is unchanged (FR-005).

**Scale/Scope**:
- **Source**: one new UI header/source pair, plus changes in `DXGIPresentHook.cpp`, `WndProcHook.cpp`, `MENU.cpp` and
  a pass-through in `HudController`.
- **Docs**: `docs/TEST_MATRIX.md` and `.claude/CLAUDE.md`.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| **I. Layered architecture** | PASS | **Device reads**: stay in the inbound adapters (`DXGIPresentHook`, `WndProcHook`). **The guard**: touches only the SKSE menu's ImGui state, so it lives in `UI/`, next to `MENU.cpp`, the only other code that uses the framework API. **Call path**: the Present hook reaches it through `Application::HudController`. **Untouched**: `Domain/` and `Config/` |
| **II. Main-thread state machine** | PASS | **State**: no `MagicState` state. The guard and edge state are file-local UI and adapter state, used on the main thread (Present hook, window procedure, page draw). **`KeyStateStore`**: untouched. **Docs**: `.claude/CLAUDE.md` documents the guard in the same change |
| **III. Never strand the player** | PASS | **No sessions**: none is involved. **Bounded guard**: it waits at most 3 s after the capture, and a capture whose page is not shown is cancelled after 0.25 s, so the menu always gets its input back |
| **IV. Dual-cast sync** | PASS (N/A) | No change to `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt` or `SyntheticInput` |
| **V. Opt-in & compatibility** | PASS | **No patch flag**: it is a bug fix, and the other-device cancel was chosen by the user (spec Clarifications). **Coexisting with the framework**: only documented ImGui flags and the framework's exported API are used, every change is restored exactly, and nothing changes outside captures. **Settings**: no INI or saved data change |
| **VI. Observable & verified** | PASS with deviation | **Logs**: `[UI] CaptureGuard:` and `[UI] Capture:` (R9). **Tests**: TEST_MATRIX 10.18–10.22, with 10.7 and 10.9 as regression. **Build**: validation on the release DLL, approved (Complexity Tracking) |

**Post-design re-check (after Phase 1)**:
- **Unchanged.** The design adds UI and adapter state only, restores everything it changes, and bounds every wait.
- **Pre-existing deviation**: `PCH.h` defines `DEBUG` unconditionally, so debug logging does not compile out of release
  builds. This spec relies on it for validation but does not change it.

## Project Structure

### Documentation (this feature)

```text
specs/009-fix-capture-input-leak/
├── plan.md                         # This file
├── research.md                     # Phase 0: R1–R11
├── data-model.md                   # Phase 1: guard state, edge state, transitions
├── quickstart.md                   # Phase 1: in-game validation
├── contracts/
│   └── capture-input-guard.md      # Phase 1: capture sources, outcomes, guard, ending paths, logs
├── checklists/
│   └── requirements.md             # Spec quality checklist
└── tasks.md                        # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
include/UI/CaptureInputGuard.h            # NEW: CaptureGuardInput, UpdateCaptureInputGuard, NoteCapturePageDrawn, constants
src/UI/CaptureInputGuard.cpp              # NEW: the guard (flags, hotkey, release wait, page timeout), logs
src/Adapters/Inbound/DXGIPresentHook.cpp  # edge-triggered gamepad and mouse capture; held state; guard update each frame
src/Adapters/Inbound/WndProcHook.cpp      # ignore auto-repeat WM_KEYDOWN; Escape cancels instead of being captured
include/Adapters/Inbound/MenuInputShieldHook.h    # NEW (R12): InstallOnce
src/Adapters/Inbound/MenuInputShieldHook.cpp      # NEW (R12): outer hook at the PollInputDevicesHook site
include/Hooks.h                           # kCallHookCount 4 (R12)
include/Application/HudController.h       # UpdateCaptureGuard pass-through
src/Application/HudController.cpp         # UpdateCaptureGuard pass-through
src/UI/MENU.cpp                           # other device cancels; stale capture reconcile; NoteCapturePageDrawn in DrawSettings
docs/TEST_MATRIX.md                       # 10.18–10.22
.claude/CLAUDE.md                         # capture input guard under HUD / UI, logs, common tasks row
```

**Structure Decision**: a single SKSE plugin project. The guard sits in `UI/` because it changes only the SKSE menu's
state, and the device reads stay in the inbound adapters that already do them. `CMakeLists.txt` globs `src/`, so the
new file needs no build change.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **VI**: in-game validation on the release DLL instead of a DEBUG build (**approved by the user, 2026-11-21**) | The DEBUG DLL faults inside `Hooks::Install_Hooks` at start-up, a DEBUG-only defect unrelated to this spec. The release build writes the same logs while `PCH.h` defines `DEBUG` | Fixing the DEBUG crash first: it is a separate defect with no root cause yet. Specs 002–008 were approved the same way |
