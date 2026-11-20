# Implementation Plan: Fix left-hand casting stuck by the block button

**Branch**: `fix/left-hand-block` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/005-fix-left-hand-block/spec.md`

## Summary

The plugin casts a hand by sending the game the same "attack/block" action the player's button sends: the right mouse
button or `LT` for the left hand. Nothing coordinates the two sources. Each frame the game's attack handler gets the
plugin's events and then the player's events for the same action (research R1).

Blocking before or during a plugin cast leaves the game's view of that action out of step. The cast never becomes
stable, and the plugin's restart loop, which has no total bound, shows it as twitching.

The user's tests add two defects:
- a block with the shield does not end a right-hand Hold or Automatic slot,
- an interrupted cast leaves the session open until the 30 s timeout or the hotkey release.

The fix touches the input, application and domain layers:

1. **Hand-button gate** (Input, R2–R4): it runs on the player's attack/block events that would really reach the game,
   after spec 004's combo rules and on replays.
   - **Plugin-cast hand**: its presses are kept from the game. A fresh press cancels Hold and Automatic sessions, and
     has no effect in Press (Q1).
   - **Other hand** (for example the shield): a fresh press reaches the game, so the character blocks at once, and it
     cancels Hold and Automatic sessions (Q3).
   - A kept press stays kept until its release, even after the session.
2. **Takeover at the plugin's first press** (Application, R5): for each hand whose action the game sees held by the
   player, a release is sent first, in the same frame, and that press is then kept until released. A shield held up no
   longer stops the cast.
3. **Cancel path** (Application, R6): `SpellSystemController::OnFrame` consumes the gate's request and runs
   `ForceExit`, with the same `IsPressMode()` rule as the game's `blockStart`. It also wires the transform-shout forced
   exit, which is computed today but never executed (user approved).
4. **Retry bound** (Domain, R7): a new `HandMode::startFailSecs` that restarts do not reset. After 1.0 s without a
   stable cast, `PumpAutomatic` ends the session through `ForceExit` (Q2).
5. **An interrupted cast closes the session** (Domain, R12): a real interruption finishes the hand, plus its dual-cast
   partner, and schedules the spell-fire finalize, so the session ends and the gear comes back.
6. **Unchanged**: Require exclusive hotkey keeps counting a held block button as an extra key (Q4, R13).
7. **Diagnostics** (R8).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: CommonLibSSE-NG (`RE::ButtonEvent`, `RE::BSFixedString`, `RE::PlayerControls`,
`RE::MagicCaster`), spdlog

**Storage**: N/A. No INI, settings or persistence changes.

**Testing**: no automated harness. The gate's decision (`DecideHandEvent`, `TakeHeldForCast`) is pure, with no
`RE::` types. Validation uses the [quickstart.md](quickstart.md) scenarios and the TEST_MATRIX cases in game, on the
release DLL with logging (approved, see Complexity Tracking).

**Target Platform**: Windows x64, Skyrim SE / AE via Address Library

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**: negligible per-frame cost. The gate adds one or two `BSFixedString` pointer comparisons per
button event that passes the filter, and one per injected replay. The bound check is a few float comparisons per frame.

**Constraints**:
- Main thread only (Constitution II). The gate runs inside `PollInputDevicesHook`.
- **Dual cast (Constitution IV)**:
  - the new exits are `ForceExit` or finish both hands,
  - the takeover only adds releases, before both presses of the same frame.
- Combo presses (spec 004) must never be gated as block or attack presses.
- No patch flag, INI or setting changes. Require exclusive hotkey keeps its meaning (R9, R13).

**Scale/Scope**:
- **Code**: about 9 files in `include/Input`, `src/Input`, `include/Application`, `src/Application`,
  `include/Domain` and `src/Domain`.
- **Docs**: `docs/TEST_MATRIX.md` (8.9–8.19) and `.claude/CLAUDE.md`.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| **I. Layered architecture** | PASS | The Input gate receives plain per-hand flags built by `InputController` and does not include `Domain/`. The cancel and the takeover are Application decisions. Domain changes stay inside `MagicState` and reuse `ForceExit` and the spell-fire finalize. Synthetic input goes through the existing `DispatchAttack` (Outbound) |
| **II. Main-thread state machine** | PASS, justified | All work runs on the main thread. The new domain field `startFailSecs` lives in `HandMode` and is reset on every exit path and every value reset. The gate's state lives in the Input layer, as in spec 004 (Complexity Tracking). `.claude/CLAUDE.md` documents the field and the gate |
| **III. Never strand the player** | PASS, improved | The new exits (cancel, give-up) are `ForceExit`: they release every synthetic input and restore the gear. An interrupted cast no longer leaves the session open (R12). No new wait state: the finalize path already has its 0.7 s fallback. Wiring the transform-shout forced exit closes another existing gap (R6) |
| **IV. Dual-cast sync** | PASS, verify | No new single-hand releases inside a dual-cast session. The exits end both hands, and R12 finishes the dual partner too. The takeover releases are sent before both presses of the same frame. `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt` and the synthetic press sites change, so TEST_MATRIX section 1 is mandatory |
| **V. Opt-in & compatibility** | PASS, justified | A bug fix, with no patch flag (R9). No INI changes, and Require exclusive hotkey keeps its meaning (R13). The gate withholds non-combo input events of the plugin-cast hand; that is a deviation from "events not claimed by a slot combo MUST be passed through or replayed" (Complexity Tracking). The other hand's presses still pass, but now cancel Hold and Automatic slots (Q3). That includes a left-hand weapon attack, a native-flow change with no patch flag (Complexity Tracking) |
| **VI. Observable & verified** | PASS, justified | Log lines for every gate decision, takeover, cancel, give-up and interrupt exit (R8). TEST_MATRIX gains 8.9–8.19, and the affected cases are listed (R10). In-game validation on the release DLL was approved by the user on 2026-11-20 (Complexity Tracking) |

**Post-design re-check (after Phase 1)**: unchanged.
- The data model keeps the domain state in `HandMode`, and R12 adds no state.
- The contract keeps combo presses out of the gate and passes the other hand's presses.
- The quickstart covers section 1, the exclusive-hotkey refusal and the spec 004 cases.

## Project Structure

### Documentation (this feature)

```text
specs/005-fix-left-hand-block/
├── plan.md                    # This file
├── research.md                # Phase 0: R1–R13
├── data-model.md              # Phase 1
├── quickstart.md              # Phase 1: in-game validation (§0–§9)
├── contracts/
│   └── hand-button-gate.md    # Phase 1: gate, takeover, session ends, logs
├── checklists/
│   └── requirements.md        # Spec quality checklist
└── tasks.md                   # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
include/
├── Input/
│   ├── HandButtonStore.h       # NEW: HandButtonState, HandButtonStore, HandEventPhase,
│   │                           #      DecideHandEvent, TakeHeldForCast (pure, no RE types)
│   └── InputFilter.h           # FilterEvents gains the gate store + per-hand flags;
│                               #   + GateHandEvent(...) for replays, HandOfUserEvent(...)
├── Application/
│   ├── InputController.h       # + m_handButtons, BuildHandGateFlags, ConsumeHandCancelRequest,
│   │                           #   TakeHeldHandButtons()
│   └── SpellSystemController.h # + DispatchAttackPress(Hand) helper
└── Domain/
    └── State.h                 # HandMode::startFailSecs; MagicState::kMaxCastStartSecs,
                                #   kSpuriousInterruptWindowSecs
