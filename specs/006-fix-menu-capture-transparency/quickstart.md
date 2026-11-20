# Quickstart: validating the menu with repeated hotkey captures

## Prerequisites

- **Build**:
  - Build release first, then **debug last**, so MO2 holds the DEBUG DLL (Constitution VI, research R4).
  - Keep the debug `IntegratedMagic.pdb`.
  - If `skse64.log` shows `couldn't load plugin 4551` (`000011C7`), Windows is still blocking the DEBUG DLL. Ask the
    user to approve the release-build fallback, rebuild with release last, and delete the PDB.
- **Log**: `C:\Users\<user>\OneDrive\Documentos\My Games\Skyrim Special Edition\SKSE\skse64.log`. Check that the
  `IntegratedMagic` line has no `couldn't load plugin`.
- **Another SKSE Menu Framework mod** installed, to check its page after the captures (any mod with a page in the SKSE
  menu).

## 1. Repeated captures (User Story 1 → SC-001, SC-002, SC-003)

1. Open the SKSE menu → Integrated Magic → the slot hotkeys.
2. Do 20 captures in a row, mixing:
   - keyboard fields (press a key) and gamepad fields (press a button),
   - completed captures and cancelled ones (the "X" button),
   - at least 5 clicks on "Cap" of different fields in quick succession.
3. **Expected**:
   - the page stays fully opaque the whole time,
   - during each capture the other fields' "Cap" buttons look disabled, and they come back when it ends,
   - each completed capture stores the pressed key, and each cancelled one changes nothing.
4. Open another mod's page in the SKSE menu. **Expected**: it looks normal (SC-002).
5. Close and reopen the SKSE menu. **Expected**: still normal.

## 2. Other controls (User Story 2 → SC-004)

Use each of the following 10 times: "- Vertex" (down to the minimum, then add vertices back), "Apply changes" (after a
change), the modifier "M" button, the HUD anchor grid, and the label corner buttons. **Expected**: the menu never fades,
and each control looks and behaves as today.

## 3. Regression (SC-005)

TEST_MATRIX 10.7 (hotkey rebinding) and 6.12 (the wheel ignored during capture). **Expected**: no regressions.
