# Data Model: The build type decides whether debug logging is compiled in

No setting, INI key, saved data or runtime state of the plugin changes (spec FR-010). What changes is build
configuration, plus two debug-only diagnostics.

## Build configuration

| Item | Where | Values |
|---|---|---|
| Build type | `CMakePresets.json` (`CMAKE_BUILD_TYPE`) | `debug` → `Debug`, `release` → `Release`. Unchanged |
| `MAGIC_DEBUG_LOGGING` | `CMakeLists.txt`, `target_compile_definitions` on `IntegratedMagic`, `PRIVATE` | Defined for `Debug` only ([contract §1](contracts/build-switch.md)) |
| `DEBUG` | nowhere | Must stay undefined. `include/PCH.h` fails the build if it is defined |
| Post-build copy | `CMakeLists.txt` (unchanged) | Every build copies its DLL to `SKYRIM_MODS_FOLDER\IntegratedMagic\SKSE\Plugins`. The debug build also copies its PDB, which the fault report reads |

**Reconfiguring**: each preset has its own folder (`build/debug`, `build/release`). A folder configured before this
change picks up the new definition on its next configure. CMake re-runs it by itself, because `CMakeLists.txt`
changed.

## Debug-only diagnostics (`src/plugin.cpp`, `src/Hooks.cpp`)

| Item | Kind | Lifetime |
|---|---|---|
| Breadcrumbs | `MAGIC_DEBUG_LOG` lines ([load-fault-report.md §1](contracts/load-fault-report.md)) | Written on each start |
| `g_loadFaultReported` | file-local `bool` in `plugin.cpp` | Set by the first fault report, so a second fault in the same load is not reported |
| The fault filter | a function returning `EXCEPTION_CONTINUE_SEARCH` | Runs only while SKSE is unwinding a fault out of `SKSEPluginLoad` |

## Code readers of the switch

| File | Use |
|---|---|
| `include/PCH.h` | `MAGIC_DEBUG_LOG` on or off; the `#error` guard for `DEBUG` |
| `src/Application/SpellSystemController.cpp` (2), `src/Domain/MagicStateLifecycle.cpp` (2), `src/Domain/MagicStatePump.cpp` (4), `src/Domain/MagicStateSlot.cpp` (1), `src/Input/ExclusiveTracker.cpp` (1), `src/Input/InputFilter.cpp` (1) | Debug-only blocks: `#ifdef DEBUG` becomes `#ifdef MAGIC_DEBUG_LOGGING` |
| `src/plugin.cpp`, `src/Hooks.cpp` | The diagnostics above |
