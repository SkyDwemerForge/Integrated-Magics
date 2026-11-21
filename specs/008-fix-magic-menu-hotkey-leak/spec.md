# Feature Specification: Slot and wheel hotkeys must not trigger Magic Menu actions

**Feature Branch**: `fix/magic-menu-hotkey-leak` (created from `main` at `f33b99f`, after spec 007 merged as #76)

**Created**: 2026-11-21

**Status**: Implemented. Validated in game on 2026-11-21 (quick bind, wheel, overlapping hotkeys); see the implementation
log in `tasks.md`

**Input**: User description: "bug em que o metodo de quick bound, ou seja, pressionar a hotkey dando hover em uma spell
esta ativando funções no magic menu. Para isso vamos usar o filter com replay quando esta no magic menu. Para o magic
menu especificamente vamos usar um filter um pouco maior de 0.3s, mas apenas nele que usamos esse filter maior. Quando
o combo é detectado então não enviamos as teclas correspondentes de volta, então o magic menu nunca reage a tecla
pressionada. A mesma coisa se aplica ao botão usado para ativar a wheel do mod, seja com teclado ou controle.", plus
"crie uma nova branch a partir da main para essa spec."

## Background & Investigation Findings *(context)*

Source: a review of the input, HUD and assignment code on `main` (`f33b99f`).

1. **Quick bind**: with the Magic Menu open and the mod's wheel (the assignment popup) closed, the HUD checks every
   frame whether all the keys of a slot's hotkey are held. When one becomes fully held, the hovered spell, shout or
   power is assigned to that slot. The check reads the recorded key state, which is updated before any filtering.
2. **Nothing is filtered in the Magic Menu**:
   - The Magic Menu is one of the menus that block slot activation, so no slot casts while it is open. That is
     intended.
   - The same "input blocked" state also skips the whole input filter. So no slot key is held back or consumed, and the
     Magic Menu receives every press of a quick-bind combo, as if it were a plain key press.
   - The menu then runs whatever function it binds to those keys. That is the reported bug.
3. **The wheel toggle**:
   - Pressing the wheel combo in the Magic Menu opens or closes the assignment popup.
   - A filter meant to keep that combo from the Magic Menu exists, but it is only called from the input filter, which
     the Magic Menu skips (item 2). So it never runs, and the menu also reacts to the wheel combo, with keyboard and
     gamepad.
4. **The filter with replay** (spec 004), used outside menus today:
   - A fresh press of a key that belongs to a slot combo is held back for a short window (50 ms, extended while a
     combo is still being pressed).
   - If a combo completes, its keys are consumed: their presses, held repeats and releases never reach the game.
   - Otherwise the held-back events are re-injected into the normal input stream, in order. A menu receives
     re-injected events like real ones, so the same mechanism can serve the Magic Menu.

## Clarifications

### Session 2026-11-21

- The design comes from the user's request: the filter with replay in the Magic Menu, with a 0.3 s window used in the
  Magic Menu only, and the combo's keys never sent back once the combo is detected. The wheel toggle gets the same
  treatment, on keyboard and gamepad.
- Q: The DEBUG DLL still faults during start-up. May this spec be validated in game on the release build
  (Constitution Principle VI)? → A: Yes, approved by the user.
- Q: The plan adds one rule to the design: a held-back key that is **released** before its 0.3 s goes to the menu at
  its release, without waiting for the rest of the window. A released key can no longer complete a hotkey, so the
  outcome never changes. Keep it? → A: Yes, keep it.
- After the first in-game test, the user reported that with overlapping hotkeys (`R2 + L2 + X` on one slot and
  `R2 + X` on another), pressing the longer one over a spell gave the spell to both slots. Only the slot with the
  longer hotkey should receive it. It is in scope (FR-013), with the same rule as gameplay (spec 004): the longest
  hotkey wins, and the lower slot wins between identical ones.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Quick bind without side effects in the Magic Menu (Priority: P1)

A player opens the Magic Menu, hovers a spell and presses a slot's hotkey to put that spell in the slot. The spell is
assigned, and the Magic Menu does nothing else: it does not equip the spell, mark it as a favorite, switch category or
run any other function bound to those keys. This holds on keyboard, mouse buttons and gamepad, for single-key and
multi-key hotkeys.

**Why this priority**: this is the reported bug. Quick bind is the fastest way to fill the slots, and today it also
triggers menu actions the player did not ask for.

**Independent Test**: open the Magic Menu, hover a spell and press each configured slot hotkey (keyboard and gamepad,
including the default gamepad layout that shares a modifier button). Each press assigns the hovered spell to its slot,
and the menu shows no other reaction.

**Acceptance Scenarios**:

1. **Given** the Magic Menu is open with a spell hovered, **When** the player presses a multi-key slot hotkey with all
   its keys within 0.3 s, **Then** the spell is assigned to that slot and the menu does not react to any of the keys:
   not on the press, not while they are held, not on their release.
2. **Given** the same, **When** the player presses a single-key slot hotkey, **Then** the spell is assigned and the
   menu does not react to that key.
3. **Given** a gamepad with the default layout (every slot shares one modifier button), **When** the player
   quick-binds with modifier + face button, in either order within 0.3 s, **Then** the spell is assigned and neither
   button triggers a menu function.
4. **Given** a shout or power is hovered, **When** the player presses a slot hotkey, **Then** it is assigned to the
   slot's voice entry, with no menu reaction, as for spells.

---

### User Story 2 - The wheel toggle does not trigger Magic Menu actions (Priority: P1)

In the Magic Menu, the player presses the wheel hotkey to open the assignment wheel, and presses it again to close it.
The wheel opens and closes, and the Magic Menu does not react to that hotkey, on keyboard or gamepad.

**Why this priority**: same bug, same cause. The wheel is the other way to assign spells from the Magic Menu.

**Independent Test**: open the Magic Menu, then open and close the wheel 10 times with the keyboard hotkey and 10 times
with the gamepad hotkey. The wheel toggles each time and the menu shows no other reaction.

**Acceptance Scenarios**:

1. **Given** the Magic Menu is open and the wheel closed, **When** the player presses the wheel hotkey, **Then** the
   wheel opens and the menu does not react to the hotkey's keys.
2. **Given** the wheel is open, **When** the player presses the wheel hotkey, **Then** the wheel closes and the menu
   does not react.

---

### User Story 3 - Normal Magic Menu use and gameplay stay as they are (Priority: P2)

The player uses the Magic Menu normally, including buttons that are also part of a slot or wheel hotkey (for example,
the gamepad modifier button pressed on its own). Each such button still does its usual menu action, only a little
later (up to 0.3 s). Buttons that are not part of any hotkey are not delayed at all. Outside the Magic Menu, slot
hotkeys behave exactly as before.

**Why this priority**: the fix must not take away Magic Menu functions or change gameplay timing. It is P2 because it
guards the change rather than adding value of its own.

**Independent Test**: in the Magic Menu, press each button that belongs to a hotkey on its own, and a few that belong
to none. Then play with the slots outside the menu and run the TEST_MATRIX 6.x cases.

**Acceptance Scenarios**:

1. **Given** the Magic Menu is open, **When** the player presses on its own a button that is part of a hotkey and does
   not complete any hotkey within 0.3 s, **Then** the menu performs that button's usual action, within 0.3 s of the
   press, exactly once.
2. **Given** the Magic Menu is open, **When** the player presses a button that is part of no hotkey, **Then** the menu
   reacts at once, with no added delay.
3. **Given** the Magic Menu is open, **When** the player holds a hotkey button on its own (for example, to scroll),
   **Then** the menu receives the press after at most 0.3 s and then the held input, and the button is never left stuck.
4. **Given** the player is in gameplay (no menu), **When** they use slot hotkeys, **Then** combo timing, activation and
   which presses reach the game are unchanged (the 0.3 s window is never used outside the Magic Menu).

---

### Edge Cases

- **Keys pressed more than 0.3 s apart** in the Magic Menu: the first key reaches the menu once its window ends, as a
  plain press. If the hotkey then completes, the quick bind still happens, and the keys pressed while the hotkey is
  fully held are kept from the menu. This matches the current rule outside menus, with a longer window.
- **The wheel is open** (the Magic Menu is hidden behind it): the wheel hotkey and the slot hotkeys are still kept from
  the Magic Menu. The wheel's own controls are unchanged.
- **The Magic Menu closes while a press is held back** (for example, it is closed by another key, or by a script): the
  held-back press is dropped, and no key is left stuck in the game or reported as held by the plugin.
- **A key pressed before the Magic Menu opened** and released inside it: its release is not held back, so whatever
  received the press also receives the release.
- **Two hotkeys that share keys** (for example, `R2 + X` and `R2 + L2 + X`):
  - none of their keys reach the menu once a hotkey completes;
  - only the longer hotkey's slot receives the entry (FR-013);
  - when the shorter one completes first, it waits up to 0.3 s for the longer one, and gets the entry only if the
    longer one does not complete in that time. It gets it at once if its keys are released first;
  - between two slots with identical hotkeys, the lower slot receives it.
- **A hotkey that contains a mouse button**: the same rule applies to that mouse button. Mouse movement and the mouse
  wheel are never held back.
- **Other menus** (Inventory, Favorites, the SKSE menu and others): unchanged. They have no quick bind, and their input
  keeps today's behavior.
- **Hotkey capture** (rebinding a hotkey in the SKSE menu): unchanged.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: While the Magic Menu is open, a fresh press of a key, mouse button or gamepad button that belongs to any
  slot hotkey or to the wheel hotkey MUST be held back from the Magic Menu for up to 0.3 s.
- **FR-002**: When the hotkey it belongs to completes within that window, the held-back keys of that hotkey MUST be
  consumed: their presses, held repeats and releases MUST never reach the Magic Menu. A key pressed while its hotkey is
  already fully held MUST be consumed at once.
- **FR-003**: When the window ends without a completed hotkey, or the key is released before it ends, the held-back
  events MUST be delivered to the Magic Menu in their original order. The later events of those keys (held input,
  release) MUST follow, so the menu performs the key's usual action exactly once.
- **FR-004**: The 0.3 s window MUST be used only while the Magic Menu is open. Outside it, the existing windows of spec
  004 (50 ms tolerance and its extensions) MUST stay as they are.
- **FR-005**: Input that belongs to no hotkey MUST reach the Magic Menu unchanged and with no added delay. Mouse
  movement, thumbsticks and the mouse wheel are never held back.
- **FR-006**: Quick bind MUST keep working as today: the hovered spell, shout or power is assigned to the slot whose
  hotkey becomes fully held, on keyboard and gamepad, with the wheel closed.
- **FR-007**: The wheel hotkey MUST still open and close the wheel in the Magic Menu, on keyboard and gamepad, and its
  keys MUST follow FR-001 to FR-003.
- **FR-008**: No slot may activate (cast) while the Magic Menu is open, as today.
- **FR-009**: No key may be left stuck: after the Magic Menu closes, or a press is consumed or dropped, neither the game
  nor the plugin may treat a released key as held.
- **FR-010**: Each decision MUST be logged with a prefix: a press held back in the Magic Menu, consumed by a hotkey
  (with the slot or "wheel"), or handed to the menu when the window ends (Constitution VI).
- **FR-011**: `docs/TEST_MATRIX.md` MUST gain cases for quick bind and the wheel toggle in the Magic Menu (keyboard and
  gamepad, single-key and multi-key hotkeys), for a hotkey button used alone in the menu, and for no stuck key after
  closing the menu.
- **FR-012**: No setting, INI key or saved data changes. The 0.3 s window is fixed. This is a bug fix with no patch flag.
- **FR-013**: When a quick bind completes hotkeys that overlap, only the longest one MUST receive the entry. Identical
  hotkeys go to the lower slot. A hotkey contained in a longer one MUST wait up to 0.3 s, or until its keys are
  released, so a longer hotkey completed in that time still wins.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In the Magic Menu, 20 quick binds with keyboard hotkeys and 20 with gamepad hotkeys (single-key and
  multi-key, including the default gamepad layout) assign the hovered entry every time, with 0 menu reactions.
- **SC-002**: In the Magic Menu, 10 openings and 10 closings of the wheel with the keyboard hotkey and the same with
  the gamepad hotkey toggle the wheel every time, with 0 menu reactions.
- **SC-003**: In the Magic Menu, each button that belongs to a hotkey, pressed alone 10 times, performs its usual menu
  action 10 out of 10 times, each within 0.3 s of the press and never twice.
- **SC-004**: In the Magic Menu, buttons that belong to no hotkey react with no perceptible delay compared with the
  same menu without the plugin.
- **SC-005**: Outside the Magic Menu, the input cases of TEST_MATRIX section 6 (6.1–6.23) still pass, with no change in
  combo timing.
- **SC-006**: After 10 closings of the Magic Menu, some of them with a hotkey key still held, no key is stuck: the next
  gameplay inputs and slot activations work at the first press.
- **SC-007**: With two overlapping hotkeys (a 2-button one contained in a 3-button one), 10 quick binds with the longer
  hotkey, in any press order, give the entry only to its slot. 10 with the shorter one give it only to the shorter
  hotkey's slot.

## Assumptions

- **Scope**: the Magic Menu only, because it is the only menu with quick bind and the wheel. Other menus keep today's
  input behavior.
- **Detection is unchanged**: quick bind keeps detecting a hotkey as "all its keys held", whatever the patch settings
  (Require exclusive hotkey, Press both at same). This spec only decides which key events the Magic Menu receives.
- **A short delay is acceptable**: a hotkey button used on its own in the Magic Menu may act up to 0.3 s later. The
  user chose this window to give the player time to complete a combo.
- **Replay reaches menus**: re-injected events go through the normal input stream, so the Magic Menu receives them as
  real input (Background item 4). The plan confirms it in game.
- **Validation build**: as in specs 002–007, the DEBUG DLL faults at start-up (a separate defect), so validation runs on
  the release build, which writes the same logs. Approved by the user (Clarifications).
