# Feature Specification: Fix Skip Channeling patch looping on bound weapon spells

**Feature Branch**: `feat/patch-for-spell-channeling-skip` (current branch; no dedicated branch created)

**Created**: 2026-11-19

**Status**: Implemented (code + docs, both presets build green). Gate T015 = under-charge, so T016 (owed charge
cost drained after the spell fires) is implemented; re-run = parity. Pending in-game: T001, T017, T018, T022, T025

**Input**: User description: "Corrigir o comportamento do patch de skip channeling para bound weapons. Quando é uma
bound weapon e o patch esta ativado o personagem fica preso em um loop como se iniciasse o cast, ele fosse
interrompido e ele tentasse ficar reiniciando novamente. Este patch esta com problemas apenas para esse caso
especifico."

## Background & Investigation Findings *(context)*

Findings from reading the current code and the user's plugin log of 2026-11-19 (see `research.md` R1). They
explain the defect and bound the scope; they do not prescribe the fix.

1. **What the patch does today**: during an automatic cast, once the hand's caster reaches its charging state and
   the spell has a charge time, the patch drains the remaining magicka cost and zeroes the hand's charge timer so the
   spell fires without the charging animation. The check looks only at "spell has a charge time". It does **not**
   look at the spell type.
2. **Documented intent says otherwise**: the in-game tooltip says the patch "only affects Fire-and-Forget spells (not
   Concentration or Bound)", and the original design note (`.claude/tasks/P1-skip-channeling-patch.md`) lists
   "Bound → not applied: the summoning equip is intentional". Bound weapon spells are fire-and-forget with a charge
   time, so the patch is applied to them anyway. **The 2026-11-19 decision supersedes this exclusion** (see
   Clarifications): bound weapons are in scope, but only their charging animation is skipped.
3. **When it triggers**: the patch path runs for any hand whose cast is driven automatically, meaning Automatic
   mode or Press mode with auto-attack. The default for bound weapons is Press without auto-attack, so the bug shows
   up once the player configures a bound weapon spell to cast automatically. The tooltip also only mentions Automatic
   mode, although Press with auto-attack is affected too.
4. **The restart loop (confirmed from the log)**:
   - When the patch skips the charge, it also takes magicka off the player itself. With Bound Sword, which is
     expensive, this leaves the player below the spell's cost, so the **game cancels the cast** right away, before
     anything is summoned.
   - The plugin treats a cancellation inside the first 0.2 s of the casting phase as "spurious" pre-cast noise and
     retries.
   - The game then refuses to start new casts until magicka regenerates, so the plugin keeps retrying. This is the
     start → interrupted → restart cycle the user describes.
   - An earlier hypothesis blamed the summoned weapon being equipped. The log rules it out: the cancellation happens
     before the spell fires.
5. **Why it looks bound-specific**: the failure depends on the spell's cost relative to the player's current magicka,
   not on the spell being a bound weapon. Bound Sword is the user's expensive spell, and the cheap spells tested
   (Candlelight) completed. Other expensive fire-and-forget spells may hit the same loop.
6. **How long the player is stuck**: nothing caps the number of restarts. The loop only ends when the session safety
   timeout (30 s) forces an exit.

## Clarifications

### Session 2026-11-19

- Q: With Skip Channeling ON, should bound weapon spells be excluded from the patch, or should the patch apply to
  them? → A: **Apply it (option B).** The patch only skips the hand animation of the character charging or
  concentrating the spell. The animation of the weapon appearing in the hand MUST NOT be removed.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Bound weapon slot works with Skip Channeling enabled (Priority: P1)

A player has the Skip Channeling patch enabled and a bound weapon spell (for example Bound Sword) on a slot set to
cast automatically. They press the slot hotkey. The hand charging animation is skipped, the weapon appears in the
hand with its normal summon visual, and the slot session finishes normally, with no cast/interrupt/restart loop.

**Why this priority**: today the character gets stuck for up to 30 seconds and cannot act. This is the defect
being reported.

**Independent Test**: enable Skip Channeling, assign Bound Sword to a slot in Automatic mode, and press the hotkey.
Observe that no hand charging pose appears, the weapon-appearing visual plays, the weapon is summoned once, and the
session ends normally. Repeat with Press mode with auto-attack.

**Acceptance Scenarios**:

1. **Given** Skip Channeling ON and a bound weapon spell in Automatic mode on the right hand, **When** the slot
   hotkey is pressed, **Then** the bound weapon is summoned exactly once and the character is controllable again,
   with no repeated cast attempts.
2. **Given** the same setup in Press mode with auto-attack, **When** the hotkey is pressed, **Then** the outcome is
   the same as scenario 1.
3. **Given** the same setup on the left hand, **When** the hotkey is pressed, **Then** the outcome is the same as
   scenario 1.
