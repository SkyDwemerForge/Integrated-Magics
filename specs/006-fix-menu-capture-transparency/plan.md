# Implementation Plan: Fix the SKSE menu turning transparent when capturing hotkeys

**Branch**: `fix/menu-capture-transparency` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/006-fix-menu-capture-transparency/spec.md`

## Summary

The hotkey capture button ("Cap") of every hotkey field is drawn between two separate checks of "is a capture in
progress?": one before it, to disable it, and one after, to re-enable it (`src/UI/MENU.cpp:108–114`). The click starts
a capture, so in that frame the second check runs without the first. Each capture started this way leaves SKSE Menu
Framework's shared ImGui disabled stack one step out of balance, and the menu fades a little more (research R1).

**The fix** (research R2): draw that button inside an unconditional `BeginDisabled(g_fieldCapture.active)` …
`EndDisabled()` pair. That is the idiom the menu's other disabled buttons already use. The audit of every other
push/pop and begin/end pair of the UI found nothing else to change (R3).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: SKSE Menu Framework (`ImGuiMCP`, its shared ImGui context), CommonLibSSE-NG

**Storage**: N/A. No settings, INI or persisted data changes.

**Testing**: no automated harness. In-game validation with repeated captures ([quickstart.md](quickstart.md)) on the
**DEBUG** build. Windows Smart App Control, which used to block it, is now off (research R4).

**Target Platform**: Windows x64, Skyrim SE / AE

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**: no change. `BeginDisabled(false)` / `EndDisabled()` cost the same as the conditional calls.

**Constraints**: the page shares ImGui state with every other SKSE Menu Framework mod, so its pairs must be balanced
every frame ([contract](contracts/menu-ui-balance.md)).

**Scale/Scope**:
- **Code**: one change in `src/UI/MENU.cpp`.
- **Docs**: `docs/TEST_MATRIX.md` (10.9, 10.10) and `.claude/CLAUDE.md` (UI note).

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| **I. Layered architecture** | PASS | The change stays in `UI/`. The page still only reads the capture state and requests captures through `CaptureState`. No `MagicState` mutation |
| **II. Main-thread state machine** | PASS (N/A) | No `MagicState` state or flags |
| **III. Never strand the player** | PASS (N/A) | No cast, equip or session path |
| **IV. Dual-cast sync** | PASS (N/A) | No cast pipeline change |
| **V. Opt-in & compatibility** | PASS | A bug fix with no patch flag: it restores the intended look and adds no behavior. No INI or persisted-data change. It improves coexistence: the plugin no longer corrupts the shared SKSE menu other mods draw into |
| **VI. Observable & verified** | PASS | Drawing is not a state transition, so no new log line (R4). TEST_MATRIX gains 10.9 and 10.10, and 10.7 and 6.12 are the regression cases (R5). Validation on the DEBUG build, now loadable |

**Post-design re-check (after Phase 1)**: unchanged. The data model adds no state, and the contract only states the
pair rule the rest of the page already follows.

## Project Structure

### Documentation (this feature)

```text
specs/006-fix-menu-capture-transparency/
├── plan.md                     # This file
├── research.md                 # Phase 0: R1–R5
├── data-model.md               # Phase 1: no new state; invariant and capture states
├── quickstart.md               # Phase 1: in-game validation
├── contracts/
│   └── menu-ui-balance.md      # Phase 1: pair rule for the SKSE menu page
├── checklists/
│   └── requirements.md         # Spec quality checklist
└── tasks.md                    # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
src/UI/MENU.cpp          # DrawKeyValueCapture: unconditional BeginDisabled(g_fieldCapture.active) … EndDisabled()
docs/TEST_MATRIX.md      # 10.9, 10.10
.claude/CLAUDE.md        # UI: SKSE menu pair rule (contract §1)
```

**Structure Decision**: a single SKSE plugin project. The fix is confined to the settings page drawer, where the
defect is. No new files.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **VI**: in-game validation on the release DLL instead of a DEBUG build (**approved by the user, 2026-11-20**) | The DEBUG DLL loads but faults inside `Hooks::Install_Hooks` at start-up (`fatal error occurred while loading plugin`), a DEBUG-only defect unrelated to this spec. The release build with `#define DEBUG` in `PCH.h` writes the same logs | Fixing the DEBUG crash first: it is a separate defect, with no root cause yet, and it would block a 6-line UI fix. Approved per spec for 002–006 |
