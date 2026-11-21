# Quickstart: validating the Magic Menu hotkey fix

In-game validation guide for spec 008. There is no automated harness (Constitution VI).

## §0. Prerequisites

- **DLL**: the release DLL in `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins` (research R12, approved by the
  user). The new `[Input] MenuHold:` lines in the log show that it is the one loaded.
- **Log**: `IntegratedMagic.log` in `Documents\My Games\Skyrim Special Edition\SKSE`. It is emptied at each game
  launch, so copy it before starting the game again.
- **Slots**:
  - a keyboard single-key hotkey (for example, `1`);
  - a keyboard 2-key hotkey (for example, `Shift + 2`);
  - the default gamepad layout, where every slot shares the modifier (`RT`) plus a face button or `LB`.
- **Wheel**: a wheel (HUD popup) hotkey on keyboard and on gamepad.
- **Settings**: Require exclusive hotkey and Press both at same as you normally play. Quick bind does not use them.
- **Reference**: before testing, note what each hotkey button does in the Magic Menu on its own (for example, `RT`
  alone or `Shift` alone). Those are the reactions that must not happen during a quick bind.

## §1. Quick bind (US1, SC-001)

1. Open the Magic Menu and hover a spell.
2. **Keyboard**:
   - press the single-key hotkey 10 times, each on a different hovered spell;
   - then press the 2-key hotkey 10 times, both keys within 0.3 s.
3. **Gamepad**:
   - quick-bind 10 times with `RT` + face button, half with `RT` first and half with the face button first;
   - repeat with a `RT + LB + face` hotkey, if one is set.
4. Hover a shout or a power, and quick-bind it twice.
5. **Overlapping hotkeys** (SC-007): with two slots whose gamepad hotkeys overlap (for example, `RT + LT + A` and
   `RT + A`):
   - quick-bind 10 times with the longer one, in different press orders. Only its slot receives the spell, and the log
     shows `QuickBind: slot=<short> SUPPRESSED by slot=<long>`, or `waiting for a longer combo` then `SUPPRESSED`;
   - quick-bind 10 times with the shorter one, holding it. Only its slot receives the spell, after about 0.3 s or at
     the release (`assigned after ... (window|released)`).

**Expected**:
- each press assigns the hovered entry to the slot, as the HUD shows;
- the menu shows none of the reactions noted in §0;
- the log shows `MenuHold: ... held`, then `consumed by slot=<n>` for every key of each hotkey, and no `handed to
  menu` for them.

## §2. Wheel toggle (US2, SC-002)

In the Magic Menu, open and close the wheel 10 times with the keyboard hotkey and 10 times with the gamepad hotkey.

**Expected**: the wheel toggles every time, the menu does not react, and the log shows `consumed by wheel`.

## §3. Normal menu use (US3, SC-003, SC-004)

1. In the Magic Menu, press each hotkey button **alone** 10 times, as a short tap. Examples: `RT` alone, `Shift` alone,
   a face button alone.
   - **Expected**: the menu performs its usual action each time, exactly once, within 0.3 s, and on a tap right at the
     release.
   - **Log**: `handed to menu after <s>s (released)` for a tap, or `(window)` for a button held past 0.3 s.
2. Hold a hotkey button alone for about 1 s, for example one the menu uses to scroll.
   - **Expected**: the menu reacts after at most 0.3 s, then keeps reacting while the button is held, and stops when
     it is released.
3. Use buttons that belong to no hotkey (navigation, other categories).
   - **Expected**: no delay compared with before.

## §4. No stuck key (SC-006)

1. Hold a hotkey key and close the Magic Menu with another key, 5 times.
2. Press a hotkey key and close the menu within 0.3 s, 5 times.

**Expected**: back in gameplay, the next movement, attack and slot activation work at the first press. The log may
show `queued event(s) dropped (Magic Menu closed)`.

## §5. Regression (SC-005)

- **Gameplay input**: TEST_MATRIX section 6 (6.1–6.23), with special attention to the gamepad shared-button cases
  (6.15–6.23). Combo timing must feel the same.
- **Wheel**: TEST_MATRIX 10.1–10.3 (open the wheel and assign with it).

Then tick TEST_MATRIX 10.11–10.16.
