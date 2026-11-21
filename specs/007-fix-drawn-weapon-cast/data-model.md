# Data Model: Fix slot casts failing with a weapon or shield in the left hand after drawing weapons

No setting, INI key or persisted data changes (spec FR-010). Everything below is runtime state of one session. Each
item is added only if research R1 confirms the cause it belongs to. **Outcome (T009, R11)**: only D6 shipped.

## Always added

| Item | Where | What |
|---|---|---|
| `DescribeHandsForLog(player)` | `include/Shared/HandDiagnostics.h` | Read-only helper. It returns one log line describing the weapon state and both hands (R2). No state |

## Shipped: D6, the equip-animation pause of the retry bound (R11)

### `HandMode` (`include/Domain/State.h`), per hand

| Field | Type | Default | Meaning |
|---|---|---|---|
| `equipAnimGraceSecs` | `float` | `0.f` | Time of the current start attempt during which the game played its equip/draw animation. That time does not count toward `startFailSecs` |
| `equipAnimGraceActive` | `bool` | `false` | The pause is running now. It is used for the paused/resumes log lines |

### `MagicState` constant

| Constant | Value | Meaning |
|---|---|---|
| `kMaxEquipAnimGraceSecs` | `2.0f` | At most this much animation time is kept out of the bound. After it, the bound counts again (Constitution III) |

### Read helper

`IsEquipAnimationPlaying(player)` (`include/Shared/HandDiagnostics.h`): the graph variable `IsEquipping`, false when
it is missing. Read-only.

### Rules

- **When the pause applies**: in `PumpCastPhase`, for a plugin-cast hand in `StartRequested`, when
  `!_session.wasHandsDown`, `equipAnimGraceSecs < kMaxEquipAnimGraceSecs` and `IsEquipAnimationPlaying`. In that case
  `dt` goes to `equipAnimGraceSecs`. Otherwise it goes to `startFailSecs`, as before.
- **Resets** (Constitution II): both fields are cleared together with `startFailSecs`:
  - in `RequestAutoAttackStart`,
  - in `FinishHand`,
  - when a cast lasts `kSpuriousInterruptWindowSecs`,
  - in every `HandMode` value reset.
- **Bound**: an attempt can last at most 2.0 s of animation plus 1.0 s without one, then `ForceExit`. That is well
  below the 30 s timeout.

## Designed, not built: D1 (R3), superseded by D6 (R11)

### `HandMode` (`include/Domain/State.h`), per hand

| Field | Type | Default | Meaning |
|---|---|---|---|
| `waitingHandSwitch` | `bool` | `false` | The hand had a weapon, shield or torch, the activation started drawn, and the plugin is waiting for the game to show the hand as magic before its first press |
| `handSwitchWaitSecs` | `float` | `0.f` | Time spent in that wait |

### `MagicState` constant

| Constant | Value | Meaning |
|---|---|---|
| `kMaxHandSwitchWaitSecs` | `0.5f` | After this, the wait ends and the press goes out anyway (Constitution III) |

### Reset rules (Constitution II)

- Both fields are part of `HandMode`. Every value reset of a hand clears them: `EnterHand` (`hm = {}`), `_left = {}` /
  `_right = {}`, `DisableHand`, `PrepareForOverwriteToSlot`, `ResetHandStates`.
- `FinishHand` also clears them, so a hand that ends during the wait (Hold released, block, menu) cannot carry the
  wait into the next session.
- `RequestAutoAttackStart` runs when the wait ends, and resets `startFailSecs`, so the 1 s bound starts after the wait.

### Per-hand cast start (D1)

```text
                    activation drawn, hand item not a spell
EnterHand ─────────────────────────────────────────────► WaitingHandSwitch (no press yet)
   │                                                          │
   │ otherwise                                                │ hand shows magic, or 0.5 s
   ▼                                                          ▼
StartRequested ◄───────────────── RequestAutoAttackStart → first press (startAttack)
   │
   ▼ (unchanged: Casting → WaitingChargeRelease → Done; retries and the 1 s bound as today)
```

- **Dual cast**: both hands leave `WaitingHandSwitch` in the same frame (Constitution IV).
- **Any exit during the wait** (Hold released, block, stagger, menu, ForceExit, timeout): the hand finishes as today.
  No press was sent, so no release is owed. A hand that the plugin did press is released on every path, as today.

## Designed, not built: D4 (R6), H4 not seen

`Application::SpellSystemController::ActiveSlotContents` gains `snapLeftObj` and `snapRightObj` (`RE::FormID`). They
are the session's snapshot hand items, which `MagicEquipSink` never treats as foreign. Read-only. They are cleared
with the session.

## Not added

- **D3** (R5) and **D5** (R7) reuse the existing phases: a hand is in its start phase when `waitingHandSwitch` is set
  or `autoCastPhase == StartRequested`. They add no state.
- **D2** (R4): the unqueued restore equip is a call parameter, not state.