4. **Given** the session ended, **When** the player checks their equipment, **Then** the bound weapon and the
   equipment restore behave exactly as they do with the patch OFF.
5. **Given** Skip Channeling ON and a bound weapon spell, **When** the hotkey is pressed, **Then** the character does
   not hold the hand charging (concentrating) pose before the summon. At most the first frames of the cast start
   are visible (SC-006).
6. **Given** the same setup, **When** the weapon is summoned, **Then** the visual of the weapon appearing in the hand
   plays as it does with the patch OFF.

---

### User Story 2 - Other spells keep their current Skip Channeling behavior (Priority: P2)

A player who uses Skip Channeling for regular fire-and-forget spells (for example Firebolt, Ice Spike) sees no change:
those spells still fire instantly. With the patch OFF, bound weapons behave as they do today.

**Why this priority**: the fix must not regress the cases that work today (Constitution Principle V).

**Independent Test**: TEST_MATRIX 5.4 and 1.3 with the patch ON, plus 3.7–3.9 with the patch OFF.

**Acceptance Scenarios**:

1. **Given** Skip Channeling ON and a fire-and-forget damage spell in Automatic mode, **When** the hotkey is pressed,
   **Then** the spell fires without the charging animation, as before.
2. **Given** Skip Channeling ON and the same fire-and-forget spell in both hands with the dual-cast perk, **When**
   the hotkey is pressed, **Then** a single instant dual cast occurs (TEST_MATRIX 1.3).
3. **Given** Skip Channeling OFF and a bound weapon spell in any mode, **When** the hotkey is pressed, **Then** the
   behavior is identical to the current release (TEST_MATRIX 3.7–3.9).
4. **Given** a slot with a bound weapon spell in one hand and a fire-and-forget spell in the other, with Skip
   Channeling ON, **When** the hotkey is pressed, **Then** each hand behaves according to its own spell type, and
   neither hand loops.

---

### User Story 3 - The patch description matches what it does (Priority: P3)

A player reading the Skip Channeling tooltip in the in-game Patches tab learns exactly which spells and activation
modes it affects.

**Why this priority**: the current tooltip already disagrees with the behavior (it says Bound is excluded and only
mentions Automatic mode). Fixing the text keeps players' expectations right, but it does not change gameplay.

**Independent Test**: open the Patches tab, hover Skip Channeling, and compare the text with the behavior verified in
User Stories 1 and 2.

**Acceptance Scenarios**:

1. **Given** the Patches tab is open, **When** the player hovers Skip Channeling, **Then** the tooltip names the
   affected spell types and activation modes, and its statement about bound weapons matches FR-002.

---

### Edge Cases

- **Bound weapon already summoned**: casting the same bound spell again while its weapon is still active must not
  start a loop.
- **Two-handed bound weapon** (for example Bound Bow), which occupies both hands when summoned from one hand.
- **Same bound spell in both hands** (dual-cast bound): must follow the dual-cast synchronization rule
  (Constitution Principle IV) and still summon only once per hand.
- **Not enough magicka** for the bound spell: the attempt must fail cleanly and end the session, with no loop.
- **Interruption by the player** (block, stagger, opening a menu) during the summon: the existing forced-exit rules
  apply unchanged.
- **Skip Equip Animations patch ON together with Skip Channeling**: the bound summon must still not loop. Whether the
  weapon-appearing visual is shortened is up to Skip Equip Animations, not Skip Channeling.
- **Mods that change bound weapon spells** (custom bound weapons using the same effect archetype) are treated as
  bound weapons.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: With Skip Channeling ON, activating a slot whose hand holds a bound weapon spell (Automatic mode, or
  Press mode with auto-attack) MUST produce exactly one cast attempt that summons the weapon, with no repeated
  start/interrupt/restart cycles.
- **FR-002**: With Skip Channeling ON, the patch MUST also apply to bound weapon spells. The hand charging
  (concentrating) animation is skipped, exactly as for other fire-and-forget spells.
- **FR-003**: When a hand's cast is interrupted because the slot's own bound weapon was equipped into that hand (the
  summon succeeded), the system MUST treat it as cast completion, not as a spurious interruption to retry.
- **FR-004**: After a bound weapon summon, the session MUST end and restore equipment exactly as it does with Skip
  Channeling OFF (Constitution Principle III).
- **FR-005**: Fire-and-forget spells that are not bound weapons MUST still fire instantly under Skip Channeling,
  including dual cast (Constitution Principle IV). The magicka spent per cast MUST equal the spell's normal cost
  without the patch, with no extra charge.
- **FR-006**: With Skip Channeling OFF, bound weapon behavior in every activation mode MUST remain identical to the
  current release (Constitution Principle V).
- **FR-007**: In a slot with two different spells, the patch decision MUST be made per hand, based on that hand's
  spell type.
