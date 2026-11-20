# Specification Quality Checklist: Fix gamepad slots 5+ losing input priority to the game's default actions

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-11-19
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- **Validation pass 1 (2026-11-19)**: one open item. One `[NEEDS CLARIFICATION]`, the overlapping/identical combo
  rule, is referenced from Edge Cases and FR-004. It is waiting for the user's answer.
- **Validation pass 2 (2026-11-19)**: all items pass. The user answered Q1 with option A: the longest combo
  completed at that moment wins, with no added wait; identical combos go to the lowest-numbered slot, with a log
  warning. It is recorded in the spec's Clarifications and applied to Edge Cases, FR-004, the new FR-009 and the new
  SC-007.
- **Revision (2026-11-20)**: the user's in-game evidence (press order: Triangle before R2 → jump) was added as
  Background item 7.
  - US1 now includes the press-order scenarios 4–6, and US2 covers buttons used on their own.
  - SC-002 now covers both press orders on slots 1–8.
  - The "Slow chord" edge case and the chord-tolerance assumption were added.
  - All items still pass.
- **Revision 2 (2026-11-20)**, after `/speckit-analyze`. Clarification session 2026-11-20 resolves findings H1–H3:
  - FR-001 is qualified by the chord tolerance (second button within 40 ms, whole combo within 0.15 s),
  - new FR-010 (press fate),
  - new SC-008 (Press-mode manual cast, Hold mode),
  - SC-002 now uses the log to decide which presses are chords.
  - It also adds the edge cases "Chord in progress held on purpose", "Casting manually during a session" and
    "longer combo cancelled", the L5 sentence, the Detection window and Press fate entities, and records the Principle
    VI approval.
  - All items still pass.
- **Revision 3 (2026-11-20)**, after the first in-game run: the user set the combo tolerance to 25 ms for combos of 2+
  buttons. FR-001, the edge cases, the Detection window entity and the chord-tolerance assumption now use 25 ms / 50 ms
  (3-button combos). All items still pass.
- **Revision 4 (2026-11-20)**, after the second in-game run: the user set the combo tolerance to 50 ms. The same items
  now use 50 ms / 100 ms (3-button combos). All items still pass.
- **Background**: it explains the confirmed mechanism in player-level terms (buttons held back, the lowest-numbered
  slot claims a shared button, and the button is handed to the game). It names no files or functions; code
  references belong to `/speckit-plan` research.
- **FR-007** requires a diagnostic log line. The project's constitution (Principle VI) makes logs part of the
  observable behavior, as in specs 001–003.
