---

description: "Task list for spec 010: the build type decides whether debug logging is compiled in"
---

# Tasks: The build type decides whether debug logging is compiled in

**Input**: design documents in `specs/010-fix-debug-build-define/`: [plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md), [contracts/build-switch.md](contracts/build-switch.md),
[contracts/load-fault-report.md](contracts/load-fault-report.md), [quickstart.md](quickstart.md).

**Tests**: no automated harness exists (Constitution VI). Validation is by build checks, plus in-game runs by the user,
marked **(in-game, user)** and run through the quickstart.

**Organization**:
- **US1** (the switch) comes first, because the diagnostics of US2 compile only under `MAGIC_DEBUG_LOGGING`.
- **US2** (the debug build loads) is a loop:
  1. instrument;
  2. one diagnosis run by the user;
  3. record the cause;
  4. fix;
  5. validate.
- **US3** (the release build ships clean) is checked last, on a release build installed last.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1, US2, US3, see spec.md)

## Conventions for every code task

- **Style**: identifiers, comments and log messages are in English. `.clang-format` applies (120 columns, 4-space
  indent).
- **The switch**: `MAGIC_DEBUG_LOGGING`, set only by `CMakeLists.txt`. Debug-only code tests
  `#ifdef MAGIC_DEBUG_LOGGING`. Never write `DEBUG` in plugin code again ([contract §1](contracts/build-switch.md)).
- **A value used only by debug log calls** gets `[[maybe_unused]]` (research R2).
- **Gameplay**: unchanged in both builds (FR-010, FR-012). Apart from the `#ifdef` renames and the 4 attributes, no
  change in `src/Domain`, `src/Input`, `src/Config` or `src/Persistence`, unless T014 places the crash there.
- **Third-party code**: no change in `extern/` without the user's approval (spec Assumptions, research R3 H5).
- **Build**: the scratchpad script
  `C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\build.ps1
  -Preset <debug|release> -LogFile <path>`. If it is gone, run `cmake --preset <p>` then `cmake --build build/<p>`.
  - **What gets installed**: each build copies its DLL to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`,
    and the debug build its PDB as well. So the build made **last** is the one the game loads.
  - **Order**: release first and debug last before a debug run, and the reverse before a release run.
  - **After each build**: compare the MO2 DLL's SHA-256 with `build\<preset>\IntegratedMagic.dll`. Leave the PDB in
    MO2 after a debug build, because the fault report reads it.
- **Warnings**: compiler warnings are counted from the build log (`warning C`). Both presets must show 0
  (merge gate 1).

---

## Phase 1: Setup

- [X] T001 Record the baseline in the implementation log of `specs/010-fix-debug-build-define/tasks.md`:
  - `git status` is clean on `fix/debug-build-define`;
  - `include/PCH.h:12` is `#define DEBUG`;
  - `grep -rnE "^\s*#\s*ifdef DEBUG\b" src` gives the 11 lines of data-model ("Code readers of the switch");
  - `MAGIC_DEBUG_LOG(` has 278 call sites in 26 files (280 matches with the 2 macro definitions in `include/PCH.h`);
  - the direct `spdlog` calls are 38, 13 of them info. Run
    `python -I C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\list_always_on.py D:\repositorios\Integrated-Magics`.
    It must list exactly the 13 info lines of [contract §3](contracts/build-switch.md).

---

## Phase 2: User Story 1 - Choosing the build type is enough (Priority: P1) 🎯 MVP

**Goal**: the `debug` preset compiles the debug logs in, and the `release` preset compiles them out, with no source
edit.

**Independent Test**: [quickstart.md](quickstart.md) §1. Build `debug`, `release` and `debug` from a clean checkout:
`git status` stays empty, both build with 0 warnings, and an added `#define DEBUG` fails the build.

- [X] T002 [US1] In `CMakeLists.txt`, right after `target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)`, add:
  ```cmake
  # Debug logging (MAGIC_DEBUG_LOG and the debug-only blocks) is compiled in for the Debug build only (spec 010).
  target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Debug>:MAGIC_DEBUG_LOGGING>")
  ```
  Keep it `PRIVATE`, so CommonLib and the vcpkg libraries never see it (FR-007).
