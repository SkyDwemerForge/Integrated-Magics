# Feature Specification: Fix slot hotkeys that stop working after using the mouse wheel

**Feature Branch**: `fix/mouse-wheel-hotkeys` (created from `main` at `8fc41d1`)

**Created**: 2026-11-19

**Status**: Implemented and validated in game on the release DLL (2026-11-19; TEST_MATRIX 6.8–6.14). See the
tasks.md implementation log.

**Input**: User description: "resolver bug em que Teclas de atalho param de funcionar após utilizar o scroll do
mouse. Documentação sobre o problema pode ser encontrado em @.claude/tasks/integrated_magic_bugs_e_melhorias.md e
@.claude/tasks/bug-02-hotkeys-roda-mouse-exclusive.md. Crie uma nova branch a partir da main para essa nova spec"

## Background & Investigation Findings *(context)*

Sources: BUG-01 in `.claude/tasks/integrated_magic_bugs_e_melhorias.md`, `.claude/tasks/bug-02-hotkeys-roda-mouse-exclusive.md`
(two player reports) and a review of the input code on `main`.

1. **Symptom (reports)**:
   - After the player moves the mouse wheel, no slot hotkey works. Examples: zooming the camera, switching between
     first and third person, or scrolling items in QuickLoot IE.
   - The hotkeys come back only after the player opens a menu that pauses the game (Esc, Favorites, inventory,
     magic).
   - Arrow keys in QuickLoot IE do **not** cause it. Turning **Require exclusive hotkey** OFF makes the problem go
     away.
2. **Mechanism (confirmed in code)**:
   - The plugin keeps its own record of which keys and buttons are held. It uses this record to match slot
     hotkeys and to apply the Require exclusive hotkey rule ("only the combo's keys may be held").
   - For a wheel scroll, the game sends a "pressed" signal but never a "released" one. The plugin therefore
     records the wheel as held forever.
   - The plugin's existing stuck-key recovery checks the real state of the keyboard, gamepad and the first five
     mouse buttons. It does not check the wheel, so the wheel is never cleared.
   - With Require exclusive hotkey ON, every slot then sees an "extra key" held and refuses to activate.
   - Opening a pausing menu clears all held keys on close, which is why it "fixes" the problem.
3. **Why it looks patch-specific**: the wheel is recorded as held even with the patch OFF. Without the patch,
   though, nothing reads extra held keys, so there is no visible symptom.
4. **Related item resolved elsewhere**: bug-02's task list mentions a possible link with the Skip Equip Animations
   1.0.7 symptom. The consolidated document records that symptom as caused by an outdated Integrated Magic build and
   already resolved, so it is out of scope here.

## Clarifications

### Session 2026-11-19

- Q: Can the player use the mouse wheel as a slot hotkey? → A: No. The wheel never counts as a hotkey input and
  cannot be captured. This fix only stops the wheel from blocking hotkeys.
- Q: How is this fix validated in game while the DEBUG DLL fails to load? → A: On the release build. The plan
  records the Principle VI deviation again, and fixing the DEBUG load failure goes to a separate spec.
- Q: Was the problem ever reported with Require exclusive hotkey OFF? → A: Unknown. The scope stays the stuck wheel,
  plus a mandatory in-game test that repeats the first report with the patch OFF. If it reproduces, a separate spec
  is opened.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Hotkeys keep working after scrolling with Require exclusive hotkey ON (Priority: P1)

A player who uses Require exclusive hotkey zooms the camera or scrolls through QuickLoot IE, then presses a slot
hotkey. The slot activates on that first press, as if the wheel had never been touched. No menu has to be opened.

**Why this priority**: this is the reported bug. It makes every hotkey unusable until a menu is opened, and it
happens constantly in normal play.

**Independent Test**: turn Require exclusive hotkey ON, bind slots to keys 1–4, scroll the wheel, then press a slot
key. The slot activates.

**Acceptance Scenarios**:

1. **Given** Require exclusive hotkey is ON and slots are bound to keys 1–4, **When** the player zooms the camera
   with the wheel and then presses `1`, **Then** slot 1 activates on the first press.
2. **Given** the same setup and QuickLoot IE open on a container with more than one item, **When** the player
   scrolls the list with the wheel, closes it and presses a slot key, **Then** the slot activates.
3. **Given** the same setup, **When** the player scrolls many times in both directions and then presses each slot
   key in turn, **Then** every slot activates, without any menu being opened in between.
4. **Given** the same setup with a multi-key combo (for example `Shift + 1`), **When** the player scrolls and then
   presses the combo, **Then** the slot activates.

