# Research: The build type decides whether debug logging is compiled in

Sources: `main` at `857f8ce`, CommonLibSSE-NG 16.9.0 (`extern/CommonLibVR-ng`), the Windows SDK 10.0.26100 headers,
the vcpkg headers of `build/release`, and a probe release build with the manual line commented out (R2).

## R1. How the build type sets the switch

**Facts**:
- **Today**: `include/PCH.h:12` holds `#define DEBUG`, placed after the CommonLib and spdlog includes.
  `MAGIC_DEBUG_LOG` (278 call sites in 26 files) and 11 `#ifdef DEBUG` blocks read it.
- **The presets**: they set only `CMAKE_BUILD_TYPE`. CMake's Debug flags are `/Zi /Ob0 /Od /RTC1`, the runtime is
  `/MDd`, which defines `_DEBUG`, and Release adds `NDEBUG`.
- **The generic name is read by other headers**: `TraceLoggingProvider.h` (`TLG_DEBUG`) and `wiautil.h` in the Windows
  SDK test `DEBUG`. The vcpkg headers, CommonLib, the SKSE Menu Framework header and Detours do not. No header
  defines it.
- **The plugin uses it** only in those `#ifdef` lines and in the `#define` itself.

**Decision**:
- **The definition**: `target_compile_definitions(IntegratedMagic PRIVATE "$<$<CONFIG:Debug>:MAGIC_DEBUG_LOGGING>")`
  in `CMakeLists.txt`.
- **The readers**: `PCH.h` and the 11 blocks test `MAGIC_DEBUG_LOGGING`. The `#define DEBUG` line is removed.
- **The guard**: `PCH.h` has `#ifdef DEBUG` → `#error "DEBUG is not the switch any more: the build type sets
  MAGIC_DEBUG_LOGGING (spec 010)"`. A line brought back by an old branch then fails the build, instead of silently
  turning off a merged `#ifdef DEBUG` block.

**Rationale**:
- **The build type decides**, with no source edit (FR-001, FR-002).
- **`PRIVATE`** keeps the definition off CommonLib, which is its own target, and off the vcpkg libraries (FR-007).
- **The project prefix**: SDK headers no longer react to the plugin's switch. A command-line `DEBUG` would reach every
  header in the plugin's translation units, CommonLib's and spdlog's included.
- **The generator expression** works for single-config Ninja and for multi-config generators alike.

**Alternatives considered**:
- **Derive it in `PCH.h` from `_DEBUG` or `NDEBUG`**: no build file change, but the switch would hang on the CRT
  choice and would not show in the build files. Rejected.
- **Keep the name `DEBUG`, set from CMake**: the smallest diff, but SDK headers read it. Rejected.
- **A CMake cache option** (logs on or off for any build type): it allows a release build with logs, which the user did
  not choose (spec Clarifications). Rejected for now. The generator expression can become an option later.

## R2. Release warnings with the switch off

**Probe**: a release build of `main` (`857f8ce`) with the manual line commented out, in `build/probe-release-nolog`,
with `SKYRIM_MODS_FOLDER` unset so nothing was copied to MO2.

**Result** (2026-11-21; configure and build exit 0, 657 s from an empty folder):
- **Warnings**: 4, all in plugin code. None came from CommonLib or the vcpkg libraries. Each is a value that only
  feeds a debug log:

  | Site | Warning | Value |
  |---|---|---|
  | `src/Adapters/Outbound/SyntheticInput.cpp:85` | C4189 | local `selPower` |
  | `src/Adapters/Outbound/MagicEquip.cpp:173` | C4189 | local `rd` |
  | `src/Input/ExclusiveTracker.cpp:109` | C4100 | parameter `winner` of `ConsumeHeldBackForCombo` |
  | `src/UI/CaptureInputGuard.cpp:86` | C4100 | parameter `now` of `TurnOff` |