- [X] T003 [US1] In `include/PCH.h`, replace lines 12–18 (`#define DEBUG` and the `#ifdef DEBUG` macro block) with:
  ```cpp
  // The build type sets MAGIC_DEBUG_LOGGING (CMakeLists.txt, Debug only). Never define it, or DEBUG, by hand (spec 010).
  #ifdef DEBUG
      #error "DEBUG is not the debug logging switch any more: the build type sets MAGIC_DEBUG_LOGGING (spec 010)"
  #endif

  #ifdef MAGIC_DEBUG_LOGGING
      #define MAGIC_DEBUG_LOG(...) spdlog::info(__VA_ARGS__)
  #else
      #define MAGIC_DEBUG_LOG(...) ((void)0)
  #endif
  ```
  The includes and `using namespace std::literals;` above stay as they are.
- [X] T004 [P] [US1] Replace `#ifdef DEBUG` with `#ifdef MAGIC_DEBUG_LOGGING` at the 11 lines of T001 (no `#else` or
  `#endif` mentions `DEBUG`):
  - `src/Application/SpellSystemController.cpp` 43, 56;
  - `src/Domain/MagicStateLifecycle.cpp` 55, 288;
  - `src/Domain/MagicStatePump.cpp` 105, 388, 951, 975;
  - `src/Domain/MagicStateSlot.cpp` 92;
  - `src/Input/ExclusiveTracker.cpp` 440;
  - `src/Input/InputFilter.cpp` 509.

  Then check that `grep -rnw DEBUG src include` shows only the two guard lines in `include/PCH.h`.
- [X] T005 [US1] After T004 (both edit `src/Input/ExclusiveTracker.cpp`), add `[[maybe_unused]]` at the 4 sites of
  research R2, and change nothing else on those lines:
  - `src/Adapters/Outbound/SyntheticInput.cpp:85`: the local `const RE::FormID selPower`;
  - `src/Adapters/Outbound/MagicEquip.cpp:173`: the local `const auto& rd`;
  - `src/Input/ExclusiveTracker.cpp:109`: the parameter `int winner` of `ConsumeHeldBackForCombo`;
  - `src/UI/CaptureInputGuard.cpp:86`: the parameter `Clock::time_point now` of `TurnOff`, written like its `why`.
- [X] T006 [US1] Build `release`, then `debug` (depends on T002–T005).
  - **Required**: 0 errors and 0 warnings in each build log, and `git status` showing only the T002–T005 edits.
  - **Then the guard** (quickstart §1.2): add `#define DEBUG` as line 11 of `include/PCH.h`, and build `debug`. The
    build must stop with the T003 `#error` text. Remove the line, and build `debug` again so the debug DLL is
    installed.
  - Record the warning counts and the hash check in the log.

**Checkpoint**: US1 is done when T006 passes. The rest of the spec builds on it.

---

## Phase 3: User Story 2 - The debug build loads in the game (Priority: P1)

**Goal**: the debug build loads with no SKSE error and every hook installed, so specs are validated on it again.

**Independent Test**: [quickstart.md](quickstart.md) §3. Three starts on the debug build, then a save, the SKSE menu
page and one cast.

### Instrument (contract [load-fault-report.md](contracts/load-fault-report.md))

- [X] T007 [P] [US2] In `src/Hooks.cpp`, `Install_Hooks()`, add a breadcrumb before each of the six `Install()` calls:
  `MAGIC_DEBUG_LOG("[Hooks] installing PollInputDevicesHook");`, and the same for `PlayerAnimGraphHook`,
  `D3DInitHook`, `DXGIPresentHook`, `MagicCasterInterruptHook` and `MagicCasterStartCastHook` (contract §1).
- [X] T008 [US2] In `src/plugin.cpp`, split the load and add the breadcrumbs (contract §1).
  - **The split**: move the body of `SKSE_PLUGIN_LOAD` into `bool LoadPlugin(const SKSE::LoadInterface* skse)`, in
    the anonymous namespace, unchanged.
  - **The breadcrumbs** inside it: `MAGIC_DEBUG_LOG("[Startup] StyleConfig::Load");` after `InitializeLogger()`, then
    `"[Startup] RegisterListener"` before the messaging block, `"[Startup] Install_Hooks"` before
    `Install_Hooks()`, and `"[Startup] SKSEPluginLoad done"` before `return true;`.
