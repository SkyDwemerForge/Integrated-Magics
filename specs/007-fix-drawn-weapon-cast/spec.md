# Feature Specification: Fix slot casts failing with a weapon or shield in the left hand after drawing weapons

**Feature Branch**: `fix/drawn-weapon-cast` (created from `main` at `f540210`, after spec 006 merged as #75)

**Created**: 2026-11-20

**Status**: Implemented. The fix (D6) was validated in game on 2026-11-21; see the implementation log in `tasks.md`

**Input**: User description: "resolver o bug relatado que acontece ao ativar o slot após sacar a arma. Informações sobre
ele podem ser encontradas em `.claude/tasks/bug-05-arma-esquerda-sacada.md` e
`.claude/tasks/integrated_magic_bugs_e_melhorias.md` no bug 07", plus "Crie uma nova branch a partir da main para essa
nova spec."

## Background & Investigation Findings *(context)*

Sources: the two task files above and a review of the slot activation, equip and restore code on `main` (`f540210`).

1. **Report (one player, version 1.5.1)**. Bug 05 and BUG-07 are the same report: same version, same text.
   - **Failing setups**: two one-handed weapons, or a shield in the left hand and a sword in the right.
   - **Weapons sheathed**: slots cast normally (left hand, right hand or dual).
   - **Weapons drawn**: most activations fail.
   - **Sequence**: sheathe → cast (works) → draw → cast. That first cast after drawing works, and every later one fails,
     until the player sheathes again.
   - **Setups that always work**: spells in both hands, or a spell in the left hand and a sword in the right.
   - **Common factor**: weapons drawn, and a weapon or shield (not a spell) in the left hand.
   - **Missing detail**: the report does not say what "fails" looks like (nothing happens, the character blocks or
     swings, or the spell is equipped but never cast). Logs must show it.
2. **Version**: the reporter's 1.5.1 cannot be matched to a commit. `CMakeLists.txt` went from 1.4.10 straight to
   1.5.2. `main` is 1.6.0.1. It contains:
   - commit `7819c49` (the exit of a left-hand cast started with the weapons sheathed and a weapon in the right hand),
     which the bug file asks to check. It concerns the sheathed path, so it is unlikely to cover this report, but it
     was never tested against it;
   - specs 002–006, among them spec 005's attack/block button rules and the 1 s bound on a cast that does not start.
3. **How a drawn-weapon activation differs from a sheathed one (code review)**:
   - **Sheathed**: before equipping, the plugin turns on the instant-equip animation variable (`InstantEquipAnim`, the
     one the Skip Equip Animation patches use). If the slot has a right-hand spell, it also raises the hands with
     magic. The spells are equipped while the weapons are away, so the
     game draws straight into magic. At the end, the plugin sheathes and gives the gear back after the sheathe ends.
   - **Drawn**: neither happens. The plugin swaps the left-hand weapon or shield for the spell while the weapons are
     out (instantly only when Skip Equip Animation is ON). In the same frame, it presses the attack button of that
     hand. At the end, the gear comes back at once, still drawn, with no sheathe and no new draw.
   - The plugin casts a hand by pressing that hand's attack/block action (spec 005). While the game still treats the
     left hand as a shield or weapon, that press means block (a shield) or a left swing (two weapons), not a cast. A
     block ends the session at once.
4. **The "first cast after drawing" pattern**:
   - Before the first drawn cast, the drawn state comes from the game's own draw.
   - Before every later one, it comes from the plugin's restore while drawn: the weapon or shield is put back in the
     hand with no sheathe and no new draw.
   - A sheathe and a new draw make the next cast work again.
   - This points to the restore-while-drawn leaving the hand's animation state out of step with its gear. It is a
     hypothesis, not a finding.
5. **Hypotheses for the plan's research**. Each must be confirmed or ruled out with logs of a failing and a working
   activation.
   - **H1**: the cast press reaches the game before it has finished switching the left hand to magic. The game blocks
     or swings instead. The session then ends (block), or the start retries and gives up after 1 s (spec 005 bound).
   - **H2**: the plugin's restore while drawn leaves the hand's animation state stale, so the next equip or cast is not
     picked up until a sheathe and draw.
   - **H3**: the plugin's "wrong spell in the hand" check ends the session when the hand's caster still reports the
     previous spell.
   - The plan's research adds two more:
     - **H4**: a late equip event from the plugin's own restore is read as a foreign equip;
     - **H5**: the drawn swap passes through a "sheathing" weapon state.
     See `research.md` R6 and R7.
6. **What spec 005 already validated (TEST_MATRIX 8.9, 8.16, 2026-11-20)**:
   - Weapons drawn, a shield and then a one-handed weapon in the left hand.
   - 10 activations in a row of a left-hand slot cast at the first try.
   - So on the current build at least that setup works. Not covered then:
     - a weapon in the right hand as well (both of the report's setups have one),
     - two one-handed weapons,
     - right-hand and dual slots,
     - the full sheathe → cast → draw → cast → cast sequence.
7. **Reproduction on the current build (2026-11-21, `research.md` R11)**:
   - **Working**: with the weapons drawn, every activation cast with a sword and a shield (patches ON), and with a
     sword and an axe (patches ON and OFF), once the draw animation had ended. This held for left-hand, right-hand and
     dual slots, and through the sheathe → cast → draw → cast sequence.
   - **Failing**: Skip Equip Animation OFF, a weapon in the left hand, a left-hand slot activated **during the draw
     animation**. Equipping the spell restarts the game's draw animation (about 1.4 s), and the left-hand cast cannot
     begin before it ends. The 1 s bound of spec 005 ends the session first, and putting the weapon back plays the
     draw animation once more. The player sees the draw restart and no spell (4 of 4 such activations).
   - **Link to the report**: a failed attempt's restore plays a full draw animation too, so a player who casts again
     soon lands in the same window. That would make "every later cast fails" self-sustaining, but the 1.5.1 build
     predates the 1 s bound, so this is a likely link, not a proven one.

## Clarifications

### Session 2026-11-20

- Q: The DEBUG DLL still faults during start-up (a DEBUG-only defect in the hook installation). May this spec be
  reproduced and validated in game on the release build (Constitution Principle VI)? → A: Yes, approved by the user.

### Session 2026-11-21

- The user reported the failing case of Background item 7: without the Skip Equip patches, a left-hand slot activated
  during the draw animation restarts the animation instead of casting. It is in scope: the slot must cast (Edge Cases,
  FR-006).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Slots cast every time with weapons drawn (Priority: P1)

A player fights with two swords, or with a sword and a shield, weapons drawn. They press a slot's hotkey again and
again, during and between fights. Every activation casts the slot's spell. Then the previous gear comes back, still
drawn, ready to attack or block. The player never has to sheathe to make a slot work.

**Why this priority**: this is the reported bug. A slot that only works with the weapons sheathed is unusable in
combat, which is where the plugin matters most.

**Independent Test**: with each reported setup, weapons drawn, activate a left-hand, a right-hand and a dual slot 10
times in a row each. Then run the reporter's sequence: sheathe → cast → draw → cast → cast.

**Acceptance Scenarios**:

1. **Given** two one-handed weapons drawn, **When** the player activates a left-hand slot 10 times in a row (Automatic),
   **Then** each activation casts the spell, and both weapons are back in their hands, still drawn, after each cast.
2. **Given** a sword and a shield drawn, **When** the player activates a left-hand, a right-hand and a dual slot 10
   times each, **Then** every activation casts, and the sword and shield come back in their hands, still drawn.
3. **Given** either setup, **When** the player sheathes, casts, draws, and then casts 5 more times, **Then** all 6
   casts work: not only the first after drawing.
4. **Given** the gear has just come back after a drawn cast, **When** the player attacks or blocks, **Then** it works
   on the first press, with no sheathe or draw needed.
5. **Given** either setup and a slot in Hold or in Press, **When** the player uses it 5 times with weapons drawn,
   **Then** each use casts as that mode does with spells in both hands.

---

### User Story 2 - The paths that work today keep working (Priority: P2)

Casting with the weapons sheathed, and the setups the reporter says always work, behave as they do today.

**Why this priority**: the fix changes what happens around the activation and the restore. It must not break the
sheathed path or the hand-button rules of spec 005.

**Independent Test**: the regression cases of TEST_MATRIX (1.5, 7.1, 7.2, 7.10, 8.9–8.19) and sheathed casts with the
reported setups.

**Acceptance Scenarios**:

1. **Given** the reported setups with weapons sheathed, **When** the player activates left-hand, right-hand and dual
   slots, **Then** the hands come up with magic, the spell is cast, the weapons are sheathed again and the gear comes
   back after the sheathe, as today.
2. **Given** spells in both hands, or a spell in the left hand and a sword in the right, weapons drawn, **When** the
   player activates slots, **Then** they cast as today.
3. **Given** a shield or weapon in the left hand, **When** the player holds or presses block around a slot (spec 005
   cases 8.9–8.19), **Then** the result is the same as before this change.

---

### Edge Cases

- **Slot activated during the draw animation**, or right after the previous slot's gear came back: the cast happens
  once the game's draw animation ends, with Skip Equip Animation OFF as well. Never a stuck slot.
- **Switching slots while drawn** (a Press slot active, then another slot): the new slot casts and the gear restored
  at the end is the one from before the first slot.
- **In combat and out of combat**: same result in both. Combat already decides whether a sheathed cast sheathes again.
- **Skip Equip Animation** and **Skip Equip Animation on Return**, both OFF and both ON: same result (the reporter's
  settings are unknown).
- **Two-handed weapon drawn**, **a staff in the left hand** and **a torch in the left hand**: the slot casts and the
  item comes back (TEST_MATRIX 7.6, 7.7). A staff is a left-hand weapon, so it is treated like a sword there.
- **Dual cast with weapons drawn** (TEST_MATRIX 1.6): one dual cast, never two single casts.
- **Not enough magicka**: the slot is refused as today, and the gear is not touched.
- **Block held when the slot is activated** (spec 005 takeover): the block goes down and the spell is cast, as today.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: With the weapons drawn and a weapon or shield in the left hand (two one-handed weapons, or a one-handed
  weapon and a shield), a slot MUST cast its spell on every activation, not only the first after drawing. This applies
  to left-hand, right-hand and dual slots, in Hold, Automatic and Press.
- **FR-002**: After a drawn-weapon cast, the previous gear MUST come back in the right hands, still drawn. The player's
  own attack and block with that gear MUST work at once, with no sheathe or draw.
- **FR-003**: The plugin MUST NOT sheathe and redraw the weapons to cast from a drawn state. The cast MAY wait for the
  game's own hand-switch animation, which Skip Equip Animation skips.
- **FR-004**: The sheathed path MUST keep its current behavior: hands raised with magic, cast, sheathe, gear back
  after the sheathe (TEST_MATRIX 1.5, 7.10).
- **FR-005**: The setups that already work MUST keep working: spells in both hands, a spell in the left hand with a
  weapon in the right, and the spec 005 attack/block rules (TEST_MATRIX 8.9–8.19).
- **FR-006**: A drawn-weapon activation that cannot cast MUST end within the existing bound (1 s without a stable
  cast) with the gear back. Time the game spends playing its own equip or draw animation does not count toward that
  1 s, up to 2 s, so a cast that only has to wait for the animation is not cut. No slot may stay active until the
  30 s timeout (Constitution III).
- **FR-007**: The root cause MUST be established before the change. Use logs of a failing activation and a working
  one with the same setup on the current build (bug 05 tasks).
  - If no reported setup fails on the current build, the spec closes the bug as already fixed. It names the change
    that fixed it if that can be determined, and still delivers FR-008 and FR-009.
- **FR-008**: The log MUST show, at slot activation and at restore, the weapon state and what each hand holds
  (spell, weapon, shield, torch, empty). A future report can then be diagnosed from the log alone.
- **FR-009**: `docs/TEST_MATRIX.md` MUST gain cases for:
  - the reported setups with weapons drawn, for each slot hand, 10 activations in a row;
  - the sheathe → cast → draw → cast → cast sequence;
  - attacking and blocking right after the gear comes back.
  1.6, 7.1, 7.2 and 7.10 remain the regression cases.
- **FR-010**: No setting, INI key or saved data changes. This is a bug fix with no patch flag.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With each reported setup and weapons drawn, 10 consecutive Automatic activations each of a left-hand, a
  right-hand and a dual slot: 10 out of 10 cast the spell, with Skip Equip Animation OFF and with it ON.
- **SC-002**: The reporter's sequence (sheathe → cast → draw → cast ×5), with each setup: 6 out of 6 casts succeed.
- **SC-003**: After each cast in SC-001, the gear is back in the right hands, still drawn, within 1 second of the cast
  ending. The first attack or block press works (10 out of 10).
- **SC-004**: Hold and Press slots with each setup, weapons drawn: 5 out of 5 uses behave as with spells in both hands.
- **SC-005**: No session reaches the 30 s timeout during any of the tests above (0 occurrences in the log).
- **SC-006**: Regression: TEST_MATRIX 1.5, 7.1, 7.2, 7.10 and 8.9–8.19 pass, and sheathed casts with the reported
  setups work 5 out of 5.

## Assumptions

- **One report**: bug 05 and BUG-07 describe the same problem from the same player.
- **"Fails" means no spell is cast**. The exact symptom (block, swing, nothing, slot ending) comes from the logs.
- **Reproduce on the current build first** (FR-007). Spec 005 already showed a drawn left-hand slot with a shield
  working 10 times in a row (Background item 6). The report may therefore be partly or fully fixed on 1.6.0.1, or it
  may need the right-hand weapon the report has.
- **Expected behavior when drawn**: the slot swaps only the hands it needs, casts and puts the gear back, without
  sheathing. The reporter's workaround (sheathe first) is not an acceptable fix (FR-003).
- **Player only**. Animation or combat overhaul mods are out of scope, unless the logs show one is involved.
- **In-game validation on the DEBUG build** (Constitution Principle VI):
  - The DEBUG DLL currently faults during start-up, a DEBUG-only defect unrelated to this spec.
  - Validation and the logs of FR-007 are expected to use the release build, as in specs 002–006. The release build
    with `#define DEBUG` in `PCH.h` writes the same logs.
  - That fallback was approved by the user for this spec (Clarifications).
- **No setting or INI key changes** (Constitution Principle V).
