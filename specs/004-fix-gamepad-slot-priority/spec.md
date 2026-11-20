# Feature Specification: Fix gamepad slots 5+ losing input priority to the game's default actions

**Feature Branch**: `fix/gamepad-slot-priority` (created from `main` at `0de67cf`; carries the staged distribution
package changes the user moved from `fix/mouse-wheel-hotkeys`)

**Created**: 2026-11-19

**Status**: Implemented and validated in game (user, 2026-11-20, release DLL). Every test passed on the 50 ms build
except one slow chord (Triangle → R2 100 ms apart), the documented limit. The combo tolerance was then set to 100 ms
(Clarifications). See tasks.md implementation log

**Input**: User description: "Resolver o Conflito de prioridade dos comandos em gamepads nos slots acima do quarto.
Use @.claude/tasks/integrated_magic_bugs_e_melhorias.md e @.claude/tasks/bug-03-gamepad-slots-5-plus.md como fontes
para a spec. Crie um nova branch para essa spec e mova todas as mudanças que estão em stage nessa branch para a nova
branch."

## Background & Investigation Findings *(context)*

Sources: BUG-02 in `.claude/tasks/integrated_magic_bugs_e_melhorias.md`, `.claude/tasks/bug-03-gamepad-slots-5-plus.md`
(one player report) and a review of the input code on `main`.

1. **Symptom (report)**: on a gamepad, slots 1–4 work, but from slot 5 on the game performs the button's default
   action instead of casting. Example from the report: slot 1 `RT + B` casts; slot 5 `RT + B` makes the character
   jump and no spell is cast. Keyboard was not tested by the reporter.
2. **The "fixed limit of 4" hypothesis is wrong.** The task note suspected a list or loop capped at 4 slots. The
   whole input path is sized for 64 slots, and the slot count follows the configured value.
3. **Mechanism (confirmed in code)**:
   - **Holding back buttons**: while a slot's combo is being pressed, the plugin briefly holds back (about 40 ms)
     the buttons that belong to slot combos, so the game does not act on them. If no combo completes, it hands them
     to the game afterwards, so normal gameplay is not blocked.
   - **First owner wins**: a button that appears in several slots' combos is always attributed to the
     **lowest-numbered slot** that uses it, even when the player is pressing a different slot's combo.
   - **Leak**: if that lower slot's combo is not complete, the button is held back for the **wrong** slot. When that
     slot's short window ends, the button is handed to the game, even though another slot was activated with it.
     The game then performs the button's default action: jump, attack, menu and so on.
4. **Why it shows from slot 5 on**:
   - **Default layout**: slots 1–4 use `RT` + one of the four face buttons. `RT` is the "modifier", and every slot
     shares it.
   - **Slot 5+**: any combo inevitably shares buttons with slots 1–4, so those slots claim its buttons and leak them
     to the game.
   - **Slots 2–4**: they share `RT` with slot 1, so `RT` probably leaks too. Because it is the "cast with the right
     hand" button, the leak is mostly invisible there.
5. **Same rule on keyboard**: keyboard and mouse combos use the same ownership rule. Combos that share keys, for
   example `Shift + 1` on slot 1 and `Shift + 5` on slot 5, should show the same problem. The default keyboard layout
   (single keys `1`–`4`) shares nothing, which is why keyboard users have not reported it.
6. **Simultaneous slots**: when two slots become pressed in the same frame, they are handled from the lowest number
   up.