- [X] T009 [US2] In `src/plugin.cpp`, add the fault report, inside one `#ifdef MAGIC_DEBUG_LOGGING` block (contract
  §2, §3; depends on T008).
  - **Includes**: `<Windows.h>`, then `<DbgHelp.h>`, `<filesystem>` and `<string>`, all inside the block.
    `dbghelp.lib` is already linked through CommonLib (`PUBLIC`).
  - **`bool g_loadFaultReported = false;`**: report once per load.
  - **`std::string DescribeAddress(DWORD64 addr)`**:
    - the module: `GetModuleHandleExW(FROM_ADDRESS | UNCHANGED_REFCOUNT)`, then `GetModuleFileNameW`, keeping the
      file name only, as `DescribeCallTarget` in `src/Adapters/Inbound/MenuInputShieldHook.cpp` does;
    - then `+0x<rva>`;
    - when dbghelp resolves it, ` <symbol>+0x<disp> (<file>:<line>)`, from `SymFromAddr` with a `SYMBOL_INFO` buffer of
      `MAX_SYM_NAME` and `SymGetLineFromAddr64`.
  - **`LONG ReportLoadFault(EXCEPTION_POINTERS* ep)`**:
    - if `g_loadFaultReported`, return `EXCEPTION_CONTINUE_SEARCH`. Otherwise set it.
    - **The code name**: `0xC0000005` access violation, `0x80000003` breakpoint, `0xC00000FD` stack overflow,
      `0xE06D7363` C++ exception, `0xC000001D` illegal instruction, anything else `other`.
    - **The first line**: `[Startup] load fault: code=0x{:08X} ({}) at {}`, with `DescribeAddress` of
      `ExceptionAddress`.
    - **Access violations**: `[Startup] load fault: access violation, {} at 0x{:016X}`, where
      `ExceptionInformation[0]` 0 is `read`, 1 `write` and 8 `execute`, and `[1]` is the target.
    - **Stack overflow**: stop here and return `EXCEPTION_CONTINUE_SEARCH`, because there is no stack left for
      dbghelp.
    - **The stack**:
      - `SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES)`;
      - `SymInitialize(GetCurrentProcess(), <the plugin DLL's folder>, TRUE)`, the folder taken from the module of
        `&ReportLoadFault`;
      - copy `*ep->ContextRecord`, and seed a `STACKFRAME64` from `Rip`, `Rsp` and `Rbp` (`AddrModeFlat`);
      - up to 24 frames with `StackWalk64(IMAGE_FILE_MACHINE_AMD64, ..., SymFunctionTableAccess64,
        SymGetModuleBase64, nullptr)`. Log each as `[Startup] load fault: frame {}: {}` with `DescribeAddress`;
      - `SymCleanup`.
    - **Return** `EXCEPTION_CONTINUE_SEARCH`. Never `EXCEPTION_EXECUTE_HANDLER`.
  - **`bool LoadPluginWithFaultReport(const SKSE::LoadInterface* skse)`**:
    - its body is only `__try { return LoadPlugin(skse); } __except (ReportLoadFault(GetExceptionInformation()))
      { return false; }`, with no C++ object in the function (C2712);
    - the handler is unreachable, because the filter never accepts the fault, so SKSE handles it as today.
  - **`SKSE_PLUGIN_LOAD`**: returns `LoadPluginWithFaultReport(skse)` under `MAGIC_DEBUG_LOGGING`, and
    `LoadPlugin(skse)` otherwise.
- [X] T010 [US2] Build `release`, then `debug` last (depends on T007–T009), as in T006.
  - **Required**: 0 errors and 0 warnings.
  - **MO2**: it holds the debug DLL and its PDB, and the DLL hash matches `build\debug\IntegratedMagic.dll`.
  - **The release DLL**: it has no fault report. Run `dumpbin /imports build\release\IntegratedMagic.dll`: no
    `StackWalk64` and no `SymFromAddr`.

### Diagnose and fix (research R3)

- [X] T011 [US2] **(in-game, user)** Run [quickstart.md](quickstart.md) §2 on the T010 build, then send
  `IntegratedMagic.log` and `skse64.log`, plus the text of any dialog.
- [X] T012 [US2] Read the T011 logs. If `skse64.log` shows the plugin loaded and the log has `[Startup] SKSEPluginLoad
  done` and the hook lines, the crash is gone: record why in research.md, and go to T016.
- [X] T013 [US2] If there is a `[Startup] load fault:` report, match it to a hypothesis of research R3 (H1–H5).
  - If there is no report, add a note to `specs/010-fix-debug-build-define/research.md`, propose the next step (for
    example a debugger attach, R3 alternatives) and stop for the user.
  - If it is H5 (third-party code), stop and ask the user before any change in `extern/`.
