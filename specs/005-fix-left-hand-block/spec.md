# Feature Specification: Fix left-hand casting stuck by the block button

**Feature Branch**: `fix/left-hand-block` (created from `main` at `c001181`, after spec 004 merged as #73)

**Created**: 2026-11-20

**Status**: Implemented and validated in game (user, 2026-11-20, release DLL). See tasks.md implementation log

**Input**: User description: "Corrigir o bug da mão esquerda não estar funcionando apropriadamente por causa do bug.
Tem informações sobre esse problema reportado em @.claude/tasks/bug-04-mao-esquerda-block.md e
@.claude/tasks/integrated_magic_bugs_e_melhorias.md no bug 03"

## Background & Investigation Findings *(context)*

Sources: `.claude/tasks/bug-04-mao-esquerda-block.md` (two player reports, plus a note added by the user),
BUG-03 in `.claude/tasks/integrated_magic_bugs_e_melhorias.md`, a review of the cast and input code on
`fix/left-hand-block` (`main` + spec 004), and the user's own tests (2026-11-20).

1. **Symptom (reports)**: left-hand casting enters a state where the cast keeps starting and being interrupted
   ("twitching"), and no spell comes out.
   - **Entering it**: holding the block button, and entering the block state, while holding the casting hotkey.
   - **Leaving it**: clicking the block button quickly a few times while holding the casting hotkey.
   - **The state lasts**: once in it, later left-hand casts keep failing until the player uses that workaround.
   - The block button is the right mouse button on keyboard/mouse. On a gamepad it is the left trigger (`LT`).
2. **Same game action (code review)**: the plugin casts a hand by sending the game the same input the player's
   attack/block button sends. For the left hand that is the game's "left attack / block" action, the one the right
   mouse button and `LT` produce. The game cannot tell the plugin's input from the player's button.
3. **Nothing coordinates them (code review)**: during a session the plugin neither filters nor tracks the player's
   own attack/block buttons. Their presses, held repeats and releases reach the game while the plugin is holding or
   releasing the same action. So:
   - a release of the player's button can end the cast the plugin is holding,
   - a button already held can make the game see the action as held when the plugin presses it,
   - when the plugin releases the action at the end of a session while the player still holds the button, the game
     and the button disagree about whether it is held.
4. **Retry loop (code review)**: while a hand waits for its cast to start, the plugin releases and re-presses the
   cast input every 0.15 s, and again after every early interruption. Nothing limits this except the end of the
   session or its 30 s safety timeout. This repeated restart matches the "twitching" in the reports.
5. **An interrupted cast leaves the session open (code review)**: when the game interrupts a cast that had already
   started, the plugin releases that hand's input and marks the hand as done, but it never closes the session. The
   slot then stays active with nothing happening: until the 30 s safety timeout in Automatic mode, and until the
   hotkey is released in Hold mode.
6. **The user's tests (2026-11-20)**, with a spell in the right hand and a shield in the left:
   - **Blocking does not cancel the slot.** Raising the shield during a Hold or Automatic slot does not end it.
     Automatic stays active "indefinitely" and Hold stays active until the hotkey is released, with no more casting.
     This matches item 5.
   - The plugin cancels Hold and Automatic sessions when the game reports a block start. In this test that did not
     happen, for a cause not yet known. So the cancel must not depend on that game event alone.
   - **The slot cannot be activated while blocking.** With Require exclusive hotkey ON (the default INI), the held
     block button counts as an extra key, so the slot is refused. That is the patch's meaning and stays as it is
     (Clarifications). With the patch OFF, the game cannot cast the right hand while the shield is up.
   - **A left-hand slot cannot block.** With a spell in the left hand, the block button casts that hand, as in
     vanilla. Blocking while a left-hand spell is equipped is only possible with mods that add their own block key.
7. **Working hypothesis** for the reported twitching (items 2–4), to confirm with the baseline log (quickstart §0):
   - The game's view of the left attack/block action gets out of step with the button.
   - The cast never reaches its stable state, and the retry loop shows it as twitching.
   - Clicking block sends fresh presses and releases that bring the game back in step, which explains the workaround.
   - The second report (Automatic mode, left hand: equip → cast → unequip with nothing cast) may be the same
     desynchronization.
8. **Related observation**: during the spec 004 tests, a right-hand Hold slot activated mid-jump kept retrying for
   about 1.7 s after landing and never cast. It is the same retry loop with a different trigger.
9. **What the reports imply** (no preference is stated in them): the players hold the casting hotkey and use block in
   the middle of it as normal play, expect no twitching, and expect the next cast to work with no workaround.

## Clarifications

### Session 2026-11-20

- Q: What should a press of the block button do while the plugin is casting that hand? → A: In Hold and Automatic,
  it **cancels the session** and restores the previous gear. To block, the player presses block again. In Press mode
  it has no effect; the second press of the hotkey ends the session.
- Q: What should happen when a hand's cast cannot start? → A: After **about 1 s** of failed starts, the session ends
  and the previous gear is restored. Retries before that keep working, so a cast that becomes possible in time (for
  example on landing) still happens.
- Q: Is in-game validation on the release build approved for this spec (Constitution Principle VI)? → A: Yes.
- Q: May this spec also fix the transform-shout forced exit that is computed but never executed (research R6)? → A:
  Yes.
- Q: What should the attack/block button of the **other** hand (the one the slot does not use, for example a shield)
  do during a Hold or Automatic slot? → A: It **cancels the slot, and the press reaches the game**, so the character
  blocks (or attacks) at once. A block press and a left-hand attack press cannot be told apart from the button, so
  both cancel. In Press mode it has no effect on the slot and works as in vanilla.
- Q: With Require exclusive hotkey ON, should holding the block button still prevent activating a slot? → A: **Yes,
  unchanged.** The held block button keeps counting as an extra key. Activating a slot while blocking only works with
  that patch OFF.

## User Scenarios & Testing *(mandatory)*

In this spec, **the plugin is casting a hand itself** when it drives that hand's cast input: Automatic mode, or Hold
or Press with auto-attack on. With auto-attack off, the player casts with their own button and the plugin does not
drive the hand. **The other hand** is the hand of the session the plugin is not casting, for example a shield.

### User Story 1 - Left-hand slots cast correctly around the block button (Priority: P1)

A player uses a slot with a left-hand spell, or a dual-cast slot, while also using the block button: blocking with a
shield just before activating the slot, or pressing block while the plugin is casting. The spell is cast as its mode
says, with no twitching, on keyboard/mouse and on gamepad.

**Why this priority**: this is the reported bug. It makes left-hand casting unusable for players who block, which is
most melee-hybrid builds.

**Independent Test**: on keyboard/mouse and on gamepad, with a shield in the left hand and a left-hand spell slot in
Hold (auto-attack on) and Automatic modes, perform the two sequences from the report 10 times each.

**Acceptance Scenarios**:

1. **Given** a shield in the left hand, the player holding block and Require exclusive hotkey OFF, **When** the player
   activates a slot with a left-hand spell, **Then** blocking stops, the spell is cast as its mode says, and the hand
   does not twitch.
2. **Given** the plugin is casting a left-hand spell (Hold with auto-attack, or Automatic), **When** the player
   presses and holds the block button, **Then** the session is cancelled and the previous gear is restored, with no
   twitching. The character does not block until the player presses block again.
3. **Given** the plugin is casting a left-hand spell in Press mode with auto-attack, **When** the player presses
   block, **Then** the press has no effect and the cast goes on, with no twitching.
4. **Given** the same as scenarios 2 and 3, **When** the player clicks block several times quickly, **Then** the hand
   does not twitch, and the outcome is the one of scenario 2 (Hold, Automatic) or 3 (Press).
5. **Given** a dual-cast slot (the same spell in both hands), **When** scenarios 1–4 are repeated, **Then** the
   spell comes out as a single dual cast, never as two single casts, or the session is cancelled as in scenario 2.

---

### User Story 2 - Blocking with the other hand ends a slot at once (Priority: P1)

A player uses a slot with a right-hand spell while holding a shield (or a weapon) in the left hand, and raises the
shield during the slot. The slot ends at once, the previous gear comes back, and the character blocks. A slot never
stays active after its cast was interrupted.

**Why this priority**: the user reproduced it in game. Today the slot stays active with nothing happening, which
strands the player in combat.

**Independent Test**: with a right-hand spell slot in Hold (auto-attack on) and Automatic modes and a shield in the
left hand, press block during the slot 10 times each, on keyboard/mouse and on gamepad.

**Acceptance Scenarios**:

1. **Given** a Hold or Automatic slot with a right-hand spell and a shield in the left hand, **When** the player
   presses block, **Then** the slot ends at once, the previous gear is restored, and the character blocks while the
   button stays held.
2. **Given** the same slot in Press mode with auto-attack, **When** the player presses block, **Then** the press does
   not cancel the slot, and the block works as in vanilla. If the block interrupts a cast still in progress,
   scenario 3 applies.
3. **Given** any slot whose cast the game interrupts after it started (block from a mod's own key, magicka running
   out, a hit), **When** the interruption happens, **Then** the slot ends and the previous gear is restored. It never
   stays active until the safety timeout or the hotkey release.
4. **Given** a shield up (block held) and Require exclusive hotkey **OFF**, **When** the player activates a
   right-hand spell slot, **Then** the shield goes down and the spell is cast. The held button does nothing more until
   it is released and pressed again.
5. **Given** the same with Require exclusive hotkey **ON**, **When** the player activates the slot, **Then** the slot
   is refused, as the patch defines.

---

### User Story 3 - No stuck state after using block (Priority: P1)

After any use of the block button, before, during or after a session, the next casts work on the first try. The player
never needs the "click block a few times" workaround.

**Why this priority**: the lasting state is what makes the bug severe. A single failed cast is a nuisance, but a hand
that stays broken until a trick is found is a blocker.

**Independent Test**: perform the sequences of User Stories 1 and 2, then activate the same slot 10 more times
without touching block.

**Acceptance Scenarios**:

1. **Given** the player just performed any sequence of User Stories 1 or 2, **When** they activate the slot 10 times
   without touching block, **Then** every activation casts on the first try.
2. **Given** a session ends (normal finish, release, cancel, stagger or menu) while the player still holds a block
   button the plugin had taken, **When** the hand gets its shield or weapon back, **Then** nothing stays held in the
   game: the character does not keep casting or blocking on its own, and a new press of block blocks normally.

---

### User Story 4 - A cast that cannot start does not twitch forever (Priority: P2)

When the game does not let a hand's cast start (for example mid-air, or while the game is still in another action),
the plugin does not restart the cast endlessly while the session lasts.

**Why this priority**: it bounds the visible symptom even when the cause is something other than block, as in the
spec 004 jump observation. The P1 stories fix the reported causes.

**Independent Test**: activate a slot whose hand the plugin casts (left and right hand, Hold with auto-attack) at the
start of a jump, and hold the hotkey.

**Acceptance Scenarios**:

1. **Given** the plugin is casting a hand and the cast cannot start, **When** the player keeps the hotkey held,
   **Then** after about 1 s of failed starts the session ends and the previous gear is restored, and the hand stops
   twitching.
2. **Given** the cast can start again before that bound ends (for example on landing), **When** the next retry
   happens, **Then** the spell is cast normally.

---

### User Story 5 - Block and manual casting stay vanilla when no slot is involved (Priority: P2)

Outside the cases above, the block and attack buttons behave as in the game without the plugin.

**Why this priority**: the fix must not change play when no slot is involved, or when the player casts by hand.

**Independent Test**: block, bash and cast manually with no session, and during a Press session where the plugin is
not casting the hand.

**Acceptance Scenarios**:

1. **Given** no session is active, **When** the player blocks with a shield or weapon, bashes, or casts a left-hand
   spell with the button, **Then** each works as in vanilla.
2. **Given** a Press session with auto-attack off and a left-hand spell, **When** the player presses the block button,
   **Then** the left-hand spell is cast manually, as today.

---

### Edge Cases

- **Weapon block**: a one-handed weapon (not a shield) in the left hand, or a two-handed weapon, while block is held
  before the slot is activated (Require exclusive hotkey OFF).
- **Dual-wielded weapons**: with a weapon in the left hand, the left button attacks instead of blocking. During a Hold
  or Automatic right-hand slot, that attack press also cancels the slot and reaches the game (Clarifications).
- **Bash**: block and attack pressed together around an activation.
- **The block button inside a slot combo**: a player binds `LT` or the right mouse button into a slot combo. The combo
  rules of spec 004 decide that press first, and a combo press never counts as a block press.
- **Session ended by something else** (stagger, knockdown, menu, game load, death, safety timeout) while a taken
  block button is held: User Story 3 scenario 2 applies.
- **Hotkey still held after a cancel** (Hold mode): the slot does not activate again until the hotkey is released and
  pressed again.
- **Cancel and retry bound together**: a block press during the retries cancels the session at once, without waiting
  for the 1 s bound.
- **Right hand**: the player's attack button (left mouse button, `RT`) pressed while the plugin is casting the right
  hand follows User Story 1's rules for the right hand (FR-007).
- **Block-with-spell mods**: a mod key that blocks while a spell is in the left hand interrupts the cast. User Story 2
  scenario 3 then ends the slot.
- **Dual cast**: any handling of the block or attack button keeps both hands in step (FR-008).
- **Automatic left hand with nothing cast** (second report): verified by the tests. If it still happens after the fix,
  it has a different cause and gets its own spec.
- **Powers and shouts**: not affected. They do not use the attack/block actions.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: While the plugin is casting a hand itself, the player's own attack/block button for that hand MUST NOT
  interrupt, restart or desynchronize that cast. A press of that button (Clarification 2026-11-20):
  - in Hold and Automatic, cancels the session and restores the previous gear,
  - in Press mode, has no effect.

  In both cases the press does not reach the game as a cast or a block. To block, the player presses again (FR-003).
- **FR-002**: When the plugin starts casting a hand while the player is holding an attack/block button the game sees
  as held (either hand, for example blocking with a shield), the cast MUST start normally. The held button MUST NOT
  keep the game blocking, or keep the action held, and it does nothing more until it is released.
- **FR-003**: When a session ends, by any exit, the game's view of the buttons MUST match the physical buttons.
  Nothing stays held in the game after the player released it. A button the plugin had taken that is still held when
  the session ends produces no action until the player presses it again.
- **FR-004**: No effect of a block interaction MUST survive into a later session. Every activation behaves as if block
  had never been used before it.
- **FR-005**: When a hand's cast cannot start, the plugin MUST NOT restart it endlessly. After about 1 s of failed
  starts, the session MUST end and the previous gear MUST be restored (Clarification 2026-11-20). Retries before that
  keep working. The bound is a build-time constant, with no setting.
- **FR-006**: When no slot is involved, or the plugin is not casting any hand of the session (auto-attack off, or
  Press after its first auto-cast), the attack/block buttons MUST behave as in vanilla.
- **FR-007**: The same rules MUST apply to both hands, and to keyboard/mouse (left and right mouse buttons) and
  gamepad (`RT` and `LT`).
- **FR-008**: In a dual-cast session, every handling of the attack/block buttons MUST keep both hands in step, so the
  spell comes out as a single dual cast (Constitution Principle IV).
- **FR-009**: The debug log MUST show:
  - each press and release of a player's attack/block button that the plugin keeps or acts on during a session, and
    what it did,
  - the game's view of a button being brought back in step at the start of a cast,
  - the plugin stopping its retries for a cast that cannot start,
  - a session ending because the game interrupted its cast (Constitution Principle VI).
- **FR-010**: `docs/TEST_MATRIX.md` MUST gain cases for:
  - block held before activating a left-hand slot (shield and weapon) and a right-hand slot, with Require exclusive
    hotkey OFF, plus the refusal with it ON,
  - block pressed, held and clicked during left-hand Hold, Automatic and Press-with-auto-attack casts,
  - block pressed during right-hand Hold, Automatic and Press slots with a shield,
  - the same on gamepad with `LT`,
  - dual cast with block,
  - a session ending while block is held,
  - no stuck state after these sequences,
  - an interrupted cast never leaving the slot active,
  - a cast that cannot start (activation mid-jump),
  - vanilla block, bash and manual casting unaffected.
- **FR-011**: During a Hold or Automatic session where the plugin is casting a hand, a fresh press of the **other**
  hand's attack/block button MUST cancel the session and restore the previous gear. The press MUST reach the game, so
  the block or attack happens at once (Clarification 2026-11-20). In Press mode the press itself MUST NOT cancel the
  session. If it interrupts a cast still in progress, FR-012 still applies.
- **FR-012**: When the game interrupts a cast the plugin had started, the session MUST end and the previous gear MUST
  be restored. A slot MUST NOT stay active with nothing happening, until the safety timeout or the hotkey release.
- **FR-013**: Require exclusive hotkey MUST keep its meaning: with the patch ON, a held attack/block button counts as
  an extra key, and the slot is refused (Clarification 2026-11-20).

### Key Entities

- **Plugin-cast hand**: a hand whose cast input the plugin drives during a session (Automatic, or Hold/Press with
  auto-attack).
- **Other hand**: the hand of the session the plugin is not casting, for example a shield or a weapon.
- **Attack/block button**: the player's button for a hand's game action: right mouse button / `LT` for the left hand,
  left mouse button / `RT` for the right hand.
- **Game's view of the button**: whether the game currently treats that action as held. It must match the physical
  button whenever the plugin is not taking that button.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The two sequences from the report with a left-hand slot (block held, then activate; hotkey held, then
  block pressed and held), 10 times each on keyboard/mouse and 10 times each on gamepad: the hand never twitches, and
  every try ends as User Story 1 defines.
- **SC-002**: Block pressed during a right-hand Hold or Automatic slot with a shield, 10 out of 10 on each input
  device: the slot ends at once, the gear is restored, and the character blocks.
- **SC-003**: After each SC-001 and SC-002 try, the next 10 activations cast on the first try without touching block:
  0 stuck states.
- **SC-004**: Dual-cast slots with the SC-001 sequences, 10 out of 10: a single dual cast every time, or a clean
  cancel.
- **SC-005**: Over all tests, 0 slots stay active after their cast was interrupted: each one ends with the gear
  restored, within about 1 s of the interruption.
- **SC-006**: An activation at the start of a jump, 10 out of 10: either the spell is cast once the character lands,
  or the hand stops twitching within about 1 s and the session ends with the previous gear restored.
- **SC-007**: Vanilla block, bash and manual left-hand casting with no session, and manual casting in a Press session
  without auto-attack, 10 out of 10 each: unchanged.
- **SC-008**: For every attack/block press the plugin keeps or acts on during testing, the log says what it did
  (100%).
- **SC-009**: Regression: TEST_MATRIX 1.1–1.6 (dual cast), 2.x (mode × slot content), 4.1–4.3 (auto-attack), 6.1–6.2
  (Require exclusive hotkey), 8.1 (block interruption) and the spec 004 cases 6.15–6.23 pass.

## Assumptions

- The reports come from keyboard/mouse players. Gamepad `LT` produces the same game action, so it is covered too.
- **Scope**: the block-related stuck state (BUG-03), the block that does not end a slot, and the slot left active after
  an interrupted cast (the user's tests). The second report in the task doc (Automatic left hand, nothing cast) is
  verified by the tests (Edge Cases), but only fixed here if it has the same cause.
- The right hand follows the same rules (FR-007), because the mechanism is the same.
- **The plugin-cast hand's button, after a session**: a held button the plugin took does nothing until it is pressed
  again (FR-003). The other hand's button is not taken during a session, so its block happens at once (FR-011).
- **No setting or INI key changes**. This is a bug fix that restores what auto-cast and block are meant to do, so it
  adds no patch flag. The plan confirms this against Constitution Principle V.
- **In-game validation**: on the release DLL, approved by the user (Clarifications), because the DEBUG DLL still fails
  to load (specs 002–004).
- **Root cause**: the twitching mechanism (Background item 7) is a hypothesis from the code review. The design does not
  depend on it, and the baseline log (quickstart §0) confirms it.