- **Errors**: none. No call site needs a name that exists only in a debug-only block.
- **The baseline**: today's release build, with the line on, has 0 warnings (spec 009, T027). So the 4 are new, and
  merge gate 1 requires removing them.

**Decision**:
- **The macro**: `MAGIC_DEBUG_LOG(...)` stays `((void)0)` with the switch off. Its arguments are not compiled, so the
  release DLL has neither the calls nor their format strings.
- **The 4 sites**: they get `[[maybe_unused]]`, as `TurnOff`'s `why` parameter already has.

**Rationale**:
- **The fix is local**: 4 attributes fix the warnings.
- **New cases are caught**: merge gate 1 builds both presets for every spec.
- **Behavior**: reading those values has no side effect, so the release build behaves the same (FR-012).

**Alternatives considered**:
- **A macro that "uses" the arguments without evaluating them**, such as
  `((void)(false && (spdlog::info(__VA_ARGS__), true)))`. It would prevent future cases. But it compiles all 278 calls
  in the release build, slows it, and may leave the format strings in the DLL, which would need a binary check.
  Rejected.
- **Moving each value into the log call, or into a debug-only block**: it works, but changes more code than the
  attribute does. Rejected.

## R3. The debug build start-up crash

**Facts**:
- **SKSE**: `skse64.log` reports "disabled, fatal error occurred while loading plugin". SKSE writes that when an
  exception escapes `SKSEPlugin_Load`: it catches it, logs, and disables the plugin.
- **The plugin's log**: it ends at `[StyleConfig] styles.ini carregado.`, with no `[Hooks] ... installed` line.
  Between those two lines, `SKSEPluginLoad` runs:
  1. the end of `StyleConfig::Load`, including the `CSimpleIniA` destructor;
  2. `MessagingInterface::RegisterListener`;
  3. in `Install_Hooks`: `SKSE::GetTrampoline()` and `empty()`, then `PollInputDevicesHook::Install`. That is
     `EnsureHookSpace`, `RelocationID::address()` (the plugin's first Address Library lookup, which loads the
     database), the patch-safety decode, and the patch write.
- **The release build of the same code loads**, so the cause depends on the build type.
- **What differs in Debug**:
  - **Compiler**: `/Od /RTC1` and the `/MDd` debug CRT, with debug heap checks and STL checks
    (`_ITERATOR_DEBUG_LEVEL=2`). Incremental linking also routes function addresses through jump thunks.
  - **CommonLib**: `REL::Module::_instance` is `constinit` only under `NDEBUG` (`src/REL/Module.cpp`). In Debug it has
    a dynamic initializer, while `Module::_initialized` is a constant-initialized `atomic_bool`.
  - **CommonLib's log level**: `SKSE::InitInfo` defaults to `debug` instead of `info`. The plugin then replaces that
    logger with its own.
  - **CommonLib's patch safety**: it is on in both builds, so it is not a Debug difference.
- **Ruled out by the review**:
  - no namespace-scope object in the plugin resolves an address during static initialization. The `REL::Relocation`
    members are default-constructed, and the rest are function locals;
  - the `REL::RelocationID` / `VariantOffset` constructors are `constexpr` and read no module state.
- **Not yet known**: the faulting instruction. SKSE's message carries no code or address.

**Decision**: get evidence first, with one game run on the debug build.
- **Breadcrumbs** ([contract §1](contracts/load-fault-report.md)): a `MAGIC_DEBUG_LOG` line before each start-up step
  and each `Install()`. The logger flushes every line, so the last one names the step.
- **The fault report** ([contract §2](contracts/load-fault-report.md)):
  - **Where**: a `__try` / `__except` filter around the body of `SKSEPluginLoad`, in a small wrapper with no C++
    objects, as `__try` requires;
  - **What it logs**: the code, the module + RVA, the access type and target for an access violation, and a stack of
    up to 24 frames walked from the fault's context, resolved with dbghelp against the PDB the debug build copies next
    to the DLL;
  - **The result**: it returns `EXCEPTION_CONTINUE_SEARCH`, so SKSE still handles the fault.