- **FR-008**: The Skip Channeling tooltip in the in-game Patches tab MUST accurately state the affected spell types
  and activation modes (Automatic, and Press with auto-attack). It MUST say that bound weapon spells are included and
  that only the charging animation is skipped while the weapon summon visual is kept. The outdated exclusion in
  `.claude/tasks/P1-skip-channeling-patch.md` MUST be updated to match.
- **FR-009**: The DEBUG log MUST show, for each hand, whether the patch was applied or skipped for that cast and why
  (Constitution Principle VI).
- **FR-010**: `docs/TEST_MATRIX.md` MUST gain cases for bound weapon spells with Skip Channeling ON: Automatic, Press
  with auto-attack, each hand, dual bound, a mixed bound + fire-and-forget slot, and a two-handed bound weapon.
- **FR-011**: Skip Channeling MUST NOT remove or shorten the visual of the bound weapon appearing in the hand. It
  skips only the charging animation that happens before the summon.
- **FR-012**: In dual cast (the same spell in both hands), Skip Channeling MUST complete both hands' charge in the
  same frame, or neither. One hand must never fire before the other because of the patch (Constitution
  Principle IV).

### Key Entities

- **Spell type classification**: the existing categorization of a slot spell (Cast, Concentration, Bound, Power,
  Shout). "Bound" covers any spell with a bound-weapon effect.
- **Skip Channeling setting**: the existing global patch flag (default OFF) in the Patches tab and the INI.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With Skip Channeling ON, 20 out of 20 activations of a bound weapon slot (covering Automatic, Press
  with auto-attack, left hand and right hand) summon the weapon exactly once and return control to the player within
  3 seconds of the key press.
- **SC-002**: Across those activations, the player is never stuck: zero sessions end through the 30-second safety
  timeout.
- **SC-003**: With Skip Channeling ON, fire-and-forget spells that are not bound weapons fire with no charging
  animation in 100% of TEST_MATRIX 5.4 and 1.3 runs.
- **SC-004**: With Skip Channeling OFF, TEST_MATRIX 3.7–3.9 (bound weapon × each mode) give the same results as the
  current release.
- **SC-005**: The DEBUG log of a bound weapon activation with the patch ON contains no "restart" (UP → DOWN next
  frame) entries for that hand.
- **SC-006**: In 100% of bound weapon activations with the patch ON:
  - the sustained hand charging pose (the charge glow held until release) is not shown, and only the first frames
    of the cast start may appear before the spell fires;
  - the weapon-appearing visual is shown.
- **SC-007**: For each tested spell (Bound Sword, one expensive and one cheap fire-and-forget spell), the magicka
  spent per cast with the patch ON is within 1 point of the magicka spent with the patch OFF.

### Verification (TEST_MATRIX)

Per Constitution Principle VI:

- **New cases**: bound weapon + Skip Channeling ON in Automatic, Press with auto-attack, left hand, right hand, dual
  bound, a mixed bound + fire-and-forget slot, and a two-handed bound weapon (FR-010). Also: a concentration spell
  with the patch ON is not affected (5b.11), and dual cast completes both hands in the same frame (FR-012).
- **Affected**: 3.7, 3.8, 3.9 (bound × modes), 5.4 and 5.5 (patch combinations), 1.3 (dual cast with Skip
  Channeling).
- **Regression baseline**: 5.1.

## Assumptions

- The original design excluded bound weapons from the patch. The user decided on 2026-11-19 to include them (only
  the charging animation is skipped), so the code's current inclusion of bound weapons is kept and the loop is what
  gets fixed.
- A spell counts as a bound weapon spell if it has at least one bound-weapon effect, as the plugin already classifies
  it today.
- The loop mechanism in Finding 4 was confirmed from the user's log during planning (`research.md` R1). The game's
  exact magicka-deduction model (progressive while charging vs. at release) is measured in the first implementation
  tasks before the cost handling is finalized (`research.md` R2).
- Concentration spells are out of scope: they have no charge timer to skip, and the user reports no problem with them.
- No new configuration option is introduced. The existing Skip Channeling flag (default OFF) covers bound weapons.
- The patch completes the charge as soon as the hand's caster enters its charging state, so the first frames of the
  cast start can still be visible. Removing them entirely would mean bypassing the cast, as the original P1 design
  with `CastSpellImmediate` did; that was rejected in `research.md` R2. Hence the tolerance in SC-006.
- "Fire-and-forget" in this spec means the plugin's own classification of the slot spell (`Cast` or `Bound`), the
  same categorization used for per-type activation defaults.
- The "weapon appearing in the hand" visual is the game's normal summon/equip visual for the bound weapon, which
  plays after the spell fires. The charging animation is the hand pose held while the spell charges, before it fires.
