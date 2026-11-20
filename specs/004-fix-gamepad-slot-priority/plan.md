# Implementation Plan: Fix gamepad slots 5+ losing input priority to the game's default actions

**Branch**: `fix/gamepad-slot-priority` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/004-fix-gamepad-slot-priority/spec.md`

## Summary

On the default gamepad layout every slot shares `RT`, and slots 1–4 own the four face buttons. A slot 5+ combo
therefore always shares buttons with slots 1–4. Three input-layer mechanisms make those buttons reach the game
(research R1):
- **M1**: the event filter attributes a shared button to the **lowest-numbered** slot that uses it, without checking
  the others.
- **M2**: held-back buttons are replayed to the game when a 40 ms timer ends, **regardless of which slot
  activated**.
- **M3**: overlapping or identical combos have no winner rule.

**M2 is confirmed in game by the user (2026-11-20)** and is the most likely primary cause.
- On slot 2 `R2 + Triangle`, Triangle slightly before R2 makes the character jump: the first button's 40 ms window
  ends before the slot accepts the combo.
- R2 first "works" only because the spell equip masks R2's attack.
- The press-order test is therefore part of User Story 1.
- The fix covers a normal quick chord, with the rest of the combo arriving within the combo tolerance (50 ms since the
  2026-11-20 tuning, 40 ms before). A slower chord behaves as today (research R3 "Coverage limit").

The fix stays in the input layer:

1. **Every candidate slot is evaluated** for each button (R2). Any candidate being completed filters the button.
   Otherwise one owner holds it back.
2. **Held-back buttons are resolved by outcome** (R3):
   - they are **consumed** when any slot containing them activates,
   - they are held **while a slot containing them is still pending** (up to 0.5 s),
   - they are held **while a chord is in progress**: 2 or more of a candidate combo's buttons held (up to 100 ms after
     the 50 ms tuning, 0.15 s before; Clarification 2026-11-20, findings H1/H2),
   - otherwise they are handed to the game, as today.
3. **Each press has a fate** (R11, Clarification 2026-11-20, finding H3). A button's held repeats and release follow
   its own press: consumed, or passed to the game. An active session no longer blocks new presses, so `RT` can cast
   manually during a Press session.
4. **Longest combo wins** among slots completing at the same moment. Identical combos go to the lowest number, with a
   warning at hotkey load (R4, R5, Clarification 2026-11-19).
5. **Diagnostics** say which slot held a button back and whether it was consumed or handed to the game (R6).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: CommonLibSSE-NG v16.9.0 (`RE::ButtonEvent`, `RE::BSWin32GamepadDevice::Key`,
`RE::INPUT_DEVICE`), spdlog 1.17

**Storage**: N/A. No INI, binding encoding or persistence changes.

**Testing**: no automated harness. The new combo helpers are pure (no `RE::` types). Validation uses the
TEST_MATRIX cases in [quickstart.md](quickstart.md) on the release DLL (Constitution VI deviation, see Complexity
Tracking).

**Target Platform**: Windows x64, Skyrim SE / AE via Address Library

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**: negligible per-frame cost.
- Candidate evaluation is at most 64 slots × 3 keys per button event.
- The overlap check runs only among slots rising in a frame (usually 0 or 1).
- The identical-combo scan (at most 2016 pairs) runs only at hotkey load.

**Constraints**:
- Main thread only (Constitution II). `KeyStateStore` is written only from the `PollInputDevicesHook` path.
- A single button with no combo in progress must reach the game after the filter window, 50 ms since the 2026-11-20
  tuning (FR-003).
- No patch, flag or INI changes (Constitution V).

**Scale/Scope**: 8 source and header files in `include/Input`, `include/Shared` and `src/Input` (see Project
Structure), plus `docs/TEST_MATRIX.md` and `.claude/CLAUDE.md`. No `Domain/` or `Application/` change.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle / gate | Assessment | Status |
|---|---|---|
| **I. Layered architecture** | Changes stay in `Input::` and `Shared/InputConstants.h`. `MagicState`, `SpellSystemController` and `DispatchSlotEvents` are not touched. Overlap resolution happens before pressed edges are published, so the application layer still receives one press per resolved slot. | ✅ Pass |
| **II. Main-thread state machine integrity** | Every touched function runs inside `PollInputDevicesHook` → `ProcessAndFilter`. No new `MagicState` flags. **But** new arrays live in `ExclusiveStore` (input layer), outside the structs this principle lists: three per slot, and the per-code press fate and owner (R11). See Complexity Tracking. | ⚠️ Deviation (justified) |
| **III. Never strand the player** | Improves it. Buttons can no longer trigger game actions that interrupt a slot's cast. The contested hold has a hard 0.5 s cap, so no button is swallowed indefinitely. | ✅ Pass (improves) |
| **IV. Dual-cast synchronization** | Not touched: no change to synthetic input, the pump or `OnCasterInterrupt`. Dual-cast regression cases 1.1, 1.3 and 1.5 are still in the validation set (SC-005). | ✅ Pass |
| **V. Opt-in & compatibility** | No flag, default, binding or INI key changes. Longest-combo-wins and identical-combo resolution fix behavior that was undefined (two slots overwriting each other in one frame); they are not new optional behaviors. Single-button gameplay timing is unchanged (FR-003). | ✅ Pass |
| **VI. Observable & verified** | Adds `[Input] HoldBack`, `SUPPRESSED` and `HotkeyCache` lines (FR-007), and TEST_MATRIX cases 6.15–6.23 (FR-008). In-game validation on the **release** DLL relies on the existing `#define DEBUG`. Both deviations were **approved by the user for this spec on 2026-11-20**. See Complexity Tracking. | ⚠️ Deviation (accepted) |