- [X] T014 [US2] Record the cause in `specs/010-fix-debug-build-define/research.md` as a new section, "R7. The cause of
  the debug build crash" (FR-013), with:
  - the report lines;
  - the faulting function, file and line;
  - why only the debug build fails;
  - the fix chosen.

  Then re-check Constitution Principles I and V for the fix (plan, post-design re-check).
- [X] T015 [US2] Apply the fix in the file R7 names.
  - **Release behavior** stays the same (FR-012).
  - **A latent bug** (H1) is fixed for both builds, and R7 says so.
  - Keep the breadcrumbs and the fault report (research R3).
- [X] T016 [US2] Build `release` (`build/release`), then `debug` (`build/debug`) last, with the checks of T010.
- [X] T017 [US2] **(in-game, user)** Run [quickstart.md](quickstart.md) §3 on the T016 build.
  - **Required**: 3 starts out of 3 with no SKSE error, the 6 `installed` lines and the `hook space` line, and no
    `load fault` line (SC-005). Then a save, the plugin's SKSE menu page and one slot cast, with the debug lines of
    `[Input]`, `[State]`, `[FLOW]`, `[SpellSystem]` and `[UI]` (SC-003, SC-006).
  - **If a fault remains**: repeat T013–T016 with the new report.

---

## Phase 4: User Story 3 - The release build ships clean (Priority: P2)

**Goal**: the release build writes only the always-on lines, and holds no debug code.

**Independent Test**: [quickstart.md](quickstart.md) §4 on a release build installed last.

- [X] T018 [P] [US3] In `docs/TEST_MATRIX.md`, section 0, add after 0.5, in Portuguese like the rest (research R5):
  - **0.6**: release build, after a session covering start, save load, the SKSE menu and one cast. Every info line
    in `IntegratedMagic.log` is one of the 13 always-on lines of `specs/010-fix-debug-build-define/contracts/build-switch.md`
    §3. Warnings and errors appear only if something failed.
  - **0.7**: debug build, in 3 starts. `skse64.log` loads `IntegratedMagic` with no error. The log has the 6
    `[Hooks] ... installed` lines, the `hook space` line and debug lines such as `[Input]`, and no
    `[Startup] load fault:`.

  Leave both unticked.
- [X] T019 [US3] Check the release DLL for debug strings (FR-003; after T016, on `build\release\IntegratedMagic.dll`):
  - each of these debug formats must have 0 matches with `grep -c -a`: `CaptureGuard: off after`,
    `HoldBack: {} consumed by slot`, `LoadHotkeyCache: slot=` and `[Startup]`;
  - the always-on `hook space: ` must have 1 or more.

  Record the counts in the log.
- [ ] T020 [US3] Build `debug`, then `release` last, so the release DLL is installed (hash check).
- [ ] T021 [US3] **(in-game, user)** Run [quickstart.md](quickstart.md) §4 on the T020 build.
  - **Required**: every info line in the log is one of the 13 always-on lines (SC-002), and the cast behaves as
    before (FR-012).
  - **Then** tick TEST_MATRIX 0.6, and 0.7 from T017.

---

## Phase 5: Polish & cross-cutting concerns

- [X] T022 [P] In `.claude/CLAUDE.md`, rewrite "Debug logging" (research R4).
  - **The switch**:
    - debug logs are compiled into the `debug` build only, because `CMakeLists.txt` defines `MAGIC_DEBUG_LOGGING` for
      the Debug configuration;
    - debug-only code tests `#ifdef MAGIC_DEBUG_LOGGING`;
    - never `#define DEBUG` (`PCH.h` stops the build);
    - a value used only by debug logs gets `[[maybe_unused]]`.
  - **Validation**: specs are validated on the debug build, built last.
  - **The start-up diagnostics**: the `[Startup]` and `[Hooks] installing` breadcrumbs, and the
    `[Startup] load fault:` report.
  - **Common tasks**: add the row "The debug build fails to load, or a start-up fault" → `src/plugin.cpp` (the fault
    report), `src/Hooks.cpp`. Check the `[Startup] load fault:` lines first.
- [X] T023 [P] Update the memory note
  `C:\Users\igora\.claude\projects\d--repositorios-Integrated-Magics\memory\smart-app-control-blocks-plugin-dll.md`
  with the cause of R7 and the fix: the DEBUG-only crash is fixed, and debug builds now load. Keep its line in
  `MEMORY.md` accurate.
