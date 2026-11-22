# Contract: the debug logging switch and what each build writes (research R1, R2, R5)

## §1. The switch

| Item | Rule |
|---|---|
| **Name** | `MAGIC_DEBUG_LOGGING`, a preprocessor definition. The generic name `DEBUG` is no longer used |
| **Who sets it** | Only `CMakeLists.txt`, on the plugin target, for the `Debug` configuration: `target_compile_definitions(IntegratedMagic PRIVATE "$<$<CONFIG:Debug>:MAGIC_DEBUG_LOGGING>")` |
| **Scope** | `PRIVATE` to the plugin target. CommonLib (its own target) and the vcpkg libraries are built without it (FR-007) |
| **Source files** | No source file defines it or `DEBUG`. `include/PCH.h` stops the build with `#error` if `DEBUG` is defined, so the old manual line cannot come back silently |
| **Readers** | `include/PCH.h` (`MAGIC_DEBUG_LOG`), and the 11 debug-only blocks, which test `#ifdef MAGIC_DEBUG_LOGGING` |

| Preset | `CMAKE_BUILD_TYPE` | `MAGIC_DEBUG_LOGGING` | `MAGIC_DEBUG_LOG(...)` | Debug-only blocks |
|---|---|---|---|---|
| `debug` | `Debug` | defined | `spdlog::info(...)` | compiled |
| `release` | `Release` | not defined | compiled out, see §2 | removed |

## §2. `MAGIC_DEBUG_LOG` with the switch off

- **No code**: the call produces no code and no string in the release DLL (Constitution VI).
- **No new warnings** (FR-006): the release build has no more warnings than the debug build.
  - The macro stays `((void)0)`.
  - A value or parameter used only by debug log calls is marked `[[maybe_unused]]`. There are 4 such sites today
    (research R2).
- **Arguments**: they are never evaluated in the release build, as today with the line commented out. No call site may
  rely on a side effect inside the arguments.

## §3. Lines each build writes to `IntegratedMagic.log`

**Always-on lines** (every build): the direct `spdlog::info` / `warn` / `error` calls, 38 today.
- **info** (13):
  - `Logger iniciado.`;
  - `[StyleConfig] styles.ini carregado.`, `styles.ini não encontrado — usando defaults.`, `styles.ini salvo.`;
  - `[Hooks] <Name> installed` for the 6 load-time hooks, and `MenuInputShieldHook installed (...)` on the first
    capture;
  - `[Hooks] hook space: ...`;
  - `[EquipSink] TESEquipEvent sink registered.`.
- **warn / error** (25): only when something fails.

**Debug lines** (debug build only): every `MAGIC_DEBUG_LOG` line, 278 call sites today. Among others, these appear in
any normal session:
- `[Hooks] D3DInitHook: ImGui HUD context initialized`;
- `[TextureManager] Loaded ...`, `[HUD] FontLoader::...`;
- `[Input] LoadHotkeyCache: ...`, `[SaveLoad] ...`, `[SaveSpellDB] LoadFromDisk: ...`, `[SpellSystem] ...`;
- the `[Startup]` breadcrumbs of [load-fault-report.md](load-fault-report.md).

**The check** (SC-002): in a release session, every info line is one of the 13 above. Any other info line is a debug
line, and fails the check.
