# Specification Quality Checklist: A configurable font size for the spell names

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-11-22
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

- **No clarification asked**: the open choices have a reasonable default, recorded in Assumptions.
  - **Unit**: a percentage, 50% to 200% in 5% steps, where 100% is today's size.
  - **Scope**: one size for the HUD and the popup names, like "Position" and "Padding".
  - **Apply**: like the other HUD settings, with "Apply changes".
- **Background and file names**: the Background section and the last assumption name `styles.ini`, the font loading
  and the likely files, the same way earlier specs named the code they touch. The task file pointed to `HudSettings`
  and `ConfigAdapter`, but the spell name settings live in `StyleConfig`, so the spec records the correction. The
  requirements stay at the level of what the player sees.
- **Validation pass 1**: FR-004 ("steps the player can set precisely") and the missing-font edge case ("applies, or is
  ignored") were not testable. Both now state one behavior (5% steps; the size applies to the fallback font).
- **For the plan**:
  - **Sharpness (FR-007)**: the HUD uses ImGui 1.89.1, where a font is prepared at one size. Sharp large names need
    the font prepared at the name size, and preparing it again when the size changes.
  - **Memory**: Chinese and Japanese at 200% need much more font memory (Edge Cases).
  - **Per-frame rules**: no file I/O or blocking work in per-frame code (Constitution, Technical Constraints).
- **Branch base**: the branch started from `fix/debug-build-define`, because validation needs the spec 010 debug build.
  It was moved onto `main` at `83598b7` once spec 010 merged (#79).
