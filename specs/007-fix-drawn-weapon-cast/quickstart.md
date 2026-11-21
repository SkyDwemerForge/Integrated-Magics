# Quickstart: validating the drawn-weapon cast fix

In-game validation guide for spec 007. There is no automated harness (Constitution VI).

## §0. Prerequisites

- **DLL**: the release DLL in `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins` (research R10, approved by the
  user). For §1 that is the build with R2's logging only, which behaves exactly like `main` at `f540210`. A run on
  the DLL installed before this spec also counts. For §2 and §3 it is the build with the fix.
- **Log**: `IntegratedMagic.log` in SKSE's log folder, `Documents\My Games\Skyrim Special Edition\SKSE` (on the
  user's machine, `C:\Users\igora\OneDrive\Documentos\My Games\Skyrim Special Edition\SKSE`). It is written line by
  line and **emptied at each game launch**, so copy it before starting the game again. Use one game session per patch
  pass.
- **Settings**: `requireExclusiveHotkey` OFF, Skip Channeling OFF. Skip Equip Animation and Skip Equip Animation on
  Return both OFF for the first pass, then both ON for the second.
- **Camera**: third person first. If nothing fails there, repeat setup A in first person.
- **Magicka**: raise it from the console (`player.setav magicka 2000`), so an empty magicka bar is never taken for
  the bug.
- **Slots**, all in Automatic, each with a spell that has a charge time (for example Firebolt):
  - a left-hand slot,
  - a right-hand slot,
  - a dual slot: the same spell in both hands. Dual cast needs the perk; without it, the slot is two single casts.
- **Gear in the inventory**:
  - **A**: two different one-handed swords,
  - **A'**: two copies of the same sword,
  - **B**: a sword and a shield.
- **Place**: out of combat, first, in an open area.

## §1. Reproduction on the current build (research R1, gate)

For each setup (A, A', B), and each patch pass (OFF, then ON):

1. Equip the setup and draw the weapons.
2. Activate the left-hand slot 10 times in a row. Wait for the gear to come back each time. Repeat with the
   right-hand slot, then with the dual slot.
3. Sheathe, activate the left-hand slot once, draw, then activate it 5 more times.
4. After each run, note:
   - how many activations cast,
   - what a failure looked like: nothing, a block, a swing, or the spell equipped and never cast;
   - whether the gear came back in the right hands. For A', check that both swords are back.

**Log lines to send back**, with about 20 lines around each failure:

| Line | Candidate |
|---|---|
| `AnimEvent: blockStart`, or an attack event right after `DispatchAttack Left 1.0 (equip)` | H1 (R3) |
| `cast did not start in` | H1 (R3) |
| `ShouldForceInterrupt: TRUE - ... spell mismatch` | H3 (R5) |
| `ShouldForceInterrupt: TRUE - player sheathing/sheathed` | H5 (R7) |
| `[EquipSink] ... conflicts -> ForceExitNoRestore` | H4 (R6) |
| `EnsureActiveWithSnapshot: ACTIVATING ... weaponState=` | the weapon state at each activation |

**Outcome**:
- If no setup fails, the bug is already fixed (spec FR-007). Only R2 and R9 ship.
- If the failures do not point to one candidate, repeat §1 on a build with R2's logging, which adds the
  `Activation hands:` and `Press hands:` lines (needed for H2).

## §2. Validation of the fix (SC-001 to SC-005)

On the fixed build, run §1 again, with both patch passes:

- **SC-001**: 10 out of 10 casts per slot and setup.
- **SC-002**: the sheathe → cast → draw → cast ×5 sequence, 6 out of 6.
- **SC-003**: after each cast, the gear is back in the right hands within 1 s, still drawn. Then one attack and one
  block (setup B) work on the first press.
- **SC-004**: Hold (auto-attack on) and Press (auto-attack on) slots, setups A and B, weapons drawn, 5 uses each.
- **SC-005**: no `TIMEOUT -> ForceExit` in the log.
- **D6 (the failing case of research R11)**: patches OFF, a weapon in the left hand. Draw, and activate the left-hand
  slot during the draw animation, 5 times. Each one casts once the animation ends, after about 1.5 s. The log shows
  `EquipAnimGrace: hand=L equip animation playing -> retry bound paused` and no `cast did not start`. The
  `... resumes after` line appears only when the cast still has not begun after the animation, so it is usually
  absent. Repeat with the dual slot: one dual cast each time.

Then tick TEST_MATRIX 7.11–7.17.

## §3. Regression (SC-006)

- **Sheathed casts**: setups A and B, weapons sheathed, left-hand, right-hand and dual slots, 5 each → the hands come
  up with magic, then sheathe, and the gear comes back after the sheathe (TEST_MATRIX 1.5, 7.10).
- **Dual cast**: TEST_MATRIX section 1, including 1.6 (dual cast with the weapons drawn). That section is mandatory
  for any change to `PumpCastPhase` (Constitution IV).
- **Restore**: 7.1, 7.2, 7.6 (two-handed weapon) and 7.7 (torch).
- **Attack/block rules**: 8.9–8.19 (spec 005).
- **Setups that already worked**: spells in both hands, and a spell in the left hand with a sword in the right,
  weapons drawn, 5 each.