7. **Confirmed in game by the user (2026-11-20)**, on the default slot 2 `R2 + Triangle` (`RT + Y`):
   - **R2 slightly before Triangle**: the slot casts. R2's own attack does reach the game, but the spell equip
     overrides it, so it goes unnoticed.
   - **Triangle slightly before R2**: the character **jumps** (Triangle's action), and the spell cannot be used.
   - **Conclusion**: the leak depends on **press order and timing**, not on the slot number.
     - The first button of a still-incomplete combo is held back and starts a 40 ms window.
     - The slot only accepts the combo about 40 ms after the **full** combo is held.
     - So the window ends first, and that first button is handed to the game.
     - Only a press of both buttons in the same frame avoids it.
   - **Why the report says "slots 5+"**: slots 5+ normally use 3-button combos (`face + RT + LB`). They leave more
     time between the first and last button, so they hit the leak far more often. This is the most likely primary
     cause of the report.

## Clarifications

### Session 2026-11-19

- Q: Which slot fires when two slots have the same combo, or one slot's combo is fully contained in another's? → A:
  The longest combo completed at that moment wins, with no added wait. For identical combos the lowest-numbered slot
  fires, and the log warns about the conflict.

### Session 2026-11-20

- Q: How long may the rest of a combo take after its first button (chord tolerance)? → A: Resolution of analysis
  findings H1/H2, delegated by the user. **Superseded** by the combo tolerance below (now 100 ms).
  - The **second** button must arrive within the existing detection window (about 40 ms) of the first.
  - Once **two or more** buttons of a combo are held (a chord in progress), the rest of that combo may take up to
    **0.15 s** from the first button.
  - A button pressed on its own is still released to the game after 40 ms. Single presses get no added delay.
- Q: Is in-game validation on the release build approved for this spec (Constitution Principle VI)? → A: Yes,
  approved by the user. It is recorded in plan.md Complexity Tracking.
- Q: May an active slot session block new presses of its combo's buttons, for example `RT` to cast manually in
  Press mode? → A: No (resolution of analysis finding H3). A button's release and held repeats follow what happened
  to its own press (consumed or passed to the game). Only the presses that build a combo are consumed.
- Q: After the first in-game run, what combo tolerance should combos of 2 or more buttons use? → A: **25 ms** (user).
  **Superseded** by the next answer (50 ms). It replaced the 40 ms / 0.15 s values above (`kComboToleranceSec`):
  - the **second** button within 25 ms of the first; a button seen in the frame the 25 ms ends still counts,
  - for 3-button combos, the whole combo within 50 ms of the first button (two gaps),
  - a multi-button slot activates 25 ms after its full combo is held. A longer combo completed within that time still
    wins (FR-009),
  - a button pressed on its own reaches the game after 25 ms, instead of 40 ms,
  - single-key slots keep their 40 ms wait with Require exclusive hotkey ON (`kSingleKeyAcceptDelaySec`).
- Q: After the second in-game run, where a Triangle → R2 gap of 100 ms still made the character jump, which tolerance
  should combos of 2 or more buttons use? → A: **50 ms** (user). It reduces that case, and the delay on commands is
  still not noticeable. Same rules as the 25 ms answer, with these values:
  - the **second** button within 50 ms of the first,
  - for 3-button combos, the whole combo within 100 ms of the first button,
  - a multi-button slot activates 50 ms after its full combo is held,
  - a button pressed on its own reaches the game after 50 ms,
  - single-key slots keep 40 ms (`kSingleKeyAcceptDelaySec`).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Gamepad combos cast regardless of slot number or press order (Priority: P1)

A gamepad player presses the combo of any slot, from slot 1 to slot 8, with the buttons in any order: face button
first or modifier first, as in a normal quick chord. The slot activates and casts its spell. The game does not
perform the default action of any button in that combo: no jump, no attack, no menu.

**Why this priority**: this is the reported bug, and its cause is confirmed by the user in game (Background item 7).
Pressing the face button slightly before the modifier makes the character jump instead of casting. Slots 5+ hit it
most, because of their 3-button combos.

**Independent Test**: on a gamepad with 8 slots (slots 5–8 sharing buttons with slots 1–4), press each slot's combo
both face-button-first and modifier-first, as a quick chord.

**Acceptance Scenarios**:

1. **Given** 8 slots, slots 1–4 on the default gamepad layout and slot 5 bound to a combo that shares buttons with
   slots 1–4, **When** the player presses slot 5's combo, **Then** slot 5 activates and casts. The character does not
   jump, attack or open a menu because of the combo's buttons.
2. **Given** the same setup, **When** the player presses slots 5, 6, 7 and 8 in turn, **Then** each one activates
   and casts its own spell.
3. **Given** the same setup, **When** the player presses slot 1's combo, **Then** slot 1 still activates as before.
4. **Given** the default slot 2 `RT + Y`, **When** the player presses `Y` slightly before `RT` (a quick chord),
   **Then** slot 2 casts and the character does **not** jump.
5. **Given** the same slot, **When** the player presses `RT` slightly before `Y`, **Then** slot 2 casts and the game
   does **not** receive `RT` as its own right-hand attack.
6. **Given** slot 6 `Y + RT + LB`, **When** the player presses the three buttons as a quick chord starting with `Y`,
   **Then** slot 6 casts and the character does not jump.

---

### User Story 2 - Buttons used on their own keep their normal game action (Priority: P2)

A button that belongs to slot combos, such as the `RT` modifier or a face button, still does what the game normally
does when the player presses it on its own or abandons a combo halfway.

**Why this priority**: the fix must not trade the bug for a gameplay regression. The fix only holds a button longer
while a combo containing it is actually in progress. Everything else must behave as today.

**Independent Test**: press the modifier and each face button alone, then start a combo and release it without
completing it. The game acts on them as usual.

**Acceptance Scenarios**:

1. **Given** no slot combo is completed, **When** the player presses `RT` alone, or a face button alone, **Then** the
   game performs its normal action (attack/cast, jump, activate, menu), with no extra delay beyond today's short
   detection window.
2. **Given** the player starts pressing a combo but releases it without completing it, **When** the window ends,
   **Then** the buttons pressed reach the game as they do today.

---

### User Story 3 - Keyboard and mouse combos follow the same rule (Priority: P3)

A keyboard player who binds combos that share a key, for example `Shift + 1` on slot 1 and `Shift + 5` on slot 5, gets
the same behavior: the pressed combo's slot activates, and the shared key does not trigger the game's action.

**Why this priority**: same root cause, different device. It is not reported yet, because the default keyboard layout
does not share keys.

**Independent Test**: bind `Shift + 1` and `Shift + 5` and press each one. Each slot activates, and `Shift` does not
cause a game action such as sprint, walk or sneak, depending on the player's bindings.

**Acceptance Scenarios**:

1. **Given** slot 1 `Shift + 1` and slot 5 `Shift + 5`, **When** the player presses `Shift + 5`, **Then** slot 5
   activates and `Shift` is not handed to the game because of slot 1.
2. **Given** the same setup, **When** the player presses `Shift + 1`, **Then** slot 1 activates as before.

---

### Edge Cases

- **One combo contained in another** (Clarification 2026-11-19), for example slot 2 `RT + Y` and slot 6
  `RT + Y + LB`:
  - When several slots' combos are complete at the same moment, the slot with the **longest** combo fires. If `RT`
    and `LB` are held when `Y` goes down, slot 6 fires and slot 2 does not.
  - There is **no added wait**. Slot 2 activates once the combo tolerance (50 ms) after `RT + Y` ends; `LB` arriving
    within it still makes slot 6 win. If `LB` comes later, slot 2 has already fired.
    Slot 6 may still fire afterwards, when `LB` completes its combo, as a normal second press. With Require exclusive
    hotkey ON, `LB` already counts as an extra key for slot 2, so slot 6 fires.
  - **The longer combo is cancelled**: if the longer combo was still being accepted and is then abandoned, for example
    the player releases `LB` before slot 6 is accepted, neither slot fires from that press. The shorter one stays
    suppressed until its buttons are released, and the player presses again. This is accepted as an edge case.
- **Identical combos on two slots** (for example slots 1 and 5 both `RT + B`): this is a configuration conflict.
  - The **lowest-numbered** slot fires.
  - The log warns about the conflict when the hotkeys load, naming both slots.
- **Combo pressed out of order**: for example `B` first, then `RT`. It is now a primary scenario (US1 scenarios 4–6).
  For a quick chord, the outcome must not depend on which button was pressed first. The existing Press both at the
  same time patch keeps its own timing rule.
- **Slow chord** (Clarification 2026-11-20, combo tolerance 50 ms): either of these.
  - The **second** button arrives later than the detection window (50 ms) after the first.
  - A 3-button combo takes longer than **100 ms** from its first button.

  The first button is then treated as a normal press and reaches the game, as today, and the slot still activates
  when the combo completes. Waiting longer for the second button would delay every single press of those buttons
  (FR-003).
- **Chord in progress held on purpose**: while two buttons of a longer combo are held, for example `RT + LB` of the
  "second layer", pressing a third combo button on its own reaches the game up to 100 ms later than today. Example:
  `Y` to jump while holding `RT + LB`. This is the cost of tolerating slow 3-button chords.
- **Casting manually during a session**: a slot is active in Press mode without auto-attack, and the player presses
  `RT` alone to cast the equipped spell. `RT` reaches the game as a normal press, after the short detection window,
  even though `RT` belongs to the active slot's combo (FR-010).
- **Require exclusive hotkey ON**: its rule ("no extra keys held") keeps working. Shared buttons must not count as
  extra keys for the slot whose combo they belong to.
- **Hold-mode slot 5+**: holding the combo must keep the buttons away from the game for the whole hold, not only at
  the press (FR-001, FR-010). Tested in quickstart §2.
- **Slot count reduced** (for example from 8 to 4): the remaining slots behave as today.
- **Menus open**: no change. Input stays blocked by menus as today.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When the player completes the combo of any slot (1 to the configured count) as a chord, none of that
  combo's buttons MUST trigger the game's default action. This holds for both press and hold, and does not depend on
  the slot's number. A chord means (Clarification 2026-11-20):
  - the second button arrives within the detection window (50 ms) after the first,
  - a 3-button combo completes within 100 ms of the first button.

  Slower presses follow the "Slow chord" edge case.
- **FR-002**: A button that belongs to several slots' combos MUST be attributed to the slot whose combo the player
  is actually pressing. A slot whose combo is not being completed MUST NOT hold back a shared button and later hand
  it to the game while another slot is using that button.
- **FR-003**: A button pressed without completing any slot combo MUST still reach the game, as today. Pressing a
  single button for its normal game action must keep working, with no added delay beyond today's short detection
  window.
- **FR-004**: The slot's number MUST NOT decide whether its combo works. The only exception is identical combos on
  two slots, where the lowest-numbered slot fires (FR-009).
- **FR-005**: The same rules MUST apply to keyboard and mouse combos (US3).
- **FR-006**: Require exclusive hotkey and Press both at the same time MUST keep their current meaning, and no setting
  or INI key changes (Constitution Principle V).
- **FR-007**: The debug log MUST show, for a held-back button, which slot it was attributed to. It MUST also show
  whether the button was finally consumed by an activation or handed to the game, so similar reports can be
  diagnosed from a log (Constitution Principle VI).
- **FR-008**: `docs/TEST_MATRIX.md` MUST gain cases for this bug:
  - gamepad slots 5–8 with shared buttons,
  - press order on slots 1–8, including the shared modifier on slots 2–4,
  - single-button gameplay,
  - keyboard shared-key combos,
  - the overlapping-combo rule (FR-009),
  - Hold mode during the hold,
  - casting manually with a combo button during an active Press session (FR-010).
- **FR-009**: When the combos of several slots are complete at the same moment, the slot with the longest combo
  (most keys or buttons) MUST fire, and no other slot activates from that press. The decision MUST NOT wait for more
  buttons. For identical combos, the lowest-numbered slot fires, and the hotkey load MUST log a warning naming the
  conflicting slots (Clarification 2026-11-19).
- **FR-010**: A button's release and held repeats MUST follow what happened to its own press (Clarification
  2026-11-20):
  - if the press was consumed by a slot activation, they are consumed too,
  - if the press reached the game, they reach the game too.

  An active slot session MUST NOT block new presses of its combo's buttons that do not complete a combo. Example:
  `RT` to cast manually during a Press-mode session without auto-attack.

