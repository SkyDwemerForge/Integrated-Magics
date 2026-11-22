# Implementation Plan: The build type decides whether debug logging is compiled in

**Branch**: `fix/debug-build-define` | **Date**: 2026-11-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/010-fix-debug-build-define/spec.md`

## Summary

Debug logging is switched today by one hand-edited line, `#define DEBUG` in `include/PCH.h`. The build presets have no
say. The debug build also faults at start-up, so every spec since 002 was validated on a release build with that
line left on.

**Approach**:
1. **The switch** (R1):
   - **Who sets it**: `CMakeLists.txt` defines `MAGIC_DEBUG_LOGGING` on the plugin target for the `Debug`
     configuration only.
   - **The rename**: `PCH.h` and the 11 debug-only blocks test the new name. The generic `DEBUG` is gone, and `PCH.h`
     stops the build with `#error` if it is defined.
2. **Release warnings** (R2): with the switch off, values computed only for a debug log become unused.
   - **The probe**: a release build with the line off gave 4 warnings, all in plugin code.
   - **The fix**: each of the 4 values gets `[[maybe_unused]]`, and `MAGIC_DEBUG_LOG` stays `((void)0)`.
3. **The debug build crash** (R3):
   - **Static review**: it narrowed the fault to the start of `SKSEPluginLoad`, but did not prove a cause.
   - **Evidence first**: debug-only breadcrumbs, and a fault report (exception code, module + RVA, and a symbolized
     stack from the PDB), around `SKSEPluginLoad`. One game run on the debug build names the faulting function.
   - **Then the fix**, chosen from the evidence (R3, hypotheses H1–H5), and recorded in research (FR-013).
4. **Ship with it**:
   - **Docs**: `.claude/CLAUDE.md` "Debug logging" (R4);
   - **Tests**: TEST_MATRIX 0.6 and 0.7 (R5);
   - **Validation**: on the debug build, as the constitution requires (R6).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive- /Zc:preprocessor`. CMake 3.21+
presets, with Ninja as the generator.

**Primary Dependencies**:
- **CommonLibSSE-NG 16.9.0** (`extern/CommonLibVR-ng`), built as its own target. Its only Debug-dependent code is
  `REL::Module::_instance`, which is not `constinit` in Debug, and the default log level in `SKSE::InitInfo` (R3).
- **dbghelp** (already imported by the DLL), for the fault report's symbols. It is used only in the debug build.

**Storage**: N/A. No settings, INI or persisted data changes (FR-010).

**Testing**: no automated harness. Build checks, plus in-game validation following [quickstart.md](quickstart.md):
the diagnosis run, then the debug and release sessions.

**Target Platform**: Windows x64, Skyrim SE / AE

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**:
- **Release**: no change. Debug code is compiled out, and the fault filter is not compiled in.
- **Debug**: the breadcrumbs add 10 log lines per start. The fault report costs nothing unless a fault happens.

**Constraints**:
- **Third-party code**: the switch must not reach CommonLib or the vcpkg libraries (FR-007). A crash cause in a
  third-party library is reported to the user before any change to it (spec Assumptions).
- **The release build's behavior**: unchanged, apart from the debug lines it no longer writes (FR-012).
- **SKSE's handling of a fault**: unchanged. The filter only logs (contract §3).

**Scale/Scope**:
- **Build**: one line in `CMakeLists.txt`.
- **Source**: `include/PCH.h`, the 11 `#ifdef` sites in 6 files, the 4 `[[maybe_unused]]` sites of R2, and the
  diagnostics in `src/plugin.cpp` and `src/Hooks.cpp`. The crash fix is not yet known.
- **Docs**: `.claude/CLAUDE.md`, `docs/TEST_MATRIX.md`, and the memory note on the DEBUG crash.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| **I. Layered architecture** | PASS | **Build switch**: build files and `PCH.h` only. **Diagnostics**: they live at the entry point (`plugin.cpp`) and in the hook installer (`Hooks.cpp`), with no game calls added to `Domain/`. **The crash fix**: it goes where the cause is, and the layer is re-checked after the diagnosis |
| **II. Main-thread state machine** | PASS | No `MagicState` change. The fault filter runs on the loading thread, only while a fault unwinds out of `SKSEPluginLoad` |
| **III. Never strand the player** | PASS (N/A) | No session or synthetic input path changes |
| **IV. Dual-cast sync** | PASS (N/A) | No change to `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt` or `SyntheticInput` |
| **V. Opt-in & compatibility** | PASS | **No patch flag**: this is a build fix. **Release**: same behavior, minus the debug lines (FR-012). **Settings**: no INI or saved data change |
| **VI. Observable & verified** | PASS, fixes a deviation | **The rule**: "debug logging MUST compile out of release builds" holds again (FR-003). **Validation**: on a DEBUG build again, once it loads (FR-008). **Tests**: TEST_MATRIX 0.6 and 0.7 (FR-011) |

