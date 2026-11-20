# Data Model: Fix left-hand casting stuck by the block button

No persisted data changes. The INI keys, spell settings and slot assignments stay as they are. This model covers the
in-memory state the fix adds.

## Domain (`include/Domain/State.h`)

### `HandMode`, new field

| Field | Type | Meaning |
|---|---|---|
| `startFailSecs` | `float` | Time the current cast attempt has spent without a stable cast (research R7). It accumulates while the hand is plugin-cast and in `StartRequested`, including deferred-restart frames. Restarts do not reset it |

**Reset rules** (Constitution II, "a flag that can survive into the next session is a defect"):
- `RequestAutoAttackStart` (new attempt) and `FinishHand` set it to `0`.
- Every value reset of `HandMode` (`hm = {}` in `EnterHand`, `ResetHandStates`, `PrepareForOverwriteToSlot`, the
  power path of `ExitAllNow`) clears it with the struct.
- A cast that stays in `Casting` for `kSpuriousInterruptWindowSecs` sets it to `0`.

### `MagicState`, new constants

| Name | Value | Meaning |
|---|---|---|
| `kMaxCastStartSecs` | `1.0f` | Retry bound: when a plugin-cast hand's `startFailSecs` reaches it, the session ends through `ForceExit` (spec Q2, FR-005) |
| `kSpuriousInterruptWindowSecs` | `0.2f` | Replaces the local `kSpuriousInterruptWindow` in `OnCasterInterrupt`. Below it an interruption counts as spurious, and past it a cast counts as real and resets `startFailSecs` |

### Interrupted cast (R12), no new state

The last branch of `OnCasterInterrupt` (a real interruption) finishes the hand, plus the other hand in dual cast, and
calls `ScheduleSpellFireFinalize` for each finished hand. It reuses `waitingSpellFireFinalize` and
`spellFireFinalizeSecs`, which already exist.

## Input layer (`include/Input/HandButtonStore.h`, new)

### `HandButtonState` (one per hand)

| Field | Type | Meaning |
|---|---|---|
| `gameHeld` | `bool` | The game sees the hand's attack/block action held by the player. Updated only from events that reach the game: passed events and injected replays (research R4) |
| `lastHeldSecs` | `float` | `heldSecs` of the last event that reached the game, used for the release sent by the takeover (R5) |
| `fate` | `PressFate` (`None` or `Consumed`, enum from `ExclusiveStore.h`) | `Consumed`: the player's current press is kept from the game until its release |

### `HandButtonStore`

| Field | Type | Meaning |
|---|---|---|
| `left`, `right` | `HandButtonState` | Per-hand state. Accessor `For(Hand)` |
| `cancelRequested` | `bool` | A fresh press asked to end the session: of a plugin-cast hand (Q1) or of the other hand (Q3), in Hold or Automatic. `SpellSystemController::OnFrame` consumes it (R6) |

### `HandGateFlags` (computed each frame, not stored)

| Field | Type | Meaning |
|---|---|---|
| `pluginCast` | `bool` per hand | Session active and `!finished && (autoActive \|\| (holdActive && wantAutoAttack))` |
| `mayCancel` | `bool` | At least one hand plugin-cast and `!IsPressMode()`, the same rule as `blockStart` |

`InputController` builds them from `MagicState` (`IsActive()`, `LeftMode()`, `RightMode()`, `IsPressMode()`) at the
start of `ProcessAndFilter`. It passes plain values into the Input layer, which does not include `Domain/`
(Constitution I).

### Pure decision (no `RE::` types, Constitution VI)

```text
enum class HandEventPhase { Press, Held, Release }

DecideHandEvent(HandButtonState& st, bool thisHandPluginCast, bool otherHandPluginCast, bool mayCancel,
                HandEventPhase phase, float heldSecs)
  -> { bool filter; bool requestCancel; }
```

Its rules are the table in research R4 and contract §2. It updates `st.fate`, and it updates `st.gameHeld` /
`st.lastHeldSecs` when the event passes.

`TakeHeldForCast(HandButtonState& st) -> std::optional<float>`:
- if `gameHeld && fate != Consumed`, it sets `fate = Consumed` and `gameHeld = false`, and returns `lastHeldSecs`
  (or 0.1 if not positive);
- otherwise it returns `nullopt`.

At every plugin press it is called for **both** hands (R5).

The RE-dependent part is in `src/Input/InputFilter.cpp`: mapping a `ButtonEvent` to a hand through its user event
(R2), and its phase from `IsDown()` / `IsHeld()` / `IsUp()`.

### Validation and reset rules

- **A fresh press always starts a new fate**: `Consumed` from a missed release never swallows it.
- **`ResetInputState`** (game load) resets the whole store.
- **Menu close** (`m_prevBlocked` true → false) resets both hands' `gameHeld` and `fate`.
- **`cancelRequested`** is cleared when consumed, and also by the resets above.

## Lifecycle of a player's attack/block press

```text
press with no hand plugin-cast ─────────────────────────▶ passes (vanilla), gameHeld = true
   └─ plugin's first press for ANY hand (equip or restart):
        TakeHeldForCast on both hands → synthetic release first, fate = Consumed  [log: button taken at cast start]
        └─ held repeats / release kept; release → fate None                       [log: button released -> gate cleared]

fresh press of a PLUGIN-CAST hand ──────────────────────▶ kept, fate = Consumed
   ├─ mayCancel (Hold / Automatic) → cancelRequested → OnFrame: ForceExit         [log: cancel session]
   └─ Press                        → no effect                                    [log: ignored (Press mode)]
   then: held repeats / release kept, even after the session ends; release → fate None

fresh press of the OTHER hand while a hand is plugin-cast ─▶ passes (block / attack at once)
   └─ mayCancel → cancelRequested → OnFrame: ForceExit                            [log: other-hand press -> passes, cancel session]
```

## Session end paths added (domain, per frame)

```text
PumpAutomatic, before the cast phases:
  any hand plugin-cast && startFailSecs >= kMaxCastStartSecs → log → mergeForceExit(ForceExit()) → return
PumpCastPhase(hand): plugin-cast && phase == StartRequested            → startFailSecs += dt
                     phase == Casting && castingElapsedSecs >= 0.2 s     → startFailSecs = 0
OnCasterInterrupt, real interruption: FinishHand(hand [+ dual partner]) + ScheduleSpellFireFinalize(each)
  → PumpSpellFireFinalize (caster idle or 0.7 s) → TryFinalizeExit → restore
```