### Key Entities

- **Slot combo**: the up-to-three keyboard/mouse keys or gamepad buttons bound to a slot. Several slots may share
  buttons. On the default gamepad layout every slot shares the modifier.
- **Held-back button**: a button press the plugin briefly keeps from the game while it decides whether a slot combo
  is being pressed. It ends either consumed by an activation or handed to the game.
- **Detection window**: the short delay a held-back press waits before going to the game when nothing else happens:
  the combo tolerance, 50 ms (40 ms before the 2026-11-20 tuning). The plan and tasks call it the **filter window**;
  it is the same thing.
- **Press fate**: what happened to a button's current press: held back, consumed or passed to the game. Its release
  and held repeats follow it (FR-010).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On a gamepad with 8 slots (slots 5–8 sharing buttons with slots 1–4), 20 out of 20 activations of slots
  5–8 cast their spell. In none of them does the game perform the default action of a combo button: jump, attack,
  menu or activate.
- **SC-002**: For both press orders (face button first, modifier first), slots 1–8 activate 20 out of 20 times each.
  The game never performs the face button's action (jump, activate, menu), and never receives the shared modifier as
  its own action during those activations.
  - **Only chords count** (FR-001). The log decides which presses are chords: a press whose buttons end as
    `consumed`, with no `handed to game` line for them, was a chord.
  - A `handed to game` line with no `window extended` before it marks a slow chord. It is excluded from the count and
    reported separately.