- [X] T024 Run clang-format in dry-run mode on the changed line ranges only, and fix any violation. The binary is
  `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe --dry-run`, with
  `--lines` passed through `@()` in PowerShell. If a fix changes code, rebuild as in T020.
- [X] T025 Final scope check:
  - `git diff main -- src/Domain src/Input src/Application src/Adapters src/UI` must show only the `#ifdef` renames,
    the 4 `[[maybe_unused]]`, and the T015 fix if it lies there;
  - `git diff --stat main -- src/Config include/Config src/Persistence extern SKSE` must be empty, unless the user
    approved a change in `extern/` (T013);
  - the MO2 DLL hash must match the build made last.
- [X] T026 Pre-commit review:
  - set `**Status**` in `specs/010-fix-debug-build-define/spec.md`;
  - complete the implementation log;
  - review `git diff`.

  Commit and push only when the user asks.

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1** (T001): first.
- **Phase 2 (US1)**: T002, T003 and T004 can run together. T005 follows T004. Then T006.
- **Phase 3 (US2)**: after T006.
  - T007 and T008 can run together, and T009 follows T008;
  - then T010, T011 (user), T012 → T013 → T014 → T015 → T016, and T017 (user);
  - T013–T016 repeat until T017 passes.
- **Phase 4 (US3)**: T018 at any time. T019 after T016. Then T020 and T021 (user), last of the in-game runs.
- **Phase 5**: T022 at any time. T023 after T014. Then T024 → T025 → T026, last.

### User story dependencies

- **US1 (P1)**: the MVP. It depends only on Phase 1.
- **US2 (P1)**: depends on US1, because the diagnostics compile under `MAGIC_DEBUG_LOGGING`.
- **US3 (P2)**: its static check (T019) and its session (T021) need the final code, so they come after US2's fix.
  T018 is independent.

### Same-file sequencing

| File | Order |
|---|---|
| `CMakeLists.txt` | T002 |
| `include/PCH.h` | T003 → T006 (temporary guard test, reverted) |
| `src/Input/ExclusiveTracker.cpp` | T004 → T005 (different lines) |
| `src/plugin.cpp` | T008 → T009 → T015 (if the fix lies there) |
| `src/Hooks.cpp` | T007 → T015 (if the fix lies there) |
| `specs/010-fix-debug-build-define/research.md` | T012/T014 |
| `docs/TEST_MATRIX.md` | T018 → T021 (ticks) |

---

## Parallel Example

```text
# Phase 2, together:
T002  CMakeLists.txt        (the definition)
T003  include/PCH.h         (macro + guard)
T004  the 11 #ifdef renames (6 files)
# then T005 (the 4 [[maybe_unused]] sites; ExclusiveTracker.cpp is shared with T004)

# Phase 3, instrumenting:
T007  src/Hooks.cpp    (breadcrumbs)
T008  src/plugin.cpp   (split + breadcrumbs), then T009 (fault report)

# Any time:
T018  docs/TEST_MATRIX.md   T022  .claude/CLAUDE.md
```

---

## Implementation Strategy

### MVP first (US1)

1. T001–T006: the switch, the rename, the warnings, and both builds.
2. **Stop and validate**: no source edit between builds, 0 warnings, and the `#error` guard works.

### Incremental delivery

1. **US2** (T007–T017): instrument, one diagnosis run, record the cause, fix, three clean starts.
2. **US3** (T018–T021): no debug strings in the release DLL, then a release session.
3. **Polish** (T022–T026).

## Notes

- Commit or push only when the user asks.
- **The diagnosis run needs the debug DLL installed**: build `debug` last (T010). The game then fails to load the
  plugin until the fix, so the user plays the release build in between only after rebuilding `release` last.
- **If the report names CommonLib's `REL` or `Trampoline` code** (H2 or H5): do not patch `extern/` without the user's
  approval.

## Implementation log

### 2026-11-21: the switch, the diagnostics and both builds (T001–T010)

- **T001** (baseline):
  - `git status`: clean on `fix/debug-build-define`, apart from this spec folder (untracked);
  - `include/PCH.h:12` was `#define DEBUG`;
  - 11 `#ifdef DEBUG` lines, as listed in data-model;
  - `MAGIC_DEBUG_LOG(`: 280 matches, but 2 are the macro definitions in `PCH.h`. So 278 call sites in 26 files. The
    spec, research, contract and this file were corrected;
  - `list_always_on.py`: 38 direct `spdlog` calls, 13 of them info, matching contract §3.
