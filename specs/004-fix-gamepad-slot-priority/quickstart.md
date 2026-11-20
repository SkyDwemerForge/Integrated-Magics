# Quickstart: validating gamepad slot priority in game

## Prerequisites

- **Build**: debug first, then release **last**, so MO2 holds the release DLL (research R9). Then delete the debug
  `IntegratedMagic.pdb` from `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`.
- **Log**: `C:\Users\<user>\OneDrive\Documentos\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`. Look for
  `[Input] HoldBack`, `SUPPRESSED` and `HotkeyCache` lines.
- **Hardware**: a gamepad (Xbox layout names are used below), plus a keyboard for §5.
- **Slot count**: 8. Give every slot a cheap fire-and-forget spell, so a successful cast is easy to see.
- **Gamepad bindings**. Slots 1–4 keep the default layout, and slots 5–8 form a "second layer":

  | Slot | Combo |
  |---|---|
  | 1 | `X + RT` (default) |
  | 2 | `Y + RT` (default) |
  | 3 | `B + RT` (default) |
  | 4 | `A + RT` (default) |
  | 5 | `X + RT + LB` |
  | 6 | `Y + RT + LB` |
  | 7 | `B + RT + LB` |
  | 8 | `A + RT + LB` |

  The HUD popup stays `LB + RB`. Do not press `RB` during these tests.
- **Settings**: Require exclusive hotkey **OFF**, unless a step says otherwise.

## 1. Slots 5–8 (User Story 1 → SC-001)

1. Hold `RT` and `LB`, then press `X`, `Y`, `B` and `A` in turn (slots 5–8). Repeat until you have 20 activations.
2. **Expected**:
   - every activation casts the slot's spell,
   - the character **never** jumps, opens a menu, activates something, or attacks with the right hand because of the
     combo,
   - the log shows `HoldBack: ... consumed by slot=4..7` for `RT` and for the face button, and **no** `handed to
     game` line for them.

## 2. Press order, all slots (User Story 1 → SC-002)

This is the user's confirmed reproduction (spec Background item 7). Press each combo as a **quick chord**: the second
button right after the first, not a deliberate pause.

**The log decides what was a chord** (spec SC-002). A chord's buttons end as `consumed by slot=...` (often after
`window extended (slot=... pending)`, `(... chord)` or `(... complete)`). A press whose first button shows `handed to game`, with no
`window extended` line, was a slow chord. It does not count against SC-002: note it and press again.

1. **Face button first**: press `Y` and then `RT` almost together (slot 2), repeated 20 times. Then do the same for
   slots 1, 3 and 4, and for slots 5–8, starting with the face button (`face`, then `RT`, then `LB`).
2. **Modifier first**: repeat step 1 with `RT` pressed first.
3. **Expected**, for every press the log counts as a chord:
   - every slot casts, in both orders,
   - the character **never jumps**, activates or opens a menu, and no right-hand attack reaches the game,
   - the log shows `consumed by slot=...` for `RT` and for the face button, and **no** `handed to game` line for
     them,
   - in the face-first case it shows `window extended (slot=... pending)` or `(... complete)` for 2-button combos, or
     `(... chord)` for 3-button ones, and then `consumed`.
4. **Slow chord (documented limit)**: press `Y`, wait about half a second, then press `RT`. **Expected**:
   - the character jumps, because `Y` was a normal press,
   - slot 2 still activates when `RT` completes the combo,
   - this matches the spec Edge Case "Slow chord", which covers a second button later than 50 ms, or a 3-button
     chord slower than 100 ms (combo tolerance, Clarification 2026-11-20).
5. **Hold mode** (spec FR-001 and FR-010, SC-008): set slot 6 (`Y + RT + LB`) to Hold mode. Hold its combo for about
   2 s, then release, 10 times. **Expected**:
   - the game performs no action of `Y`, `RT` or `LB` during the hold,
   - after the release no button stays held in the game: no stuck attack, and the character can move and attack
     normally.

## 3. Buttons on their own and abandoned combos (User Story 2 → SC-003)

1. With no slot pressed, press `RT` alone (attack/cast), then `Y` (jump), `A` (activate) and `B` (menu). 10 each.
2. **Expected**:
   - each one performs its normal game action, with no extra delay you can notice compared with the current version,
   - the log shows `held for slot=...` followed by `handed to game` for each one.
3. **Abandoned combo**: press `Y`, then release it without pressing `RT`. Repeat 10 times. **Expected**:
   - the jump happens as a normal press,
   - the log shows `handed to game`.
4. **Casting manually during a session** (spec FR-010, SC-008):
   - Set slot 2 to **Press** mode with auto-attack **OFF**, and activate it with `Y + RT`. The spell is equipped and
     the session stays active.
   - Release both buttons, then press and hold `RT` **alone** to cast the spell, and release it. Repeat 10 times.
   - **Expected**: the spell casts every time, released when `RT` is released, and nothing stays stuck. The log
     shows `held for slot=...` and then `handed to game` for `RT`.
   - Press `Y + RT` again to end the session.

## 4. Overlap rule (FR-009 → SC-007)

1. Hold `RT` and `LB`, press `Y` last. Repeat 10 times. **Expected**:
   - slot 6 (`Y + RT + LB`) fires every time, and slot 2 (`Y + RT`) never does,
   - the log shows `slot=1 SUPPRESSED by slot=5 (longer combo)`.
2. Hold `RT`, press `Y`, wait about 0.5 s, then press `LB`. **Expected**: slot 2 fires, because it completed first.
   Slot 6 may fire afterwards as a second press. This is documented behavior.
3. Temporarily set slot 8 to `B + RT`, identical to slot 3. Reload the hotkeys by pressing Apply, and press
   `RT + B`. **Expected**:
   - the log shows `HotkeyCache: slots 2 and 7 have the same gp combo; slot 2 fires`,
   - slot 3 fires,
   - restore slot 8 afterwards.
4. Require exclusive hotkey **ON**: repeat step 1. **Expected**: slot 6 fires, and slot 2 is refused with
   `extra=gp:8` (`LB`).

## 5. Keyboard shared key (User Story 3 → SC-004)

1. Bind slot 1 to `Shift + 1` and slot 5 to `Shift + 5`. Press each one 10 times.
2. **Expected**:
   - each combo activates its own slot,
   - `Shift` causes no game action because of the combo,
   - the log shows `consumed by slot=...` for `Shift` (`kb:0x2a`).

## 6. Regression (SC-005)

Run TEST_MATRIX 6.1–6.14 and the dual-cast cases 1.1, 1.3 and 1.5. **Expected**: no regressions.

## 7. Optional baseline (before the fix)

The user already reproduced the bug on 2026-11-20 (spec Background item 7). To repeat it on the current build:
- run §2 step 1 on slot 2 (`Y` slightly before `RT`) → the character jumps,
- run §1 step 1 → it jumps or attacks on some presses.