src/
├── Input/
│   └── InputFilter.cpp         # user-event → hand mapping (R2); gate after ShouldFilterAndSave (R3)
├── Application/
│   ├── InputController.cpp     # flags from MagicState; gate on replay injection; store resets (R4)
│   └── SpellSystemController.cpp # OnFrame: consume cancel + transform forced exit first (R6);
│                               #   press sites go through DispatchAttackPress (R5)
└── Domain/
    ├── MagicStatePump.cpp      # startFailSecs accumulate/reset; give-up check in PumpAutomatic (R7);
    │                           #   OnCasterInterrupt: kSpuriousInterruptWindowSecs; real interrupt →
    │                           #   finish (+ dual partner) + ScheduleSpellFireFinalize (R12)
    └── MagicStateSlot.cpp      # FinishHand resets startFailSecs
docs/TEST_MATRIX.md             # 8.9–8.19
.claude/CLAUDE.md               # Input system: hand-button gate; HandMode field; retry bound; interrupt exit
```

**Structure Decision**: a single SKSE plugin project. The sources are collected by `file(GLOB_RECURSE …
CONFIGURE_DEPENDS)` (`cmake/CollectFiles.cmake`), so the new header needs no list change.
- The gate sits with the other input filters.
- The takeover and the cancel sit in the controllers that already dispatch synthetic input.
- The bound and the interrupt exit sit in the domain pump that owns the cast loop.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **II**: new state outside the `MagicState` structs (`HandButtonStore` in the Input layer) | The gate tracks input events, which the domain never sees. A kept press must stay kept **after** the session ends, until its release (FR-003), and `HandMode` is reset at every exit | Store it in `HandMode`: it would be wiped at exit, so the release would reach the game alone, and the Input layer cannot include `Domain/`. Spec 004 made the same choice for press fates |
| **V**: withholding input events that no slot combo claims (the player's attack/block events for the hand the plugin is casting, and taken held buttons) | The running slot's cast drives that same game action. Letting the player's events through is the defect (R1). A press still does something: it cancels Hold and Automatic sessions. The other hand's presses still reach the game | Pass them and only bound the retry loop: the twitching would become a failed cast, and the stuck state of spec User Story 3 would remain |
| **V**: a native input-flow change with no patch flag. A fresh press of the other hand's attack/block button cancels Hold and Automatic slots (Q3). With a weapon in the left hand, that button attacks, so a left-hand weapon attack now cancels too | It is the user's decision (Clarification Q3). The block case fixes a defect the user reproduced: today the shield block leaves the slot open (spec Background item 6). The existing `blockStart` rule already means "block ends Hold and Automatic". From the button alone a block press cannot be told apart from a left-hand attack press, because both are `"Left Attack/Block"` | **A patch flag (default OFF)**: with it OFF, the shield block would still leave the slot open, which is the reported defect, and Constitution III forbids stranding the player. **Cancel only with a shield or an empty left hand**: it makes the gate read equipment (a domain query on every press) and splits one button rule into two. It can be added later if dual-wield players ask for it. The press still reaches the game, so the attack itself is not lost |
| **VI**: in-game validation on the release DLL instead of a DEBUG build (**approved by the user, 2026-11-20**) | The DEBUG DLL still fails to load (specs 002–004). The release build with `#define DEBUG` in `PCH.h` writes the same logs | No alternative until the DEBUG load failure is fixed. It was approved per spec for 002, 003, 004 and 005 |
