# Data Model: Fix Skip Channeling patch looping on bound weapon spells

No persisted data changes: the INI keys and save databases stay as they are. The model below covers the in-memory
state and intents that the fix adds or changes.

## HandMode (existing, `include/Domain/State.h`), new fields

| Field | Type | Default | Meaning |
|---|---|---|---|
| `chargeSkipped` | `bool` | `false` | The patch forced this hand's charge to complete in the current cast attempt (research R3) |
| `skipMagickaOwed` | `float` | `0` | The charge cost the game did not drain because the charge was skipped (research R2, gate T015). Set together with `chargeSkipped`, paid in `OnSpellFired`, and reset with `chargeSkipped`, so a failed attempt costs nothing extra |

### Lifecycle and validation rules

- **Set** in the same frame the domain emits the forced-charge intent for this hand (phase `Casting`, caster state
  `kUnk02`, patch ON, the slot spell is classified `Cast` or `Bound` by `DetectSpellType`, charge time > 0):
  - **single hand**: in `PumpCastPhase`.
  - **dual cast** (`_session.isDualCasting`): in `PumpAutomatic`, for **both** hands at once, and only when both
    reported `chargeSkipEligible` in that frame (research R11, FR-012).
- **Read** by `OnCasterInterrupt`: when `chargeSkipped == true`, the interruption is a real failure (`FinishHand` +
  `ScheduleSpellFireFinalize`) and never "spurious".
- **Reset** on every path that starts a new attempt or leaves the hand:
  - `EnterHand` (`hm = {}`),
  - `FinishHand`,
  - `ResetSessionState` / `ForceExit` (the `HandMode` structs are value-reset),
  - the transition back to `StartRequested`.

  Constitution Principle II: no flag may survive into the next session.
- The intent is emitted at most once per attempt: while `chargeSkipped == true`, `PumpCastPhase` does not emit
  `forceChargeComplete` again.

### State transitions (Automatic / Press with auto-attack, patch ON)

```text
StartRequested ──caster kUnk02──▶ Casting ──[patch applies]──▶ Casting + chargeSkipped
                                                    │
                     caster kReady ◀── game completes charge (intent executed)
                          │
                          ▼
             WaitingChargeRelease ──UP──▶ (fire) ──MRh_SpellFire──▶ Done ──▶ finalize / restore
                                                    │
   interrupt while chargeSkipped ─────────────────▶ FinishHand + ScheduleSpellFireFinalize (no retry)
   (dual-cast: both hands)
```

## PumpCastPhaseResult (existing, `include/Shared/PumpResults.h`), new field

| Field | Type | Meaning |
|---|---|---|
| `forceChargeComplete` | `bool` | Intent: the controller must complete this hand's charge now (single-hand path only) |
| `chargeSkipEligible` | `bool` | Dual cast only: this hand meets the applicability rule this frame. `PumpAutomatic` pairs both hands (R11) |

## PumpAutomaticResult (existing), new fields

| Field | Type | Meaning |
|---|---|---|
| `forceChargeLeft` | `bool` | Merged from the left hand's `forceChargeComplete`, or set by the dual-cast pairing |
| `forceChargeRight` | `bool` | Merged from the right hand's `forceChargeComplete`, or set by the dual-cast pairing. In dual cast both are always set together |

`SpellSystemController::OnFrame` executes these right after `PumpAutomatic` and before dispatching attack input,
through `MagicAction::ForceChargeComplete` ([contracts/skip-channeling.md](contracts/skip-channeling.md)).

## SpellFiredResult (existing), new field

| Field | Type | Meaning |
|---|---|---|
| `magickaToDrain` | `float` | Intent: the skipped charge cost owed by the fired hand. In dual cast it covers both hands. `SpellSystemController::NotifyAnimEvent` executes it through `MagicAction::DrainMagicka` right after `OnSpellFired` |

## CastInterruptResult (existing), field use

No new fields. On a failed forced charge, `finishedLeft` / `finishedRight` carry the held time from `FinishHand`, so
the controller releases the synthetic attack (Constitution Principle III, "every DOWN gets an UP").

## Diagnostic log records (FR-009)

These are log lines with the prefix `[FLOW] SkipChanneling:`, not stored data. The fields are listed in the contract.
