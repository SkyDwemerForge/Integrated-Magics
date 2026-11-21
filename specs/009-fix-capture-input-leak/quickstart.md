# Quickstart: validating the hotkey capture fix

In-game validation guide for spec 009. There is no automated harness (Constitution VI).

## §0. Prerequisites

- **DLL**: the release DLL in `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins` (approved by the user). The
  `[Hooks] MenuInputShieldHook installed` and `[UI] CaptureGuard:` lines in the log show that it is the one loaded.
- **Log**: `IntegratedMagic.log` in `Documents\My Games\Skyrim Special Edition\SKSE`. It is emptied at each game
  launch, so copy it before starting the game again.
- **Setup**: SKSE Menu Framework open on the plugin's page, Hotkeys tab, with a slot selected. Have a gamepad
  connected, plus a keyboard and mouse.

## §1. Gamepad capture (US1, SC-001)

1. Using only the gamepad, move to a gamepad field's "Cap" and press `A`.
   - **Expected**: the capture waits. `A` is not taken as the value (log: `CaptureGuard: on`, and no value yet).
2. Press each of the 16 gamepad inputs once, one capture each: D-pad ×4, `Start`, `Back`, `L3`, `R3`, `LB`, `RB`, `A`,
   `B`, `X`, `Y`, `LT`, `RT`.
   - **Expected**: each field shows its button. The SKSE menu does not move the focus, activate anything, go back or
     close, open the window switcher or switch tabs.
3. Hold a D-pad direction for 1 s while capturing it.
   - **Expected**: the focus does not move while it is held. The log shows `CaptureGuard: off after <s>s (released)`
     only after the release.

## §2. Keyboard and mouse capture (US2, SC-002)

1. With the mouse, click "Cap" on a keyboard field, then press arrows, `Enter`, `Space` and `Tab` (one capture each).
   Then click with the left, right and middle mouse buttons over another control.
   - **Expected**: each value is stored, and no control reacts.
2. With the keyboard, move to "Cap" and activate it with `Enter` or `Space`.
   - **Expected**: that key is not taken as the value.

## §3. Other device and Escape (SC-005)

1. Capture on a keyboard field and press a gamepad button, 5 times.
   - **Expected**: the capture is cancelled, the field is unchanged, the menu does not react, and the log shows
     `Capture: gp input during a kb capture -> cancelled`.
2. Capture on a gamepad field and press a key, 5 times.
   - **Expected**: the same, with `kb input during a gp capture`.
3. Capture and press `Escape`.
   - **Expected**: the capture is cancelled (`Capture: Escape -> cancelled`), and the SKSE menu stays open, because it
     does not see input during a capture (research R12). Press `Escape` again after releasing it: now the menu closes
     as usual.

## §4. Normal use and closing (US3, SC-003, SC-004)

1. After each capture of §1–§3, release everything and press a few buttons.
   - **Expected**: the menu reacts as usual on the first press (`CaptureGuard: off` in the log).
2. Start a capture and close the SKSE menu with its hotkey or `Escape`. Reopen it.
   - **Expected**: the capture is gone (`page not shown -> cancelled`, or the Escape line), and navigation works.
3. Run TEST_MATRIX 10.9: 20 captures in a row, completed and cancelled.
   - **Expected**: the menu stays fully visible, and other mods' pages behave normally.

**If the menu still reacts in §1 or §2**, check the log:
- `[Hooks] MenuInputShieldHook installed (outer, previous target in <module>)` must appear once, on the first capture.
  `<module>` is the hook that was outermost before. In the validation run it was `InputManager.dll` (the Input
  Manager mod), with SKSE Menu Framework's hook further in;
- `CaptureGuard: on (menu input hidden, ...)` must appear at each capture, and the `off` line must report
  `input frames hidden from the menu=<n>` with `n > 0`;
- if `n` is 0, the page was not seen as drawn. If `n > 0` and the menu still reacts, the menu reads that input from
  somewhere other than the input call site (research R12).

Then tick TEST_MATRIX 10.18–10.22.