**Merge gate 1** (both presets build with no new warnings): a release build with the switch off gave 4 warnings
(R2). The 4 attributes bring it back to 0 (FR-006).

**Post-design re-check (after Phase 1)**:
- **Unchanged, all pass.** The design adds one build definition, a rename, and debug-only diagnostics that never
  handle a fault.
- **Deviation removed**: specs 002–009 recorded validation on the release build as a Principle VI deviation. This spec
  ends it once the debug build loads.
- **Open by design**: the crash fix itself is chosen after the diagnosis run (R3). The tasks re-check Principles I and
  V against it before it is written.

## Project Structure

### Documentation (this feature)

```text
specs/010-fix-debug-build-define/
├── plan.md                       # This file
├── research.md                   # Phase 0: R1–R6
├── data-model.md                 # Phase 1: build configuration and the debug-only diagnostics
├── quickstart.md                 # Phase 1: build checks, diagnosis run, debug and release sessions
├── contracts/
│   ├── build-switch.md           # The switch, and which lines each build writes
│   └── load-fault-report.md      # Start-up breadcrumbs and the fault report
├── checklists/
│   └── requirements.md           # Spec quality checklist
└── tasks.md                      # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
CMakeLists.txt                          # MAGIC_DEBUG_LOGGING for the Debug configuration (R1)
include/PCH.h                           # remove #define DEBUG; MAGIC_DEBUG_LOG on MAGIC_DEBUG_LOGGING; #error on DEBUG (R1, R2)
src/Application/SpellSystemController.cpp  # #ifdef DEBUG -> MAGIC_DEBUG_LOGGING (2)
src/Domain/MagicStateLifecycle.cpp      # same (2)
src/Domain/MagicStatePump.cpp           # same (4)
src/Domain/MagicStateSlot.cpp           # same (1)
src/Input/ExclusiveTracker.cpp          # same (1)
src/Input/InputFilter.cpp               # same (1)
src/Adapters/Outbound/SyntheticInput.cpp   # [[maybe_unused]] selPower (R2)
src/Adapters/Outbound/MagicEquip.cpp    # [[maybe_unused]] rd (R2)
src/Input/ExclusiveTracker.cpp          # [[maybe_unused]] winner (R2), besides its #ifdef
src/UI/CaptureInputGuard.cpp            # [[maybe_unused]] now (R2)
src/plugin.cpp                          # [Startup] breadcrumbs; the load fault filter (R3)
src/Hooks.cpp                           # [Hooks] installing <Name> breadcrumbs (R3)
<to be found>                           # the crash fix, after the diagnosis run (R3)
docs/TEST_MATRIX.md                     # 0.6 (release clean), 0.7 (debug loads) (R5)
.claude/CLAUDE.md                       # "Debug logging": the switch, the diagnostics, debug-build validation (R4)
```

**Structure Decision**: a single SKSE plugin project. The switch belongs to the build files, because the build type is
what decides it. The diagnostics stay where start-up happens, in `plugin.cpp` and `Hooks.cpp`, and compile only into
the debug build. `CMakeLists.txt` globs `src/`, so no new file needs a build change.

## Complexity Tracking

No constitution violations. Validation moves back to the DEBUG build, so the release-build exception of specs 002–009
is not needed.

| Item | Why it is needed | Simpler alternative rejected because |
|---|---|---|
| Debug-only fault report (structured exception filter + dbghelp) | SKSE catches the fault and logs only "fatal error". The plugin's log stops at a line, with no code or address, and the cause is not known (FR-013) | **Breadcrumbs alone**: they name the step, not the instruction or the call stack. **A debugger attached by the user**: the plugin loads within the first second of the game, before attaching is practical |
