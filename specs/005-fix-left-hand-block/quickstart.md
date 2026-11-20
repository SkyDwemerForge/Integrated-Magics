# Quickstart: validating the block button with slots in game

## Prerequisites

- **Build**: debug first, then release **last**, so MO2 holds the release DLL (in-game validation on release was
  approved). Then delete the debug `IntegratedMagic.pdb` from `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`.
- **Log**: `C:\Users\<user>\OneDrive\Documentos\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`. It is
  overwritten at every launch, so copy it before restarting the game. Look for:
  - `[Input] HandGate`, `HandGate cancel`,
  - `cast did not start`, `real interrupt`,
  - `REFUSED (not exclusive)`,
  - the `[FLOW]` retry lines.
- **Hardware**: keyboard/mouse (block = right mouse button) and a gamepad (block = `LT`).
- **Gear**: a one-handed weapon in the right hand and a **shield** in the left hand, unless a step says otherwise.
- **Slots**:

  | Slot | Content | Mode |
  |---|---|---|
  | A | Flames (Concentration), **left hand** | Hold, auto-attack on |
  | B | Firebolt (Cast), **left hand** | Automatic |
  | C | Firebolt, **left hand** | Press, auto-attack on |
  | D | Firebolt, **both hands** (dual cast) | Automatic |
  | E | Flames, **right hand** | Hold, auto-attack on |
  | F | Firebolt, **right hand** | Automatic |

- **Settings**: Require exclusive hotkey **OFF**, unless a step says otherwise.

## 0. Baseline (current release DLL, before the fix; optional, confirms research R1)

1. Hold block, then press slot A's hotkey and keep it held for 2 s. Copy the log.
2. Hold slot A's hotkey, then press and hold block. Copy the log.
3. Hold slot E's hotkey (right hand, shield in the left), then press block. Copy the log.
4. **Expected (bug)**:
   - in 1–2, the left hand twitches, and the log repeats `StartRequested for 0.15s → UP` or `pre-cast spurious` for
     the left hand,
   - in 3, the slot stays active. Note whether `AnimEvent: blockStart` appears.

## 1. Block held before the slot (User Story 1 scenario 1, User Story 2 scenarios 4–5 → SC-001)

1. Hold block, then activate slot A and keep the hotkey held for 2 s. Release both. 10 times.
2. Repeat with slot B (tap the hotkey while blocking) and with slot F (right hand). 10 times each.
3. Repeat 1 and 2 on gamepad with `LT`.
4. Repeat 1 with a one-handed **weapon** in the left hand instead of the shield.
5. **Expected**:
   - the block stops, the spell is cast, and there is no twitching,
   - the log shows `HandGate: Left button taken at cast start -> released in game` once per activation.
6. **Require exclusive hotkey ON**: repeat step 2 with slot F once. **Expected**: the slot is refused, and the log
   shows `REFUSED (not exclusive) extra=mouse:1` (`extra=gp:<n>` on gamepad), as today.

## 2. Block pressed during a left-hand cast (User Story 1 scenarios 2–4 → SC-001)

1. **Hold**: hold slot A's hotkey; while Flames is casting, press and hold block. 10 times.
   - **Expected**: the session ends at once and the shield comes back. The character does not block until block is
     pressed again.
   - **Log**: `press during plugin cast -> cancel session`, then `HandGate cancel -> ForceExit`.
2. **Automatic**: activate slot B and press block before the bolt leaves. 10 times. **Expected**: the same as step 1.
3. **Press**: activate slot C and press block while it charges. 10 times. **Expected**: no effect, and the bolt is cast.
   Log: `ignored (Press mode)`.
4. **Clicks**: repeat 1–3, clicking block quickly 3–4 times instead of holding it. **Expected**: no twitching, with the
   same outcome as each step.
5. Repeat 1–4 on gamepad with `LT`.

## 3. Block with the shield during a right-hand slot (User Story 2 scenarios 1–3 → SC-002, SC-005)

1. **Hold**: hold slot E's hotkey; while Flames is casting, press and hold block. 10 times.
   - **Expected**: the slot ends at once, the right-hand weapon comes back, and the character **blocks while the
     button is held**, with no re-press.
   - **Log**: `other-hand press -> passes, cancel session`.
2. **Automatic**: activate slot F and press block during the charge. 10 times. **Expected**: the same as step 1.
3. **Press**: set slot F's spell to Press with auto-attack, activate it, and press block during the charge. **Expected**:
   - the press does not cancel the slot, and the block works,
   - if the block interrupted the charge, the slot ends within about 1 s, with `real interrupt -> finish + finalize`.
4. **Never stays active**: in every run of this section and of §2, the HUD shows no slot active afterwards. In
   particular, no Automatic slot stays active for 30 s.
5. Repeat 1–2 on gamepad with `LT`.

## 4. Dual cast (User Story 1 scenario 5 → SC-004)

1. Repeat §1 step 2 and §2 step 2 with slot D, 10 times each.
2. **Expected**: a single dual cast every time (dual-cast animation, dual cost), or a clean cancel as in §2 step 1.
   Never two single casts.

## 5. No stuck state (User Story 3 → SC-003)

1. After each §1, §2 and §3 run, activate the same slot 10 times **without touching block**.
2. **Expected**: every activation casts on the first try, and the click workaround is never needed.
3. **Session ends while a taken button is held**: hold block, activate slot A (the button is taken), let the session
   end, keep block held, and release it 2 s later.
   - **Expected**: no cast or block happens on its own, and a new block press blocks normally.
   - **Log**: `button released -> gate cleared` once.

## 6. Automatic left hand with nothing cast (second report, Edge Cases)

1. Use slot B 20 times in normal play, with and without block.
2. **Expected**: every activation casts. If equip → unequip with nothing cast still happens, copy the log: it is a
   separate cause and gets its own spec.

## 7. Cast that cannot start (User Story 4 → SC-006)

1. Jump, and right at the start of the jump activate slot E and keep the hotkey held. 10 times.
2. Repeat with slot A (left hand).
3. **Expected**:
   - either the spell is cast once the character lands, or within about 1 s the session ends and the gear comes back,
   - the hand never twitches for longer,
   - when it gives up, the log shows `cast did not start in 1.0Xs -> ForceExit`.

## 8. Vanilla behavior (User Story 5 → SC-007)

1. **No session**: block with the shield, bash, and cast a left-hand spell equipped from the magic menu with the right
   mouse button. 10 each. **Expected**: as in vanilla, with no `HandGate` lines.
2. **Press without auto-attack**: set slot C's spell to auto-attack **off**. Activate it, then cast with the right mouse
   button. 10 times. **Expected**: a manual cast every time, as today.

## 9. Regression (SC-009)

TEST_MATRIX 1.1–1.6, 2.x, 4.1–4.3, 6.1–6.2, 8.1, and spec 004 6.15–6.23. **Expected**: no regressions. Combo presses
are never logged by `HandGate`.
