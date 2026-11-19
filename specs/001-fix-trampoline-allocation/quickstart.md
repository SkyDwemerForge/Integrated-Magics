# Quickstart: Validate the hook-space fix

How to prove the fix works end to end. Expected log lines are defined in
[contracts/hook-installation.md §4](contracts/hook-installation.md#4-startup-log-lines-diagnostic-interface).

## Prerequisites

- Visual Studio 2026 (D:), vcpkg baseline `00c5775`, CommonLibSSE-NG v16.9.0 (already set up on this branch).
- Skyrim SE and/or AE with SKSE, Address Library, SKSE Menu Framework.
- `SKYRIM_MODS_FOLDER=D:\ModdingSkyrim\MO2\mods` (post-build copies the DLL to `IntegratedMagic\SKSE\Plugins`).

## 1. Build both presets

From a VS Developer PowerShell (x64) at the repository root:

```powershell
cmake --preset release; cmake --build build/release
cmake --preset debug;   cmake --build build/debug
```

Expected: both builds succeed with 0 errors and no new warnings (Constitution merge gate 1).

## 2. Startup (TEST_MATRIX 0.1, 0.2)

1. Enable the mod in MO2 and launch through SKSE.
2. **Expected**: the main menu loads with no MessageBox from `IntegratedMagic.dll`.
3. Open `Documents\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`.
4. **Expected**: six `[Hooks] ... installed` lines and
   `[Hooks] hook space: 42 / 128 B used (33%), SKSE trampoline interface: yes`. No `warn` or `critical` `[Hooks]`
   lines.

Repeat 10 times for SC-001 (any failure = fail).

## 3. Gameplay smoke test

1. Load a save. **Expected**: the slot HUD is visible (TEST_MATRIX 10.1, 10.4).
2. Press a slot hotkey with a Cast spell in Automatic mode. **Expected**: equip → cast → restore (TEST_MATRIX 2.3).
3. Check the input-dependent cases 6.1–6.7 and the HUD cases 10.5, 10.6, 10.8.

## 4. Animation events (User Story 2)

With the DEBUG build:

1. Activate a slot from sheathed hands and let it finish. **Expected**: the restore waits for the sheathe animation
   (TEST_MATRIX 7.10). The log shows `[State]`/`[FLOW]` lines reacting to the anim tags.
2. Block and get staggered during a session. **Expected**: ForceExit + restore (8.1, 8.2).
3. Use a shout slot. **Expected**: it finishes on `shoutStop` (9.6).

## 5. Large load order (TEST_MATRIX 0.3, SC-004)

Launch with the full mod list where the crash was reported. **Expected**: same results as steps 2–3. The summary
line may show the SKSE interface as `yes` with either pool or fallback allocation; the game starts either way.

## 6. Negative check: named failure (SC-006, local only)

1. In a local debug build only, temporarily set `kHookSpaceBytes` to `16` and relax the `static_assert`.
2. Launch. **Expected**: a MessageBox captioned `IntegratedMagic.dll` reading
   `... Integrated Magic: not enough hook space to install hook at ...D3DInitHook::Install...: need 14 B, free 2 B
   of 16 B reserved`, and the same text at `critical` level in the log.
3. **Revert the change.** Do not commit it.

## 7. Regression baseline

Run TEST_MATRIX 5.1 (all patches OFF) and confirm the behavior matches before the change (Constitution merge
gate 3).