- **SC-003**: Single buttons pressed for gameplay (the modifier alone, each face button alone) perform their normal
  game action 10 out of 10 times, with no perceivable extra delay compared with the current version.
- **SC-004**: Keyboard combos that share a key (`Shift + 1` and `Shift + 5`) each activate their own slot 10 out of 10
  times, and the shared key causes no game action.
- **SC-005**: TEST_MATRIX section 6 (6.1–6.14) and the dual-cast cases 1.1, 1.3 and 1.5 pass with no regressions.
- **SC-006**: For every held-back button in testing, the log names the slot it was attributed to and whether it was
  consumed or handed to the game (100%).
- **SC-007**: With slot 2 `RT + Y` and slot 6 `RT + Y + LB`, pressing `Y` last while `RT` and `LB` are held
  activates slot 6, 10 out of 10 times, and slot 2 never fires from that press. Two slots with an identical combo
  produce a conflict warning in the log at every hotkey load.
- **SC-008**: Two FR-010 checks, 10 out of 10 tries each:
  - **Press mode, no auto-attack**: during an active session, `RT` pressed alone casts the equipped spell manually.
  - **Hold mode**: during a held slot 6 the game performs no action of `Y`, `RT` or `LB`, and releasing the combo
    leaves no button stuck in the game.

