# Quickstart: Validate the Skip Channeling fix for bound weapons

Expected log lines are defined in [contracts/skip-channeling.md §4](contracts/skip-channeling.md#4-diagnostic-lines-fr-009).

## Prerequisites

- Both presets build, **debug last**: `cmake --preset release; cmake --build build/release`, then the same for
  `debug`. The post-build step copies the DEBUG DLL and PDB to MO2. **Every validation below runs on the DEBUG
  build** (Constitution VI). The release DLL in `build/release` is for distribution only.
- Only **one** Integrated Magic mod enabled in MO2, `IntegratedMagic` (the build output). Disable the older
  `Integrated Magic` package while testing (research R9).
- A test character with Bound Sword, Bound Bow, one expensive fire-and-forget spell (for example Fireball) and one
  cheap one (Firebolt or Candlelight), plus enough magicka for each.
- Log: `Documents\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`. On this machine Documents is
  `C:\Users\igora\OneDrive\Documentos`.

## 1. Diagnostic gate: magicka cost model (T015)

For each spell (Bound Sword, Fireball, Firebolt), in Automatic mode on the right hand:

1. Patch **OFF**: note magicka before the press and after the cast. Note whether the bar drains **while charging**
   or all at once **at release**.
2. Patch **ON**: the same. Check the `[FLOW] SkipChanneling: applied ... owed=`, `fired ... owed=` and
   `[Action] DrainMagicka: owed=... magicka=A -> B` lines.
3. **Expected**: the magicka spent with the patch ON is within 1 point of the patch OFF (SC-007).
   - First run (2026-11-19): ON spent only one frame of the cost (under-charge), so the R2 fallback was applied
     (T016).
   - Re-run after T016: `owed` ≈ `cost` minus one frame, and `magicka at skip − B` ≈ `cost`.

## 2. Bound weapon with the patch ON (User Story 1)

For each configuration (Automatic / right, Automatic / left, Press with auto-attack / right):

1. Press the slot hotkey.
2. **Expected**:
   - no sustained hand charging pose (at most the first frames of the cast start, SC-006),
   - the weapon-appearing visual plays,
   - the weapon is summoned **once**,
   - control returns within 3 s (SC-001).
3. **Log**: one `SkipChanneling: applied` line, then `fired`, and no `spurious → UP (DOWN next frame)` lines for
   that hand (SC-005).
4. Repeat 20 times in total across the configurations. Expect zero sessions ending by `TIMEOUT` (SC-002).
5. Press mode: the second press ends the session and the restore matches the patch OFF (FR-004).

## 3. Summon interruption is not a failure (FR-003)

In the logs from §2, the `InterruptCast` emitted when the bound weapon equips arrives **after** `OnSpellFired` and
does not produce a restart or an early finish.

## 4. Forced failure ends cleanly (FR-001, R3)

1. Patch ON, Bound Sword in Automatic mode. Lower magicka just above the cost (for example, cast other spells
   first), then take damage from an enemy's magicka-drain spell or potion during the cast if available.
2. **Expected**: if the game cancels the cast after the skip, the log shows
   `SkipChanneling: interrupted after skip ... -> finish (no retry)`, the session ends within about 1 s, and
   equipment is restored.

## 5. Interactions

- **Dual bound**: Bound Sword in both hands. Both summon, no loop, and both hands finish together. The log shows a
  single `SkipChanneling: applied dual L+R` line and no per-hand `applied` lines (FR-012). Both hands fire in the
  same frame.
- **Mixed slot**: Bound Sword right and Firebolt left. Each hand follows its own spell; no loop (FR-007).
- **Two-handed bound** (Bound Bow): summons once and the session ends normally.
- **Skip Equip Animations ON together with Skip Channeling**: no loop. Note whether the weapon draw is shortened
  (accepted per the spec edge case; report it to the user).
- **Bound weapon already summoned** (TEST_MATRIX 5b.9): with Bound Sword still in the hand, activate the slot again
  with the patch ON. No loop, and the result matches the patch OFF.
- **Player interruption during the summon** (TEST_MATRIX 5b.10): with the patch ON, block, get staggered, or open a
  menu while the Bound Sword slot is casting. The session ends, equipment is restored and there is no loop. Note
  which exit the log shows: `ForceExit`, or `SkipChanneling: interrupted after skip ... -> finish (no retry)`.

## 6. Regression

- Patch **ON**: TEST_MATRIX 5.4, 5.5 and 1.3 (instant cast and dual cast still work). For 1.3, the log shows
  `applied dual L+R` and a single dual cast occurs (FR-012).
- Patch **ON**, concentration spell (TEST_MATRIX 5b.11): Flames in Automatic mode charges and channels normally, and
  the log shows `SkipChanneling: skipped ... reason=type=Concentration`.
- Patch **OFF**: TEST_MATRIX 3.7–3.9 (bound × modes) and the 5.1 baseline are unchanged.

## 7. Tooltip (User Story 3)

Open the in-game menu, go to the Patches tab and hover Skip Channeling. The text matches contract §5. Check it with
the distributed `IntegratedMagic_Strings.txt` present as well.
