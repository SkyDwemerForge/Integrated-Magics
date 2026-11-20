# Feature Specification: Fix the SKSE menu turning transparent when capturing hotkeys

**Feature Branch**: `fix/menu-capture-transparency` (created from `main` at `c301462`, after spec 005 merged as #74)

**Created**: 2026-11-20

**Status**: Implemented and validated in game (user, 2026-11-20, release DLL, approved). See tasks.md implementation log

**Input**: User description: "Recebi esse reporte de um problema em que o SKSE ficava transparante ao usar a
funcionalidade de captura de teclas" (a player report, quoted below), plus "Crie uma nova branch a partir da main para
essa nova spec."

## Background & Investigation Findings *(context)*

Sources: the player's report below and a review of the in-game settings menu code on `main` (`c301462`).

1. **Report (player)**: clicking the button several times while binding a hotkey makes the SKSE menu fade, until it is
   completely invisible after a few clicks.
   - The reporter traced it to a button whose "disabled" look is switched on and off by checking the same state
     twice: once before the button and once after.
   - Clicking the button changes that state, so the second check disagrees with the first. The menu's "disabled"
     bookkeeping is then closed without having been opened.
   - Every such click leaves it one step out of balance. The menu's opacity is multiplied down again and again, so the
     panel fades.
   - The reporter's fix: read the state once, before the button, and use that same value for both checks.
   - The code in the report names a "Clear" button and a crosshair action, which this plugin does not have. The
     pattern is the point.
2. **The same defect exists in this plugin (code review)**: the hotkey capture button ("Cap"), next to every keyboard
   and gamepad hotkey field of every slot and of the HUD popup.
   - While a capture is in progress, the other fields' capture buttons are shown disabled.
   - The button checks "is a capture in progress?" before it is drawn (to disable it) and again after (to re-enable it).
   - Clicking it starts a capture, so on that frame the second check is true and the first was false. The menu is
     re-enabled without having been disabled.
   - Each click on "Cap" that starts a capture leaves the menu one step out of balance, which matches the report: more
     clicks, more fading.
3. **Nothing else in the menu has this defect (code review)**:
   - the other places that disable a button ("- Vertex" in the slot shape editor, "Apply changes") always open and
     close the disabled state together,
   - the buttons that are highlighted when selected (modifier "M", HUD anchor grid, label corner) read their state
     once, before the button, which is the reporter's pattern,
   - the HUD and the assignment popup are drawn separately and are not affected.
4. **Why other mods' menus can fade too**: the plugin's settings are drawn inside the shared SKSE menu, the one other
   mods use too. The broken state can therefore carry over to the whole SKSE menu, not only this plugin's page.

## Clarifications

### Session 2026-11-20

- Q: The DEBUG DLL still fails to load (a DEBUG-only fault in the hook installation, not Windows). May this spec be
  validated in game on the release build (Constitution Principle VI)? → A: Yes, approved by the user.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Binding hotkeys never makes the menu fade (Priority: P1)

A player opens the plugin's page in the SKSE menu and binds slot hotkeys with the capture button: once, or many times
in a row, on keyboard and gamepad fields, cancelling some captures and completing others. The menu stays fully opaque
and usable the whole time.

**Why this priority**: this is the reported bug. A menu that becomes invisible cannot be used to finish the setup, and
it can affect the menus of other mods too.

**Independent Test**: open the plugin's settings and press "Cap" 20 times in a row on different fields, completing
some captures and cancelling the rest. Then check the plugin's page and another mod's page in the SKSE menu.

**Acceptance Scenarios**:

1. **Given** the plugin's settings are open, **When** the player clicks "Cap" on a hotkey field and presses a key,
   **Then** the key is bound and the menu looks exactly as before the click.
2. **Given** the same, **When** the player clicks "Cap" 20 times in a row, on keyboard and gamepad fields, completing
   or cancelling each capture, **Then** the menu stays fully opaque, with no fading at any point.
3. **Given** a capture is in progress, **When** the player looks at the other hotkey fields, **Then** their capture
   buttons are shown disabled, as today, and they become available again when the capture ends.
4. **Given** the player did steps 1–2, **When** they open another mod's page in the SKSE menu, **Then** it looks
   normal.

---

### User Story 2 - The rest of the menu keeps working (Priority: P2)

The other controls of the plugin's settings that become disabled or highlighted depending on a state keep the same
look and behavior: the "- Vertex" button, "Apply changes", the modifier "M" button, the HUD anchor grid and the label
corner buttons.

**Why this priority**: it guards against regressions in the same menu. They are not reported as broken today.

**Independent Test**: use each of those controls 10 times in a row and check that the menu never fades or changes
look.

**Acceptance Scenarios**:

1. **Given** the slot shape editor, **When** the player removes vertices down to the minimum, **Then** "- Vertex"
   becomes disabled and the menu stays fully opaque.
2. **Given** pending changes, **When** the player clicks "Apply changes" repeatedly, **Then** it disables itself after
   applying and the menu stays fully opaque.
3. **Given** the highlighted selection buttons ("M", HUD anchor, label corner), **When** the player toggles them
   repeatedly, **Then** only the selected one is highlighted and the menu stays fully opaque.

---

### Edge Cases

- **Capture started and completed in the same frame**, by a key already held when "Cap" is clicked: the menu must
  stay balanced.
- **Capture started on one field, then "Cap" clicked on another**: today the other buttons are disabled during a
  capture, so it cannot happen. That must stay true.
- **Capture cancelled with the "X" button, or by closing the menu**: no fading, and the next capture works.
- **A capture of the wrong device** (a gamepad button pressed for a keyboard field): the capture keeps waiting, as
  today, and the menu does not fade.
- **A menu that has already faded** on a build without this fix: reopening the SKSE menu after the fix (or restarting
  the game) restores it. The fix prevents new fading; it does not need to repair an old state.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Starting a hotkey capture, any number of times, on any keyboard or gamepad field, MUST NOT change the
  opacity or the look of the plugin's settings or of the SKSE menu.
- **FR-002**: During a capture, the capture buttons of the other fields MUST stay disabled, and they MUST become
  available again when the capture ends (completed, cancelled, or the menu closed), as today.
- **FR-003**: Every place in the plugin's settings that disables, re-enables or highlights a control MUST decide the
  start and the end of that effect from the same value, so a click on that control cannot leave it unbalanced. The
  code review found one place that breaks this (Background item 2); the others already follow it (item 3).
- **FR-004**: The plugin MUST leave the shared SKSE menu in a balanced state after drawing its page, so other mods'
  menus are never affected.
- **FR-005**: `docs/TEST_MATRIX.md` MUST gain a case for repeated hotkey captures with the menu staying opaque, and
  keep 10.7 (hotkey rebinding) as the regression check.
- **FR-006**: No setting, INI key or saved data changes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 20 consecutive hotkey captures, mixing keyboard and gamepad fields and completed and cancelled
  captures: the menu stays fully opaque, with 0 visible fading.
- **SC-002**: After SC-001, another mod's page in the SKSE menu looks normal (1 out of 1 check).
- **SC-003**: Every capture in SC-001 binds the pressed key to the chosen field, or is cancelled when asked (100%).
- **SC-004**: The User Story 2 controls, 10 uses each: the menu never fades, and each control looks as it does today.
- **SC-005**: Regression: TEST_MATRIX 10.7 (hotkey rebinding) and 6.12 (the wheel ignored during capture) pass.

## Assumptions

- The report comes from the same in-game settings page. The "Clear" button it names belongs to the reporter's own
  example; this plugin's equivalent is the capture button (Background item 2).
- The fix is in the settings menu only. The capture logic itself, the hotkey storage and the HUD are not changed.
- **No setting or INI key changes**. It is a bug fix with no patch flag (Constitution Principle V).
- **In-game validation on the DEBUG build** (Constitution Principle VI):
  - The DEBUG DLL was blocked by Windows Smart App Control in specs 002–005.
  - That protection is now off on the user's machine (`VerifiedAndReputablePolicyState = 0`, checked 2026-11-20), so
    the DEBUG DLL is expected to load.
  - If Windows still blocks it (`skse64.log` error `000011C7` / 4551), validation falls back to the release build, as
    in specs 002–005. That needs the user's approval for this spec.
  - **Outcome**: the DEBUG DLL loaded but faulted during start-up (`fatal error occurred while loading plugin`), a
    DEBUG-only defect unrelated to this spec. Validation runs on the release build, approved by the user
    (Clarifications).
