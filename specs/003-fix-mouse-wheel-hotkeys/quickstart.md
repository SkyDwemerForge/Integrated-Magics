# Quickstart: validating the mouse wheel hotkey fix in game

## Prerequisites

- **Build**: debug first, then release **last**, so MO2 holds the release DLL (research R9). After the release
  build, delete the debug `IntegratedMagic.pdb` that the debug post-build step copies into the MO2 mod folder.
- **Log**: `C:\Users\<user>\OneDrive\Documentos\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`, or the
  Documents folder Windows reports.
- **Mods**: QuickLoot IE installed and enabled, for the scroll scenarios.
- **Slots**:
  - slots 1–4 bound to keys `1`–`4`,
  - one slot bound to `Shift + 5` (multi-key),
  - one Hold-mode slot with a concentration spell (Flames) on key `6`.
- **Settings**: Patches → **Require exclusive hotkey ON**, unless a step says otherwise.

## 1. Hotkeys after scrolling (User Story 1 → SC-001, SC-002)

1. Zoom the camera in and out with the wheel (several notches), then press `1`. Repeat for `2`, `3`, `4` and
   `Shift + 5`, with a scroll before each press. 10 presses in total.
2. Open a container with more than one item, scroll the QuickLoot IE list with the wheel, step away and press a slot
   key. 10 presses in total.
3. **Expected**: 20 out of 20 slots activate, each on the first press, and no menu is opened during the test.
   - The log shows `mouse wheel ignored for hotkeys` once.
   - The log shows **no** `REFUSED (not exclusive) extra=mouse:8` or `extra=mouse:9` line.

## 2. Scrolling during a hotkey press (User Story 2 → SC-003)

1. Hold `6` (Hold-mode Flames) and scroll the wheel while holding. Release after 2 s. Repeat 10 times.
2. **Expected**:
   - the cast continues through the scroll and ends only on release,
   - there is no `pending CANCELLED` line for that slot.
3. With a Press-mode slot active, scroll and then press its key again. **Expected**: the slot deactivates as usual.
4. Multi-key combo (spec US2 scenario 2). Hold `Shift`, scroll the wheel, press `5` and scroll again while both are
   held. Repeat 5 times.
   - **Expected**: the `Shift + 5` slot activates each time.
   - **Log**: no `pending CANCELLED` line and no `REFUSED` line with `extra=mouse:8` or `extra=mouse:9` for that slot.

## 3. The wheel still works for the game (User Story 3 → SC-004)

1. 10 camera zooms, 10 switches between first and third person, and 10 QuickLoot IE scrolls.
2. **Expected**: each one responds as it does without the plugin. There is no delay and no lost notch.

## 4. Require exclusive hotkey still refuses real extra keys (FR-006, FR-009 → SC-006, SC-007)

1. Hold `G` (not in any combo) and press `1`. **Expected**:
   - slot 1 does **not** activate,
   - the log shows `slot=0 kb REFUSED (not exclusive) extra=kb:0x22`, exactly once for that press.
2. Release `G` and press `1`. **Expected**: slot 1 activates.
3. Run TEST_MATRIX 6.1–6.7. **Expected**: no regressions.

## 5. Hotkey capture ignores the wheel (FR-005 → SC-005)

1. In the settings menu, start capturing a slot hotkey and scroll the wheel. Repeat 5 times.
2. **Expected**: the capture keeps waiting. The wheel is never recorded, and the next key or button press is the one
   recorded.

## 6. Require exclusive hotkey OFF: the first report's steps (Clarification Q3 → SC-008)

1. Turn Require exclusive hotkey **OFF**.
2. Zoom the camera, switch first/third person, then press `1`–`4`. 10 presses in total.
3. **Expected**: 10 out of 10 activate.
   - **If any fails**, record the log excerpt. That means a second cause, which goes to a new spec. It does not block
     this feature.

## 7. Stuck-key recovery for extra mouse buttons (FR-007)

Mouse ids count from 0: id 5 is the 6th button.

1. **Premise check (any mouse)**: hold the left or right mouse button for about 2 s (block, or draw a bow).
   - **Expected**: the log shows `first held mouse event seen (id=<0|1> heldSecs=...)` once. This confirms that the
     game delivers held events to the plugin, which the 500 ms heartbeat relies on (research R6).
   - **If it never appears**: stop, because the heartbeat would wrongly clear held buttons 6–8.
2. **Lost release (only with a 6+ button mouse)**: hold the 6th button (or higher), alt-tab away, release it, then
   come back.
   - **Expected**: within about 1 s the log shows `ReconcilePhysicalKeyState: cleared stuck keys`, and hotkeys work
     with Require exclusive hotkey ON.
