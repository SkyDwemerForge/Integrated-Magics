# Quickstart: validating the build switch and the debug build

In-game validation guide for spec 010. There is no automated harness (Constitution VI).

## §0. Prerequisites

- **Builds**: both presets, `debug` and `release`, built from the same checkout with no local edits.
- **Which DLL the game loads**: each build copies its DLL to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`,
  so the **last** build made is the one installed. The debug build also copies its PDB there.
- **Logs**: `IntegratedMagic.log` and `skse64.log`, both in `Documents\My Games\Skyrim Special Edition\SKSE`. Both are
  emptied at each game launch, so copy them before starting the game again.

## §1. No source edit between the builds (US1, SC-001)

1. From a clean checkout (`git status` empty), build `debug`, then `release`, then `debug` again.
   - **Expected**: each build succeeds. `git status` is still empty after each one.
2. Check that `include/PCH.h` has no `#define DEBUG`. Add the line by hand and build once.
   - **Expected**: the build stops with the `#error` message that names `MAGIC_DEBUG_LOGGING`. Remove the line again.

## §2. Diagnosis run (US2, FR-013): only while the debug build still fails

1. Build `debug` last, so it is installed. Start the game through SKSE.
2. **If a dialog appears** (for example "Debug Assertion Failed" or "Run-Time Check Failure"): copy its text (`Ctrl+C`
   copies a Windows message box), then press **Retry**. That turns the assertion into a fault the report can describe.
3. Send `IntegratedMagic.log` and `skse64.log`.
   - **Expected**: `skse64.log` shows the same "fatal error occurred while loading plugin". `IntegratedMagic.log` ends
     with the last `[Startup]` / `[Hooks] installing` breadcrumb, then the `[Startup] load fault:` lines
     ([contracts/load-fault-report.md](contracts/load-fault-report.md)).
   - **If there is no `load fault` line**: the process ended without an exception SKSE can catch. Report what you saw.

## §3. The debug build loads (US2, SC-005, SC-006)

1. Build `debug` last. Start the game through SKSE 3 times.
   - **Expected**: `skse64.log` loads `IntegratedMagic` with no error. `IntegratedMagic.log` has the 6
     `[Hooks] ... installed` lines and the `hook space` line, with no `load fault` line.
2. Load a save, open the SKSE menu on the plugin's page, and cast one slot (TEST_MATRIX section 0 and one case of
   section 1).
   - **Expected**: the log has the debug lines of each component (`[Input]`, `[State]`, `[FLOW]`, `[SpellSystem]`,
     `[UI]`), and the cast behaves as on the release build.

## §4. The release build ships clean (US3, SC-002)

1. Build `release` last. Start the game, load a save, open the SKSE menu, and cast one slot.
   - **Expected**: every info line in `IntegratedMagic.log` is one of the 13 always-on lines of
     [contracts/build-switch.md §3](contracts/build-switch.md). Warnings and errors may appear only if something
     failed. No `[Input]`, `[State]`, `[FLOW]`, `[SpellSystem]`, `[UI]`, `[SaveLoad]` or `[Startup]` line appears.
   - **Gameplay**: the cast behaves as before (FR-012).

Then tick the new TEST_MATRIX cases 0.6 and 0.7.

**Build order from now on**: the build made last is the one installed. Build `release` first and `debug` last to
validate a spec. Build `release` last to play or to publish.