- **T002**: `CMakeLists.txt` defines `MAGIC_DEBUG_LOGGING` with `$<$<CONFIG:Debug>:...>`, `PRIVATE`, after
  `target_compile_features`.
- **T003**: in `include/PCH.h`, `#define DEBUG` is gone. There is an `#ifdef DEBUG` → `#error` guard, and the macro
  reads `MAGIC_DEBUG_LOGGING`.
- **T004**: the 11 lines are renamed. `grep -rnw DEBUG src include` now finds only the guard in `PCH.h`.
  - `sed` turned the 6 files' CRLF line endings into LF in the working copy. They were converted back with
    `scratchpad\to_crlf.py`, and the diff is 11 lines.
- **T005**: `[[maybe_unused]]` at the 4 sites. clang-format re-aligned the `selPower` ternary
  (`SyntheticInput.cpp:85–87`). Line 88 of that file is an existing over-long log line, already flagged on `main`, and
  was left as it was.
- **T006**:
  - **The builds**: release, then debug. Both had 0 warnings and 0 errors, against 4 warnings in the R2 probe.
  - **`compile_commands.json`**: in release, no file has `MAGIC_DEBUG_LOGGING`. In debug, every file of the plugin
    target has it: the 56 `src/` files, the PCH, the generated `__IntegratedMagicPlugin.cpp`, and the 5 Detours
    sources `cmake/detours.cmake` builds into the plugin target. Detours never reads the name. None of CommonLib's
    files, or `hde64.c`, has it (FR-007).
  - **The guard**: `#define DEBUG` added to `PCH.h` stopped the debug build with
    `PCH.h(14): fatal error C1189: #error: "DEBUG is not the debug logging switch any more: ..."`. The line was
    removed, and debug was rebuilt with 0 warnings.
- **T007**: `src/Hooks.cpp` writes `[Hooks] installing <Name>` before each of the 6 `Install()` calls.
- **T008**: `src/plugin.cpp`:
  - the body of `SKSE_PLUGIN_LOAD` moved into `LoadPlugin`, unchanged;
  - `[Startup]` breadcrumbs before `StyleConfig::Load`, `RegisterListener` and `Install_Hooks`, and before
    `return true`.
- **T009**: the fault report in `src/plugin.cpp`, inside `#ifdef MAGIC_DEBUG_LOGGING`: `FaultName`, `BaseName`,
  `DescribeModule`, `DescribeAddress`, `PluginFolder`, `ReportLoadFault` and `LoadPluginWithFaultReport`.
  - **Departures from the task text**:
    - plain `char` paths (`GetModuleFileNameA`, `strrchr`) instead of `std::filesystem`, so nothing in the filter can
      throw;
    - `SymCleanup` runs only if this code's own `SymInitialize` succeeded, so dbghelp state set up by another module
      (a crash logger, for example) is not torn down.
  - **Include order**: Google's `IncludeBlocks: Regroup` sorted `<DbgHelp.h>` above `<Windows.h>`, which would break
    the build. The two lines are wrapped in `// clang-format off` / `on`.
- **T010**:
  - **The builds**: release, then debug last. Both had 0 warnings and 0 errors.
  - **MO2**: it holds the debug DLL and its PDB, and the DLL's SHA-256 matches `build\debug` (`535713BD…`).
  - **`dumpbin /imports`**: the debug DLL imports `StackWalk64`, `SymFromAddr` and `SymInitialize`, and the release
    DLL none of them.
  - **Strings**: `[Startup]` is in the debug DLL and not in the release DLL.

**Next**: T011, in game, by the user, on the T010 build (quickstart §2).

### 2026-11-21: the diagnosis run, the cause and the fix (T011–T016, T018, T019, T022)

- **T011** (user, 21:19, T010 build): "O mesmo erro aconteceu". No dialog was mentioned.
  - **`skse64.log`**: `disabled, fatal error occurred while loading plugin`.
  - **`IntegratedMagic.log`**: it ends at `[Hooks] installing PollInputDevicesHook`, then the report. It shows
    `code=0xC0000005 (access violation)` at `SKSE::Trampoline::write_branch<5>+0x25 (Trampoline.h:144)`,
    `read at 0x0000000000CF966B`, and 16 frames resolved down to `SKSEPlugin_Load` and the SKSE and game frames.