---

### User Story 2 - Scrolling during a hotkey press does not cancel it (Priority: P2)

A player presses or holds a slot hotkey and moves the wheel at the same moment, for example zooming the camera
while holding a Hold-mode slot. The slot behaves as if the wheel had not been touched.

**Why this priority**: once the wheel is no longer stuck, this is the remaining way the wheel could still interfere
with Require exclusive hotkey. Scrolling is common during combat.

**Independent Test**: with Require exclusive hotkey ON, hold a Hold-mode slot key, scroll the wheel while holding,
then release. The cast continues through the scroll and ends on release.

**Acceptance Scenarios**:

1. **Given** Require exclusive hotkey ON and a Hold-mode slot, **When** the player holds the slot key and scrolls
   the wheel, **Then** the slot stays active until the key is released.
2. **Given** Require exclusive hotkey ON and a multi-key combo, **When** the player scrolls while pressing the
   combo's keys, **Then** the slot activates as it would without the scroll.
3. **Given** a Press-mode slot that is active, **When** the player scrolls and then presses the slot key again,
   **Then** the slot deactivates as usual.

---

### User Story 3 - The wheel keeps working for the game and other mods (Priority: P3)

Camera zoom, first/third-person switching, QuickLoot IE scrolling and menu scrolling behave exactly as they do today.
Require exclusive hotkey keeps rejecting a slot when a real extra key is held.

**Why this priority**: the fix must not trade one bug for another. Players rely on the wheel and on the exclusive
rule.

**Independent Test**: scroll in each of those contexts with the plugin loaded, then run the exclusive-hotkey
regression (TEST_MATRIX 6.1 and 6.2).

**Acceptance Scenarios**:

1. **Given** the plugin is loaded, **When** the player zooms the camera, switches view or scrolls QuickLoot IE or a
   menu, **Then** each action responds exactly as without the plugin.
2. **Given** Require exclusive hotkey ON and a combo `R1 + L1`, **When** the player holds `R1 + L1 + R2`, **Then**
   the slot does **not** activate (unchanged behavior).
3. **Given** the player is rebinding a slot hotkey, **When** they scroll the wheel while the capture is waiting,
   **Then** the wheel is not recorded as the new hotkey.

---

### Edge Cases

- **Wheel in a slot combo**: the hotkey capture cannot record the wheel, so a wheel combo is only possible by
  editing the INI by hand. It is not supported (Clarification 2026-11-19): the wheel part never counts as pressed,
  so that slot cannot activate through it. Such a combo must not leave other slots blocked.
- **Scroll while a slot session is active** (casting, waiting for restore): the session continues normally, and
  hotkeys work as soon as it ends.
- **Scroll while a menu is open**: no effect on hotkeys after the menu closes (already true today, must stay true).
- **Release lost for a real key or button** (for example alt-tab while holding a key, or a synthetic press from
  another mod without a release): the key must not block hotkeys indefinitely. Stuck-key recovery clears it within
  the time set by FR-007, once the player no longer holds it.
- **Gamepad-only players**: unaffected, since the wheel is a mouse input.
- **Require exclusive hotkey OFF**: the visible behavior is unchanged, but the wheel is no longer recorded as held
  either. Whether the reported symptom also happens with the patch OFF is unknown (Clarification 2026-11-19). A
  mandatory test repeats the first report's steps with the patch OFF (SC-008). A failure there means a second cause,
  which goes to a separate spec.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A mouse wheel scroll, in either direction, MUST NOT leave the plugin treating any key or button as
  held. After the scroll, the plugin's record of held inputs MUST match what the player is actually holding.
- **FR-002**: With Require exclusive hotkey ON, a slot hotkey pressed after any number of wheel scrolls MUST
  activate on the first press, without the player opening a menu.
- **FR-003**: A wheel scroll that happens while a slot hotkey is being pressed or held MUST NOT cancel, block or
  end that hotkey. The wheel never counts as an "extra key" for the exclusive rule.
- **FR-004**: Wheel input MUST keep reaching the game and other mods unchanged (camera zoom, view switching,
  QuickLoot IE scrolling, menu scrolling). The plugin MUST NOT consume or alter it. The one exception is existing
  behavior and stays as it is: while a hotkey capture (rebinding) is waiting, the plugin withholds every button
  event from the game, the wheel included.
- **FR-005**: The hotkey capture used for rebinding MUST NOT record a wheel scroll as a hotkey.
- **FR-006**: Require exclusive hotkey MUST keep refusing a slot while a real extra key or button is held
  (TEST_MATRIX 6.1), and keep accepting the exact combo (6.2).