- **Assertion dialogs**: if the user sees "Debug Assertion Failed" or "Run-Time Check Failure", pressing Retry raises a
  breakpoint exception, and the report names the assertion site (quickstart §2).

**Hypotheses, and what the report would show**:

| # | Hypothesis | Report | Fix direction |
|---|---|---|---|
| H1 | A debug STL or CRT check fails on a latent bug, such as out-of-range indexing or an invalid heap free. Release lets it pass silently | `breakpoint` after a dialog, with the stack in plugin code | Fix the bug. It is UB in release too |
| H2 | An address is resolved against an empty `REL::Module` (Debug-only dynamic initializer) | `access violation` at a small or odd target, with frames in REL or Trampoline | Find what calls `Module::get()` too early, in the plugin's code |
| H3 | A C++ exception is thrown only in Debug | `C++ exception`, with the stack at the throw | Fix the throwing call |
| H4 | Debug stack frames overflow the stack | `stack overflow` | Move the large locals off the stack |
| H5 | The fault is inside a third-party library | Frames in CommonLib, Detours or the menu framework | Report it to the user and ask before changing that library (spec Assumptions) |

**The diagnostics stay** after the fix. They exist only in the debug build, cost nothing until a fault happens, and
cover any later start-up fault (data-model).

**Alternatives considered**:
- **A vectored exception handler**: it sees every first-chance exception, including the benign ones raised by
  `OutputDebugString` (CommonLib's license notice, the `msvc_sink`). It needs filtering. Rejected: the `__except`
  filter sees only the fault that escapes.
- **Attaching Visual Studio to the game**: the plugin loads within the first second, which is hard to catch in time.
  It stays a fallback if the report shows nothing.
- **A fix from the static review alone**: no hypothesis has evidence yet. Rejected (FR-013).

## R4. Documentation

**Decision**:
- **`.claude/CLAUDE.md` "Debug logging"**:
  - the debug build has the debug logs and the release build does not;
  - the switch is `MAGIC_DEBUG_LOGGING` from `CMakeLists.txt`, never a source edit, and debug-only code tests
    `#ifdef MAGIC_DEBUG_LOGGING`;
  - specs are validated on the debug build;
  - the breadcrumbs and the fault report.
- **The DEBUG crash notes**: the memory note on the DEBUG-only crash is updated with the cause once it is fixed.
  Existing specs keep their history.
- **The constitution**: no change needed. The presets stay `debug` and `release`, and Principle VI already requires
  release builds without debug logs and DEBUG-build validation.

## R5. TEST_MATRIX cases

**Decision**: two cases in section 0, in Portuguese like the rest of the matrix.
- **0.6**: release build → `IntegratedMagic.log` has only the 13 always-on info lines
  ([contract §3](contracts/build-switch.md)), plus warnings or errors when something fails, after a session that
  covers start, save load, the SKSE menu and one cast.
- **0.7**: debug build → `skse64.log` loads the plugin with no error, the six `[Hooks] ... installed` lines and the
  `hook space` line are there, and the debug lines are present. Repeated in 3 starts (SC-005).

## R6. Validation build

**Decision**:
- **This spec**: the release session (quickstart §4) checks FR-003 and FR-012. The debug sessions check FR-008: the
  diagnosis run (§2) while it still fails, then §3.
- **Later specs**: validated on the debug build, built last so it is installed (quickstart, build order). The release
  build exception of specs 002–009 ends.

## R7. The cause of the debug build crash (FR-013)

**Evidence** (diagnosis run of 2026-11-21, 21:19, T010 build):

```text
[Startup] Install_Hooks
[Hooks] installing PollInputDevicesHook
[Startup] load fault: code=0xC0000005 (access violation) at IntegratedMagic.dll+0x7D735
[Startup] load fault: access violation, read at 0x0000000000CF966B
[Startup] load fault: frame 0: ... SKSE::Trampoline::write_branch<5>+0x25 (Trampoline.h:144)
[Startup] load fault: frame 1: ... SKSE::Trampoline::write_call<5>+0x81 (Trampoline.h:126)
[Startup] load fault: frame 3: ... Hook::stl::write_call<...PollInputDevicesHook...::Impl,5>+0xEE (HookUtil.hpp:59)
[Startup] load fault: frame 4: ... IntegratedMagic::Inbound::PollInputDevicesHook::Install+0x165 (PollInputDevicesHook.cpp:34)
```

`skse64.log`: `plugin IntegratedMagic.dll (... 01060001) disabled, fatal error occurred while loading plugin`.

**Reading**:
- **The faulting line**: `Trampoline.h:144` is the first read of the patch site, `*reinterpret_cast<std::int8_t*>(a_src)`.
- **The address read**: `0xCF966B` is the call site's offset alone. The game image is loaded at `0x140000000`, so
  `a_src` lacks the module base. `RelocationID::address()` is `Module::get().base() + offset`, so `base()` was 0,
  while `Module::_initialized` was `true` and the Address Library lookup still worked.
- **Why the base is 0**, from the static initializers listed out of the debug PDB with dbghelp
  (`scratchpad\list_dyn_init.py`, 754 symbols):
  1. **The trigger**: only four of the plugin's static objects reach the game at DLL load. They are namespace-scope
     `RE::BSFixedString`s: `kInstantAnim` (`src/Adapters/Outbound/MagicEquip.cpp:13`), and `kRightAttackEvent`,
     `kLeftAttackEvent` and `kShoutUserEvent` (`src/Adapters/Outbound/SyntheticInput.cpp:10–12`). The other
     initializers are strings, containers, a format facet, the menu framework's `key`, and CommonLib's
     `GRenderer::Cxform::Identity` constant.
  2. **The call into REL**: their constructor calls `ctor8`, which resolves `REL::Relocation{RELOCATION_ID(67819,
     69161)}` (`RE/B/BSFixedString.h:194`). That calls `Module::get()`, which fills `Module::_instance` with the
     right base and sets `_initialized`.
  3. **The wipe**: the plugin's objects are initialized before the CommonLib library's. In Debug,
     `REL::Module::_instance` has a dynamic initializer (`src/REL/Module.cpp:34–38`: `constinit` only under
     `NDEBUG`). It runs after step 2 and default-constructs the instance again, with `_base{0}`. `_initialized` is a
     separate, constant-initialized `atomic_bool`, so it stays `true`, and `get()` never reloads.
  4. **The fault**: every address resolved afterwards is a bare offset. The first patch write reads it.
- **Why release loads**: `Module::_instance` is `constinit` there, with no dynamic initializer, so nothing wipes the
  state step 2 set. The four strings were also built during DLL load in release, against the game's string pool,
  which happened to work.
- **The hypothesis**: H2 of R3, with the trigger in the plugin's own code. Nothing in `extern/` needs a change.

**Decision**:
- **The fix**: each game string is built on first use, in a function-local static, never at DLL load:
  - in `SyntheticInput.cpp`, inside the existing `RightAttackEvent()` and `LeftAttackEvent()` accessors, plus a
    file-local `ShoutUserEvent()`;
  - in `MagicEquip.cpp`, a file-local `InstantAnimVar()`.
- **A regression check**: the `[Startup] Install_Hooks` breadcrumb also logs the game module base, so a base of 0
  shows before the first patch.
- **The rule**, for `.claude/CLAUDE.md`: no namespace-scope or static-member object may call into the game at DLL load
  (`RE::BSFixedString`, `REL::Relocation`, `GetSingleton()`). Use a function-local static instead.

**Principles**:
- **I**: the fix stays in the outbound adapters, where game calls belong.
- **V and FR-012**: the strings and their values are the same, created at their first use (a synthetic input or an
  equip) instead of at DLL load. Release behavior does not change.