**Post-design re-check (after Phase 1)**: unchanged.
- **Data model**: three input-layer arrays and one constant (`kMaxContestedHoldSec`), plus pure combo helpers.
- **Contract**: single-button timing is unchanged, and the outcome no longer depends on slot number.

## Project Structure

### Documentation (this feature)

```text
specs/004-fix-gamepad-slot-priority/
├── spec.md                  # Feature spec (Clarifications 2026-11-19)
├── plan.md                  # This file
├── research.md              # Phase 0: R1–R10
├── data-model.md            # Phase 1: ExclusiveStore fields, helpers, lifecycles
├── quickstart.md            # Phase 1: in-game validation guide
├── contracts/
│   └── input-ownership.md   # Phase 1: candidates, held-back fate, overlap rule, log lines
├── checklists/
│   └── requirements.md      # Spec quality checklist
└── tasks.md                 # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
include/
├── Shared/
│   └── InputConstants.h        # + kMaxContestedHoldSec (R3b), kMaxChordHoldSec (R3c); kComboToleranceSec 50 ms + kSingleKeyAcceptDelaySec replace kFilterReplayDelaySec
└── Input/
    ├── ExclusiveStore.h        # + suppressedUntilRelease, heldBackSecs, contestedLogged (R3, R4)
    │                           # + PressFate enum, kbFate/gpFate, kbOwner/gpOwner (R11)
    ├── ExclusiveTracker.h      # TickFilterWindows signature: + cache, keys (R3b, R3c)
    ├── HotkeyMatcher.h         # + ComboKeyCount, ComboHeldCount, ComboStrictSubset, ComboSameKeys,
    │                           #   DescribeHeldCode (moved here; pure)
    └── InputFilter.h           # + EffectiveComboCode(dev, rawIdCode) declaration

src/
└── Input/
    ├── InputFilter.cpp         # ShouldFilterAndSave: press-fate gate + candidate evaluation + owner choice + logs (R2, R6, R11)
    │                           # EffectiveComboCode definition (uses GamepadIdToIndex)
    ├── ExclusiveTracker.cpp    # RecomputeSlotEdges: three passes, overlap resolution, consume on activation (R3a, R4)
    │                           # TickFilterWindows: contested hold (0.5 s) + chord in progress / complete (100 ms), handed-to-game log (R3b, R3c)
    │                           # fate resolution on consume / Success / discard / handed to game (R11)
    │                           # resets of the new fields in ResetExclusiveState / ClearEdgeStateOnly
    └── HotkeyMatcher.cpp       # LoadHotkeyCache_FromConfig: identical-combo warning (R5)

docs/TEST_MATRIX.md             # section 6: new cases 6.15–6.23 (FR-008)
.claude/CLAUDE.md               # Input system: shared buttons, held-back lifecycle, overlap rule, logs
```

**Structure Decision**: single-project SKSE plugin. The fix stays where input events are filtered and slot edges are
computed. No new files are needed. `EffectiveComboCode` is declared in the existing `InputFilter.h`, so
`ExclusiveTracker.cpp` can map held-back events to combo codes.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **Principle II**: "New state MUST live in the existing structs (`SessionState`, `HandMode`, ...)". This plan adds `suppressedUntilRelease`, `heldBackSecs` and `contestedLogged` (per slot) and `kbFate` / `gpFate` / `kbOwner` / `gpOwner` (per input code, R11) to `ExclusiveStore`. | They are input-layer bookkeeping for the existing per-slot timing windows, alongside `filterWindowActive`, `pendingTimer` and the other fields already in `ExclusiveStore`. They are not session flags, and no session logic reads them. They are reset with the existing input resets (`ResetExclusiveState`, `ClearEdgeStateOnly`, menu close). Same justification as spec 003. | Putting them in `MagicState` structs would move input-layer data into `Domain/` and break Principle I. **Follow-up (optional)**: a PATCH clarification of the constitution ("New **MagicState** state MUST…"). |
| **Principle VI**: in-game validation on the **release** DLL, not a DEBUG build. **Approved by the user for this spec on 2026-11-20** (spec Clarifications). Approvals for specs 002 and 003 did not carry over. | The DEBUG DLL still fails to load (SKSE: "fatal error occurred while loading plugin"). The release DLL writes the same `[Input]` lines (next row). | Fixing the DEBUG load failure first: it is unrelated to input and goes to its own spec (as decided in spec 003). |
| **Principle VI**: "Debug logging MUST compile out of release builds" is already violated by `#define DEBUG` in `include/PCH.h`. This validation relies on it, and the new `[Input]` lines will ship in release. | It is the only log evidence available while the DEBUG DLL cannot load. The new lines fire on press/release events and hotkey loads only, never per frame. | Making `DEBUG` conditional now would remove the validation evidence. It belongs to the DEBUG-build spec, which must fix the load failure and this define together. |