- **FR-007**: A key or button that the plugin still records as held after the player released it, for example
  because the release signal was lost, MUST be cleared automatically within 1 second while no slot is active. This
  covers every mouse button the game reports, not only the first five, so hotkeys never stay blocked until a menu
  is opened.
- **FR-008**: The patches' settings and defaults MUST NOT change: Require exclusive hotkey, Press both at the same
  time and the others keep their meaning, and no INI key is renamed (Constitution Principle V).
- **FR-009**: When Require exclusive hotkey refuses or cancels a slot, the debug log MUST name the extra key or
  button responsible, so future reports of "hotkey does nothing" can be diagnosed from a log
  (Constitution Principle VI).
- **FR-010**: `docs/TEST_MATRIX.md` MUST gain cases for this bug in section 6:
  - camera zoom, QuickLoot IE scroll, scroll during a held hotkey and scroll during capture, with the patch ON,
  - the first report's steps with the patch OFF: camera zoom and first/third-person switching, then keys 1–4.

  The existing cases 6.1–6.7 are the regression set.

### Key Entities

- **Held-input record**: the plugin's own view of which keyboard keys, mouse buttons and gamepad buttons are held
  right now. It is used by slot hotkey matching, the Require exclusive hotkey rule and the Press both at the same
  time rule. It must only contain inputs that can actually be held.
- **Stuck-key recovery**: the existing periodic check that clears inputs the record still shows as held after the
  player released them. It also runs when a pausing menu closes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With Require exclusive hotkey ON, 20 out of 20 slot hotkey presses activate their slot when each press
  comes right after a wheel scroll (10 camera zooms, 10 QuickLoot IE scrolls), with no menu opened during the
  test.
- **SC-002**: Every activation in SC-001 happens on the first press of the hotkey. No extra presses and no waiting
  are needed.
- **SC-003**: Scrolling while holding a Hold-mode slot ends the cast 0 times in 10 tries. The cast ends only when
  the key is released.
- **SC-004**: Camera zoom, view switching and QuickLoot IE scrolling respond as before in 10 out of 10 tries each.
- **SC-005**: Scrolling during hotkey rebinding records the wheel 0 times in 5 tries.
- **SC-006**: TEST_MATRIX section 6 (6.1–6.7) passes with no regressions.
- **SC-007**: Whenever Require exclusive hotkey refuses a slot in testing, the log names the extra input that caused
  it (100% of refusals).
- **SC-008**: With Require exclusive hotkey OFF, 10 out of 10 presses of keys 1–4 activate their slot when each one
  follows a camera zoom or a first/third-person switch. Any failure is recorded and opens a separate spec; it does
  not block this one.

### Verification (TEST_MATRIX)

- **New** (section 6):
  - with Require exclusive hotkey ON: camera zoom then hotkey, QuickLoot IE scroll then hotkey, scroll during a
    held Hold-mode hotkey, and scroll during capture,
  - with the patch OFF (mandatory): the first report's steps (SC-008).
- **Regression**: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7.

## Assumptions

- **The wheel is not a supported hotkey** (confirmed, Clarification 2026-11-19). The rebinding capture in the
  settings menu only records keys and mouse buttons, never the wheel, and this spec keeps it that way. Binding the
  wheel to a slot (for example "scroll up casts") is out of scope.
- The QuickLoot IE report confirms that turning Require exclusive hotkey OFF fixes the problem. The camera-zoom
  report does not say, and whether the symptom happens with the patch OFF is unknown (Clarification 2026-11-19).
  The scope is the stuck wheel. SC-008 checks the patch-OFF case, and a failure there goes to a separate spec.
- The consolidated note "it can happen even when the wheel is not bound to the camera" is covered, because the cause
  is the wheel signal itself, not what the game does with it.
- QuickLoot IE and camera zoom stand for every use of the wheel. Any other mod that uses the wheel benefits from the
  same fix.
- The Skip Equip Animations 1.0.7 symptom mentioned in bug-02 is out of scope (resolved, see Background item 4).
- **In-game validation and Constitution Principle VI** (Clarification 2026-11-19): the principle requires a DEBUG
  build, but the DEBUG DLL currently fails to load (see spec 002, plan Complexity Tracking).
  - In-game validation runs on the **release** DLL, which writes the same `[Input]` log lines.
  - The plan records the deviation in its Complexity Tracking.
  - Fixing the DEBUG load failure is out of scope here and goes to a separate spec.