### Verification (TEST_MATRIX)

- **New**:
  - gamepad slots 5–8 sharing buttons with slots 1–4,
  - press order (face button first, modifier first) on slots 1–8, including the modifier during slots 2–4,
  - single-button gameplay (modifier and face buttons alone),
  - keyboard `Shift + 1` / `Shift + 5`,
  - the overlapping-combo rule (longest combo wins, identical combos warn; SC-007),
  - Hold mode during the hold, and casting manually with `RT` during a Press session (SC-008).
- **Regression**: 6.1–6.14, 1.1, 1.3, 1.5.

## Assumptions

- The reporter used the default gamepad layout for slots 1–4: `RT` as the modifier, plus one face button per slot.
  Their exact slot 5 binding is unknown.
  - The order-dependent leak was **reproduced in game by the user** on slot 2 (2026-11-20, Background item 7).
  - The other mechanisms are derived from the code review.
- The fix keeps the existing short detection window, which lets a single button reach the game. It changes only:
  - which slot a shared button is attributed to,
  - what happens to it when another slot activates,
  - how long it is held while the combo containing it is still being accepted, or while a chord is in progress,
  - how its release and held repeats are treated (FR-010).
- **Chord tolerance** (Clarification 2026-11-20, set to 100 ms after the in-game runs): the second button
  within the detection window (50 ms) of the first, and a 3-button combo within 100 ms, as in a normal quick chord. A
  slower chord behaves as today (Edge Cases, "Slow chord").
  - Both values are build-time constants (`kComboToleranceSec`, `kMaxChordHoldSec`), so they can be tuned without
    changing any setting or INI key.
  - A longer wait for the **second** button was rejected, because it would delay every single press of a combo
    button.
- No setting, default binding or INI key changes. The modifier convention stays as it is.
- **In-game validation and Constitution Principle VI**: the DEBUG DLL still fails to load (see specs 002 and 003).
  In-game validation on the **release** DLL was approved by the user for this spec on 2026-11-20 (Clarifications),
  and the deviation is recorded in plan.md Complexity Tracking.
- The staged distribution package changes carried onto this branch (`SKSE/Plugins/` files, the version bump in
  `CMakeLists.txt`, the `MENU.cpp` string changes and the removal of `.claude/tasks/bug-02-...md`) are unrelated to
  this fix. They stay staged for the user to commit separately.
