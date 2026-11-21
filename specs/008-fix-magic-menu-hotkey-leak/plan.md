# Implementation Plan: Slot and wheel hotkeys must not trigger Magic Menu actions

**Branch**: `fix/magic-menu-hotkey-leak` | **Date**: 2026-11-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/008-fix-magic-menu-hotkey-leak/spec.md`

## Summary

In the Magic Menu, quick bind (hover + slot hotkey) and the wheel hotkey work, but the menu also runs its own
functions for the same keys.

**Cause (research R1)**:
- The Magic Menu blocks slot input, and that blocked state also skips the whole input filter, so every key reaches the
  menu.
- The one filter written for the wheel only runs inside that skipped filter, so it is dead code.

**Approach**:
1. **A Magic Menu hold** (R1–R7): while the Magic Menu is open, a dedicated pass holds back every press of a key that
   belongs to a slot or wheel hotkey.
   - Each press waits up to 0.3 s, or until its key is released.
   - If its hotkey completes, all its key events are consumed, so the menu never sees them.
   - Otherwise the events are replayed to the menu in order, one per frame, through the existing synthetic input
     stream.
2. **Its own state**: a new store, separate from the gameplay filter of spec 004, which stays unchanged (R6, R9).
3. **Cleanup**: remove the dead wheel filter (R8).
4. **Ship with it**: logs (R10), TEST_MATRIX cases (R11), release-build validation (R12).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**:
- CommonLibSSE-NG (`extern/CommonLibVR-ng`): `RE::UI::IsMenuOpen`, `RE::ButtonEvent`, `RE::INPUT_DEVICE`;
- the plugin's input layer (`KeyStateStore`, `HotkeyCacheStore`, `HotkeyMatcher`) and
  `IntegratedMagic::detail::EnqueueRetainedEvent`.

**Storage**: N/A. No settings, INI or persisted data changes (FR-012).

**Testing**: no automated harness. In-game validation following [quickstart.md](quickstart.md), on the release DLL
(R12).

**Target Platform**: Windows x64, Skyrim SE / AE

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**:
- **Per button event** in the Magic Menu: one scan of the active combos, the same cost as `ShouldFilterAndSave` in
  gameplay.
- **Per frame**: one combo check per slot plus the wheel, a few dozen comparisons.
- **Outside the Magic Menu**: no added cost.

**Constraints**:
- **Never outside the Magic Menu**: the 0.3 s window (FR-004).
- **No delay** for keys outside every hotkey (FR-005).
- **No stuck key** on any exit (FR-009).
- **No slot activation** in menus (FR-008).

**Scale/Scope**:
- **Source**:
  - one new header/source pair in `Input/`;
  - one new constant;
  - three call sites in `InputController`;
  - removal of one dead function.
- **Docs**: `docs/TEST_MATRIX.md` and `.claude/CLAUDE.md`.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| **I. Layered architecture** | PASS | **Rules**: in the input layer (`include/Input/`, `src/Input/`). **Wiring**: in `Application::InputController`. **Game writes**: the only one is the replay, through the existing outbound adapter `EnqueueRetainedEvent`, called from `Application/` as the gameplay replay already is. **Untouched**: no change to `Domain/`, `UI/` or `Config/` |
| **II. Main-thread state machine** | PASS | **Thread**: the new store is used only on the main thread inside `PollInputDevicesHook`. **`KeyStateStore`**: still written only by `ProcessButtonEvents`, and the new code only reads it. **`MagicState`**: no new state. **Docs**: `.claude/CLAUDE.md` documents the store and the rules in the same change |
| **III. Never strand the player** | PASS | **Sessions**: no session runs in menus (the Magic Menu forces an exit on open). **Bounded waits**: 0.3 s per press, and the outbox sends one event per frame. **Cleanup**: clearing the store on every exit path (menu close, game load, hotkey reload) leaves no key stuck (R7) |
| **IV. Dual-cast sync** | PASS (N/A) | No change to `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt` or `SyntheticInput` |
| **V. Opt-in & compatibility** | PASS | **No patch flag**: it is a bug fix that restores the intended behavior (a hotkey is for the plugin, not the menu), chosen by the user with no option (FR-012). **The only new delay**: up to 0.3 s for a hotkey key used alone in the Magic Menu, and only until its release. **Unclaimed input**: input not claimed by a hotkey is passed or replayed, as the principle requires. **Settings**: no INI or saved data change |
| **VI. Observable & verified** | PASS with deviation | **Logs**: every decision logs with `[Input] MenuHold:` (R10). **Tests**: TEST_MATRIX gains 10.11–10.16, with section 6 as regression. **Build**: validation on the release DLL; see Complexity Tracking |

**Post-design re-check (after Phase 1)**:
- **Unchanged.** The data model adds only input-layer state, cleared on every exit path, and the contract keeps every
  wait bounded and gameplay untouched.
- **Pre-existing deviation**: `PCH.h` defines `DEBUG` unconditionally, so debug logging does not compile out of release
  builds. This spec relies on it for validation but does not change it.

## Project Structure

### Documentation (this feature)

```text
specs/008-fix-magic-menu-hotkey-leak/
├── plan.md                      # This file
├── research.md                  # Phase 0: R1–R12
├── data-model.md                # Phase 1: MagicMenuHoldStore, MenuFate, transitions
├── quickstart.md                # Phase 1: in-game validation
├── contracts/
│   └── magic-menu-hold.md       # Phase 1: per-event and per-frame rules, guarantees, logs
├── checklists/
│   └── requirements.md          # Spec quality checklist
└── tasks.md                     # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
include/Shared/InputConstants.h       # kMagicMenuHoldSec = 0.3f
include/Input/MagicMenuHold.h         # NEW: MagicMenuHoldStore, MenuHeldEvent, MenuFate, function declarations
src/Input/MagicMenuHold.cpp           # NEW: FilterMagicMenuEvents, TickMagicMenuHold, PopMagicMenuOutbox, ClearMagicMenuHold
include/Input/InputFilter.h           # IsMagicMenuOpen declaration
src/Input/InputFilter.cpp             # IsMagicMenuOpen; remove the dead ShouldFilterHudToggle call in FilterEvents
include/Input/HudToggle.h             # remove ShouldFilterHudToggle
src/Input/Hudtoggle.cpp               # remove ShouldFilterHudToggle
include/Application/InputController.h # m_menuHold, m_prevInMagicMenu, m_quickBind, ResolveQuickBindSlots
src/Application/InputController.cpp   # menu pass, tick, outbox send, clears on close / load / reload
include/Input/QuickBindResolver.h     # NEW (R13): QuickBindState, ResolveQuickBind, ResetQuickBind
src/Input/QuickBindResolver.cpp       # NEW (R13): longest hotkey wins, pending wait for a longer one
src/Application/HudController.cpp     # quick bind through ResolveQuickBindSlots instead of its own rising-edge mask
docs/TEST_MATRIX.md                   # 10.11–10.16
.claude/CLAUDE.md                     # Magic Menu hold under the input system section, logs, common tasks row
```

**Structure Decision**: a single SKSE plugin project. The new rules live in the input layer beside `InputFilter` and
`ExclusiveTracker`, in their own file, so the spec 004 gameplay rules stay untouched. `CMakeLists.txt` globs `src/`, so
the new file needs no build change.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **VI**: in-game validation on the release DLL instead of a DEBUG build (**approved by the user, 2026-11-21**) | The DEBUG DLL faults inside `Hooks::Install_Hooks` at start-up, a DEBUG-only defect unrelated to this spec. The release build writes the same logs while `PCH.h` defines `DEBUG` | Fixing the DEBUG crash first: it is a separate defect with no root cause yet. Specs 002–007 were approved the same way |