- **T012**: the crash is not gone.
- **T013**: H2. The address read is the call site's offset with no module base. The trigger is in plugin code, so no
  change in `extern/` is needed.
- **T014**: the cause is recorded in research R7. Steps:
  1. the debug PDB's static initializers (`scratchpad\list_dyn_init.py`, 754 symbols) show four namespace-scope
     `RE::BSFixedString`s. Their constructor resolves `REL::Relocation` and fills `REL::Module::_instance` at DLL
     load;
  2. CommonLib's Debug-only dynamic initializer of that instance then resets `_base` to 0, while `_initialized`
     stays `true`.
- **T015**: the fix.
  - **`src/Adapters/Outbound/SyntheticInput.cpp`**: `RightAttackEvent()` and `LeftAttackEvent()` hold their strings
    as function-local statics, plus a file-local `ShoutUserEvent()`.
  - **`src/Adapters/Outbound/MagicEquip.cpp`**: `InstantAnimVar()`.
  - **The breadcrumb**: `[Startup] Install_Hooks` now logs `game module base=0x<hex>`. Contract §1 was updated to
    match.
  - **clang-format**: clean on the changed ranges.
- **T016**:
  - **The builds**: release, then debug last. Both had 0 warnings and 0 errors.
  - **The static initializers**: the debug PDB now lists 750, and none is a `BSFixedString` or one of the four
    names.
  - **MO2**: it holds the debug DLL and its PDB, and the DLL's SHA-256 matches `build\debug` (`B7990C32…`).
- **T019**:
  - **The release DLL**: it has 0 matches for `CaptureGuard: off after`, `HoldBack: {} consumed by slot`,
    `LoadHotkeyCache: slot=`, `[Startup]` and `load fault:`, and 1 each for the always-on `hook space: ` and
    `Logger iniciado.`;
  - **The debug DLL**: it has 1 match for each of the debug strings.
- **T018**: TEST_MATRIX 0.6 and 0.7 added, unticked.
- **T022**: in `.claude/CLAUDE.md`:
  - "Debug logging" now covers the switch, the `#error` guard, the `[[maybe_unused]]` rule, the always-on lines,
    validation on the debug build, and the start-up diagnostics;
  - a new Common tasks row covers start-up faults;
  - a new invariant: no global initializer may call into the game.

**Next**: T017, in game, by the user, on the T016 build (quickstart §3).

### 2026-11-21: validation and closing (T017, T023–T026)

- **T017** (user, T016 build): "funcionou".
  - **`skse64.log`** (23:33): `plugin IntegratedMagic.dll (... 01060001) loaded correctly`.
  - **`IntegratedMagic.log`** (last start, 23:03–23:36):
    - `[Startup] Install_Hooks (game module base=0x7FF778CF0000)`;
    - the 6 `installing` / `installed` pairs, `hook space: 42 / 128 B used (33%)` and `[Startup] SKSEPluginLoad done`;
    - no `load fault` line;
    - debug lines `[Input]` ×32, `[SpellSystem]` ×302 and `[SaveLoad]` ×9.
  - **Not in the log**:
    - **3 starts**: only the last start is kept, because the log is emptied at each launch. The count is reported by
      the user;
    - **a slot cast**: there are no `[State]` or `[FLOW]` lines, so it is not confirmed (SC-006).
  - **TEST_MATRIX**: 0.7 ticked.
- **T020 / T021**: not run. The release session (quickstart §4, TEST_MATRIX 0.6) stays open. The release DLL's static
  check (T019) passed.
- **T023**: the memory note on the DEBUG crash was updated with the cause and the fix, and so was its `MEMORY.md`
  line.
- **T024**: clang-format was run on every changed range in T005, T009 and T015, and was clean. Line 88 of
  `SyntheticInput.cpp` is an over-long log line that was already flagged on `main`, and it was left as it was.
- **T025**:
  - nothing changed in `src/Config`, `include/Config`, `src/Persistence`, `extern` or `SKSE`;
  - the `src/` diff has only the 11 `#ifdef` renames, the 4 `[[maybe_unused]]`, the R7 fix in `MagicEquip.cpp` and
    `SyntheticInput.cpp`, and the diagnostics in `plugin.cpp` and `Hooks.cpp`;
  - the MO2 DLL matches `build\debug` (`B7990C32…`).
- **T026**: spec `Status` set to Implemented, and `git diff` reviewed.
