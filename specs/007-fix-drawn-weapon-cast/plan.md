# Implementation Plan: Fix slot casts failing with a weapon or shield in the left hand after drawing weapons

**Branch**: `fix/drawn-weapon-cast` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/007-fix-drawn-weapon-cast/spec.md`

## Summary

With the weapons drawn and a weapon or shield in the left hand, slot casts fail, except the first one after a draw.

**What makes a drawn activation different (code review)**: the plugin swaps the hand's item for the spell and presses
that hand's attack/block action about one frame later. It does not raise the hands and sets no instant-equip variable.
At the end, it re-equips the item at once, through the game's equip queue, with no sheathe and no draw.

**Five candidate causes**, each with a log signature and a ready design (research R3–R7):
- **H1**: the press arrives before the game switched the hand to magic, so it blocks or swings.
- **H2**: the restore while drawn leaves the hand's animation state stale.
- **H3**: a stale caster spell trips the "wrong spell" check.
- **H4**: a late equip event of the plugin's own restore is read as a foreign equip.
- **H5**: the swap passes through a "sheathing" weapon state.

**Approach** (spec FR-007):
1. **Reproduce first** on the current build, whose log already shows H1, H3, H4 and H5 (research R1).
2. **Always ship** a diagnostic log of both hands (R2, FR-008) and the new TEST_MATRIX cases (R9, FR-009).
3. **Fix only the confirmed causes**:
   - D1, for H1: a short hand-switch wait before the first press, at most 0.5 s;
   - D2, for H2: a restore without the queue, or D1's ready gate;
   - D3, for H3 and D5, for H5: these checks do not apply during the start phase;
   - D4, for H4: the session's own snapshot items are never foreign.

   If nothing fails, the bug is closed as already fixed.

**Outcome (T009, 2026-11-21, research R11)**:
- **The failure**: patches OFF, a left-hand slot activated during the draw animation. Equipping the spell restarts the
  draw (~1.4 s), and spec 005's 1 s bound ends the session before the left cast can begin.
- **The fix, D6**: the game's equip animation time is kept out of that bound, up to 2 s.
- **Not built**: D1–D5, because none of H1–H5 caused a failure.

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: CommonLibSSE-NG (`extern/CommonLibVR-ng`): `ActorEquipManager`, `PlayerControls`
attack/block handler, animation graph variables, `MagicCaster`.

**Storage**: N/A. No settings, INI or persisted data changes (FR-010).

**Testing**:
- **No automated harness.** Validation is in game, following [quickstart.md](quickstart.md): reproduction gate, fix,
  regression.
- **Build**: the release DLL, whose log is on because `PCH.h` defines `DEBUG` (research R10).

**Target Platform**: Windows x64, Skyrim SE / AE

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**:
- **No per-frame cost** beyond the existing checks. The diagnostic lines run a few times per activation.
- **D1** adds one graph-variable read per frame, only for a hand that is waiting.

**Constraints**:
- **No sheathe and redraw** (FR-003).
- **Every new wait has a timeout** (Constitution III).
- **Both dual-cast hands go DOWN together** (Constitution IV).
- **The root cause is unknown** until the reproduction gate runs (research R1). It is resolved by that gate, not by
  more reading.

**Scale/Scope**:
- **Always**: one new shared helper, three log points and one dropped-input log.
- **Conditional**: D1–D5 in `src/Domain/MagicStatePump.cpp`, `src/Domain/MagicStateSlot.cpp`,
  `src/Domain/MagicStateLifecycle.cpp`, `include/Domain/State.h`, `src/Adapters/Inbound/EquipEventAdapter.cpp`,
  `src/Adapters/Outbound/RestoreEquip.cpp` and `src/Application/SpellSystemController.cpp`, as R1 decides.
- **Docs**: `docs/TEST_MATRIX.md` and `.claude/CLAUDE.md`.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| **I. Layered architecture** | PASS | **R2 helper**: read-only, in `Shared/`, like `CasterUtil` / `InventoryUtil`. **D1/D3/D5**: decisions in the domain, with the ready signal read through the helper. **D2**: changes the outbound adapter `RestoreOneHand`. **D4**: in the inbound adapter. **Debt**: the direct game calls of the `ExitAllNow` sheathe path are not touched, so they are neither expanded nor moved |
| **II. Main-thread state machine** | PASS | **New state**: D1's two `HandMode` fields, cleared by every `HandMode` reset and by `FinishHand` (data-model). **Threads**: no work outside the main thread. **Docs**: `.claude/CLAUDE.md` documents the fields and the wait in the same change |
| **III. Never strand the player** | PASS | D1's wait ends at 0.5 s and every existing exit still applies. No press is sent during the wait, so no release is owed. D4 only stops false foreign-equip exits, and a real foreign item is still respected. R8 checks that no weapon is lost with two copies of the same sword |
| **IV. Dual-cast sync** | PASS | D1 ends the wait for both hands in the same frame ([contract §4](contracts/hand-switch-wait.md)). TEST_MATRIX section 1 is in the regression (quickstart §3) |
| **V. Opt-in & compatibility** | PASS | **Bug fix, no patch flag**: it restores the intended behavior (a drawn slot casts) and does not change the vanilla cast flow. With weapons sheathed, or a hand that already holds a spell, nothing changes. **D1's delay**: at most 0.5 s, and only for a drawn hand swap, which fails today. **Settings**: no INI or persisted-data change |
| **VI. Observable & verified** | PASS with deviation | **Logs**: each new decision logs with a prefix (`[FLOW] HandSwitch:`, `[State] Activation hands:`, `[SpellSystem] Press hands:` / `Restore hands`, `[Action] synthetic attack dropped`). **Tests**: TEST_MATRIX gains 7.11–7.16. **Build**: validation runs on the release DLL; see Complexity Tracking |

**Post-design re-check (after Phase 1)**:
- **Unchanged.** The data model adds only the two D1 fields (conditional), and the contracts keep every wait bounded
  and both dual-cast hands paired.
- **Constitution VI** says debug logging compiles out of release builds, but `PCH.h` defines `DEBUG` unconditionally.
  That pre-dates this spec, and the plan relies on it for R1, but this spec does not change it.

## Project Structure

### Documentation (this feature)

```text
specs/007-fix-drawn-weapon-cast/
├── plan.md                          # This file
├── research.md                      # Phase 0: R1–R10 (reproduction gate, H1–H5, logs, tests, build)
├── data-model.md                    # Phase 1: D1 fields (conditional), D4 query, reset rules
├── quickstart.md                    # Phase 1: reproduction, validation, regression
├── contracts/
│   ├── diagnostic-log.md            # Phase 1: hands description and log points (always)
│   └── hand-switch-wait.md          # Phase 1: D1 wait (if H1 is confirmed)
├── checklists/
│   └── requirements.md              # Spec quality checklist
└── tasks.md                         # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
include/Shared/HandDiagnostics.h           # NEW (always): DescribeHandsForLog
src/Shared/HandDiagnostics.cpp             # NEW (always)
src/Domain/MagicStateLifecycle.cpp         # Activation hands log (always); D5 in ShouldForceInterrupt, D3 mismatch skip
src/Application/SpellSystemController.cpp  # Press hands / Restore hands logs (always)
src/Adapters/Outbound/SyntheticInput.cpp   # dropped-input log (always)
include/Domain/State.h                     # D1: HandMode fields, kMaxHandSwitchWaitSecs
src/Domain/MagicStateSlot.cpp              # D1: wait entry in OnSlotPressed, skip in OnEquipComplete, FinishHand reset
src/Domain/MagicStatePump.cpp              # D1: wait end in PumpCastPhase, dual pairing, guards in NotifyAttackEnabled / OnCasterInterrupt
src/Adapters/Outbound/RestoreEquip.cpp     # D2: unqueued equip while drawn; R8 instance choice (if needed)
src/Adapters/Inbound/EquipEventAdapter.cpp # D4: own snapshot items are not foreign
include/Application/SpellSystemController.h # D4: ActiveSlotContents snapshot IDs
docs/TEST_MATRIX.md                        # 7.11–7.16 (always)
.claude/CLAUDE.md                          # diagnostic log; D1 fields and wait (if shipped)
```

**Structure Decision**: a single SKSE plugin project. The only new files are the read-only diagnostic helper in
`Shared/`, next to `InventoryUtil`. Each conditional change sits where the decision it belongs to is already made.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **VI**: reproduction and in-game validation on the release DLL instead of a DEBUG build (**approved by the user, 2026-11-20**) | The DEBUG DLL faults inside `Hooks::Install_Hooks` at start-up, a DEBUG-only defect unrelated to this spec. The release build writes the same logs while `PCH.h` defines `DEBUG` | Fixing the DEBUG crash first: it is a separate defect with no root cause yet. Specs 002–006 were approved the same way |
| **Conditional design** (D1–D5 ready, only the confirmed ones built) | The cause cannot be known from the code alone. The game's animation and equip timing only shows in a log (Constitution VI rationale) | Building all five: three of them may change working restore and equip-event behavior for nothing. Guessing one: the report's two patterns (setups, first cast after a draw) point to different candidates |
