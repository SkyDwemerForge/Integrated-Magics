# Quickstart: validating the spell name size

In-game validation guide for spec 011. There is no automated harness (Constitution VI). Validate on the **debug**
build (spec 010).

## §0. Prerequisites

- **Builds**: `release`, then `debug` last, from the same checkout. Both with 0 warnings.
- **Where things are installed** (MO2):
  - **The DLL**: each build copies it to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`, so the last build
    made is the one loaded.
  - **`styles.ini` and the font**: in the `Integrated Magic` mod (`SKSE\Plugins\IntegratedMagics\`). The build does
    not copy them. `SpellNameScale` is written there by "Apply changes", so it does not need to be added by hand.
- **Logs**: `IntegratedMagic.log` in `Documents\My Games\Skyrim Special Edition\SKSE`, emptied at each launch.
- **Slots for the test**: at least 4 slots, with:
  - a short one-spell name (for example `Flames`);
  - a long one-spell name (for example `Lightning Bolt`);
  - two different spells (for example `Flames | Healing`);
  - a shout or a power.
- **Settings**: "Show spell names" on.
- **Screenshots**: the game's screenshot key or the Steam overlay, at the same resolution for all of them.

## §1. The default look (US1 scenario 5, FR-003, SC-004)

1. **Before the change**: on a release build from `main`, take a screenshot of the HUD with names on, position Bottom.
   Use slots whose names fit in 1 or 2 lines.
2. **After the change**: on the new build, with `SpellNameScale` absent or 1.0, take the same screenshot.
   - **Expected**: the names match the first screenshot. The debug log has `[HUD] FontLoader: atlas built (base=28 px,
     names=none, scale=1)`.
   - **Known difference**: a name that needs 3 lines (for example `Flames | Healing` when a word is wider than the
     slot) now shows its third line, which the HUD window cut off before (research R5).

## §2. Changing the size (US1, FR-001, FR-004, FR-005, SC-001)

1. Open the SKSE menu, the plugin's page, the HUD section. Below "Padding" there is "Size (%)" with 100.
2. Click `+` once (105). Then click `+` several times quickly up to 150. Watch the HUD, which stays visible behind the
   menu.
   - **Expected**: the names grow about 0.25 s after the last click, without a restart. One `atlas built (...,
     names=42 px, scale=1.5)` line for the burst of clicks, not one per click.
3. Type `73` in the field and press `Enter`.
   - **Expected**: the value becomes 75.
4. Type `10`, then `500`.
   - **Expected**: the value becomes 50, then 200.
5. Repeat step 2 ten times between 50 and 200 (SC-001, SC-007).
   - **Expected**: each change shows in under 1 s. After the last one, the frame rate is the same as before them.
6. Press "Apply changes", close the menu and restart the game (3 times; SC-005).
   - **Expected**: the names have the applied size from the first frame. `styles.ini` has `SpellNameScale` under
     `[HUD]`.

## §3. Sharp, complete and in place (US2, FR-007, FR-008, FR-009, SC-002, SC-003)

1. Set 200%. Take a screenshot. Set 100% and take another one.
   - **Expected**: at 200% the letters are as sharp as at 100%, with no blur at the edges (SC-002).
2. At 200%, select each of the 6 positions (Top, Right, Bottom, Left, Toward Center, Away from Center).
   - **Expected**: every line of every name shows in full, including the 2-spell name. The names sit on the chosen side
     and do not cover the slot circle or its hotkey icons (SC-003).
3. At 50%, check the same positions.
   - **Expected**: small but readable names, at the same gap from the circle.

## §4. Other text, the popup and the edge cases (US1 scenario 4, US3, Edge Cases)

1. At 200%, open the Magic Menu and the assignment popup.
   - **Expected**: the popup names are drawn at 200% and show in full. The tooltips and the mode labels keep their
     size (FR-002, SC-006). Assign a spell by clicking: it works as before (US3 scenario 2).
2. At 150%, turn "Show spell names" off, then on again.
   - **Expected**: the names disappear, then come back at 150%. Debug log: `names=none` when off, `names=42 px` when on.
3. Activate a slot while the size control is being changed.
   - **Expected**: the cast runs normally. Names are hidden during the session, as before.
4. **Out-of-range INI**: close the game, set `SpellNameScale = 9` in `styles.ini`, start the game.
   - **Expected**: the names are drawn at 200%, and the control shows 200. Set it back.
5. **Optional, extra character sets**: with a font that has Cyrillic glyphs and `RangeCyrillic = true`, give a slot a
   spell with a Cyrillic name (for example from a translation).
   - **Expected**: the characters show at 50%, 100% and 200% (FR-010).

## §5. TEST_MATRIX cases (section 10)

| Case | Covered by |
|---|---|
| 10.23 Default size unchanged | §1 |
| 10.24 Size changes live, saved, kept after restart | §2 |
| 10.25 200%: sharp, full, in place in the 6 positions | §3 |
| 10.26 Popup names follow the size, other text does not | §4.1 |
| 10.27 Names off/on and out-of-range INI | §4.2, §4.4 |
