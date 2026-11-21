# Feature Specification: The SKSE menu must not react to input pressed during a hotkey capture

**Feature Branch**: `fix/capture-input-leak` (created from `main` at `c4fc06f`, after spec 008 merged as #77)

**Created**: 2026-11-21

**Status**: Implemented. Validated in game on 2026-11-21 (12 gamepad captures with no SKSE menu reaction); see the
implementation log in `tasks.md`. TEST_MATRIX 10.18–10.22 stay open for the full button set, the keyboard, the other
device, `Escape` and closing the menu.

**Input**: User description: "O SKSE menu esta reagindo ao pressionar botões do gamepad durante a captura de input
ativada. Ele não deveria reagir ao input pressionado durante a captura."

## Background & Investigation Findings *(context)*

Source: a review of the hotkey capture code on `main` (`c4fc06f`).

1. **How a capture works**: in the plugin's page of the SKSE menu, the player clicks "Cap" next to a hotkey field.
   The next key, mouse button or gamepad button pressed becomes the field's value. "X" cancels the capture.
   - **Keyboard fields** take keys and mouse buttons.
   - **Gamepad fields** take gamepad buttons and triggers.
   - **The other device**: today a press from it is ignored, and the capture keeps waiting. This spec changes that
     to a cancel (FR-010).
2. **Where the captured input comes from**: three places feed the capture.
   - **Game input events**: keyboard, mouse and gamepad presses that reach the game's input.
   - **Window messages**: key presses and mouse clicks.
   - **A direct read of the gamepad state** once per frame, which is how gamepad buttons are captured while the SKSE
     menu is open.
3. **What already keeps captured input from the game**: while a capture waits, the plugin removes the button
   events from the game's input, but only when no input-blocking menu is open.
4. **What the player sees**: the SKSE menu still reacts to the gamepad button pressed for the capture. It may
   navigate, activate the focused control, go back or close. The menu has its own way of reading the gamepad, which
   the plan must identify (the SKSE Menu Framework offers an input callback that can block events).
5. **Related behavior already validated**: spec 006 fixed the SKSE menu fading during captures (TEST_MATRIX 10.9), and
   spec 003 keeps the mouse wheel out of captures (6.12).

## Clarifications

### Session 2026-11-21

- Q: During a capture, what should happen with an input from the other device (a gamepad button while a keyboard
  field waits, or a key while a gamepad field waits)? → A: The SKSE menu does not receive it, and the capture is
  cancelled with no value (FR-010).
- Q: The DEBUG DLL still faults during start-up. May this spec be validated in game on the release build
  (Constitution Principle VI)? → A: Yes, approved by the user.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Capturing a gamepad button does not drive the SKSE menu (Priority: P1)

A player sets a slot's gamepad hotkey from the SKSE menu. They click "Cap" and press the button they want, for
example `B`, `Y`, a D-pad direction, a shoulder button or a trigger. The field shows the button, and the SKSE menu does
nothing else: it does not go back or close, does not move the focus, does not switch tabs and does not activate a
control.

**Why this priority**: this is the reported bug. Buttons like `B` (back) or the D-pad are among the most common hotkey
choices, and today pressing them during a capture also acts in the menu.

**Independent Test**: open the plugin's page in the SKSE menu with a gamepad, capture each of the 16 gamepad buttons
and triggers into a gamepad field, and watch the menu.

**Acceptance Scenarios**:

1. **Given** a capture waits on a gamepad field, **When** the player presses a gamepad button, **Then** the field takes
   that button and the SKSE menu does not react to the press, to the button being held, or to its release.
2. **Given** the same, **When** the player presses `B`, **Then** the menu does not go back or close, and the capture
   ends with `B` in the field.
3. **Given** the same, **When** the player presses a D-pad direction or a shoulder button, **Then** the focus and the
   tab stay where they were.

---

### User Story 2 - Capturing a key or mouse button does not drive the SKSE menu (Priority: P2)

The same holds for keyboard fields: the key or mouse button pressed for the capture goes into the field, and the SKSE
menu does not act on it (for example, navigation keys or `Enter`).

**Why this priority**: the report names the gamepad, but the rule is about any input pressed during a capture. It is
P2 because no keyboard case was reported.

**Independent Test**: capture navigation keys (arrows, `Enter`, `Space`, `Tab`) and mouse buttons into a keyboard field
and watch the menu.

**Acceptance Scenarios**:

1. **Given** a capture waits on a keyboard field, **When** the player presses a key or a mouse button, **Then** the
   field takes it and the menu does not react to it, while it is held, or at its release.

---

### User Story 3 - The SKSE menu works as before outside captures (Priority: P2)

Outside a capture, the SKSE menu reacts to the gamepad, keyboard and mouse as before, including the "Cap" and "X"
buttons and the rest of the plugin's page.

**Why this priority**: the fix must not take input away from the menu when no capture waits.

**Independent Test**: navigate the whole plugin page with the gamepad and the mouse, start and cancel captures, and
change settings.

**Acceptance Scenarios**:

1. **Given** no capture waits, **When** the player uses the gamepad or the keyboard in the SKSE menu, **Then** the menu
   reacts as before.
2. **Given** a capture just ended, **When** the player presses any button again (after releasing the captured one),
   **Then** the menu reacts to it normally.

---

### Edge Cases

- **The button that started the capture** (for example, gamepad `A` on "Cap"): it must not be taken as the captured
  input, and its release must not act in the menu. The plan checks today's behavior.
- **Holding the captured button after the capture ends**: the capture ends on the press. The button's held input and
  its release still must not reach the menu, until it is released.
- **Input from the other device during a capture** (a gamepad button while a keyboard field waits, or a key or mouse
  button while a gamepad field waits): the SKSE menu does not receive it, and the capture is cancelled with no value
  (FR-010). So the menu never reacts during a capture, and a gamepad-only player is never stuck in a keyboard-field
  capture.
- **Cancelling**: a capture ends when its value is stored, when the other device or `Escape` cancels it, when the slot
  selection changes, or when the plugin's page is no longer shown (the SKSE menu closed, or another section opened). A
  capture never leaves input blocked after it ends. "X" stays on screen, but during a capture the mouse is the input
  being captured: a click is the value in a keyboard field, and it cancels a gamepad field (FR-010).
- **`Escape`**: today it is captured into keyboard fields through the window-message path (research R7). It must not be
  a hotkey value: it cancels the capture. During a capture the SKSE menu does not see `Escape`, so it stays open
  (research R12). Outside captures, `Escape` closes it as always.
- **The mouse wheel**: keeps its current behavior during captures (spec 003).
- **Several captures in a row** (TEST_MATRIX 10.9): each one behaves the same, and the menu stays fully visible.
- **A second gamepad**: only the first gamepad is read for captures today. This spec does not change that.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: While a capture waits, the input pressed for the capture MUST NOT trigger any SKSE menu action:
  navigation, activation, back or close, or tab switch. This holds for gamepad buttons and triggers, keys and mouse
  buttons.
- **FR-002**: The captured input's held state and release MUST NOT trigger SKSE menu actions either, even when they
  arrive after the capture has ended, until that input is released.
- **FR-003**: The captured value MUST stay exactly as today: the same button or key goes into the field.
- **FR-004**: The input used to start a capture MUST NOT be taken as the captured value.
- **FR-005**: Outside captures, the SKSE menu MUST receive every input as before.
- **FR-006**: A capture MUST end on every path: a value stored, the other device (FR-010), `Escape`, a change of slot
  selection, or the plugin's page no longer shown. The SKSE menu MUST get all its input back once the captured input
  is released, in every case.
- **FR-011**: `Escape` MUST NOT be captured as a value. During a capture it cancels the capture.
- **FR-007**: Each decision to keep an input from the SKSE menu MUST be logged with a prefix (Constitution VI).
- **FR-008**: `docs/TEST_MATRIX.md` MUST gain cases for gamepad and keyboard captures with no menu reaction, and for the
  menu working normally right after a capture.
- **FR-009**: No setting, INI key or saved data changes. This is a bug fix with no patch flag.
- **FR-010**: An input from the other device during a capture MUST NOT trigger any SKSE menu action, with its held
  state and release included, as in FR-002. It MUST cancel the capture and leave the field unchanged. That covers a
  gamepad button or trigger during a keyboard-field capture, and a key or mouse button during a gamepad-field capture.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Capturing each of the 16 gamepad buttons and triggers once into a gamepad field gives 16 correct values
  and 0 SKSE menu reactions (no navigation, activation, back, close or tab change).
- **SC-002**: Capturing 10 navigation keys and the 3 main mouse buttons into a keyboard field gives the correct values
  and 0 SKSE menu reactions.
- **SC-003**: After each capture, the next press of any button acts in the SKSE menu as usual, 10 out of 10 times.
- **SC-004**: 20 captures in a row, completed and cancelled, leave the SKSE menu fully visible and responsive
  (TEST_MATRIX 10.9 still passes).
- **SC-005**: 5 gamepad presses during a keyboard-field capture and 5 key presses during a gamepad-field capture each
  cancel the capture, leave the field unchanged and cause 0 SKSE menu reactions.

## Assumptions

- **Scope**: the plugin's hotkey capture in the SKSE menu only. Other mods' pages and the rest of the SKSE menu are
  unchanged outside captures.
- **The SKSE Menu Framework's input can be paused**: it reads input from the same hook site as the plugin. A hook
  installed after it can hand it an empty event list during a capture (research R12).
- **Validation build**: as in specs 002–008, the DEBUG DLL faults at start-up (a separate defect), so validation runs on
  the release build, which writes the same logs. Approved by the user (Clarifications).
