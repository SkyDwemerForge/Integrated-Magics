# Data Model: Fix gamepad slots 5+ losing input priority to the game's default actions

No persisted data changes. The INI keys, hotkey encodings and default bindings stay as they are. This model covers
the in-memory input state that the fix adds or constrains.

## Constants (`include/Shared/InputConstants.h`)

| Name | Value | Meaning |
|---|---|---|
| `kComboToleranceSec` | `0.05` | Combo tolerance for combos of 2+ buttons (Clarification 2026-11-20, user). The filter window: how long a held-back button waits before going to the game, when nothing contests it. Also the wait before a multi-button slot activates once its full combo is held. Replaces `kFilterReplayDelaySec` (`0.04`) |
| `kSingleKeyAcceptDelaySec` | `0.04` | Wait before a single-key slot activates while held, with Require exclusive hotkey on (the old `kFilterReplayDelaySec` value, unchanged for single keys) |
| `kMaxContestedHoldSec` | `0.5` | Upper bound on holding a button back while a slot containing it is still pending (research R3b) |
| `kMaxChordHoldSec` | `2 × kComboToleranceSec` = `0.1` | Upper bound on holding a button back while some candidate combo has 2 or more of its buttons held: a chord in progress, or a combo completed this frame (research R3c). Two gaps, as combos have at most 3 buttons. It counts the frame that crosses it (`heldBackSecs − dt < cap`) |

## ExclusiveStore (existing, `include/Input/ExclusiveStore.h`), new fields

| Field | Type | Meaning |
|---|---|---|
| `suppressedUntilRelease` | `std::array<bool, kInputMaxSlots>` | The slot lost an overlap resolution (research R4). While set and its combo is still held, it cannot activate. Cleared when the combo is no longer held |
| `heldBackSecs` | `std::array<float, kInputMaxSlots>` | Time since the slot's current filter window opened. It drives the `kMaxContestedHoldSec` and `kMaxChordHoldSec` caps. Reset when the window closes |
| `contestedLogged` | `std::array<bool, kInputMaxSlots>` | The "window extended" line was already logged for the current window. Reset when the window closes |
| `kbFate`, `gpFate` | `std::array<PressFate, kMaxCode>` | Fate of the current press of each keyboard/mouse code and each gamepad index (research R11). `enum class PressFate : std::uint8_t { None, HeldBack, Consumed, Passed }` |
| `kbOwner`, `gpOwner` | `std::array<int, kMaxCode>` | For `HeldBack` codes: the slot whose `retained` list holds the press, so held repeats and the release join the same list. `-1` otherwise |

### Validation and reset rules

- Every new field is reset by `ResetExclusiveState` and `ClearEdgeStateOnly`, as the existing fields are. Fates go
  to `None` and owners to `-1`.
- `heldBackSecs[s]` and `contestedLogged[s]` are reset whenever `filterWindowActive[s]` goes to `false`.
- `suppressedUntilRelease[s]` is only set by the overlap resolution in `RecomputeSlotEdges`. It is only cleared there
  once the slot's raw combo is no longer held, or by the resets above.
- **Fate transitions** (research R11):
  - **Fresh press**: R2 phase 1 sets `Consumed`, phase 2 `Passed`, phase 4 `HeldBack` with its owner.
  - **Held-back events resolved**:
    - consumed (R3a, `ClearExclusivePending` `Success`, `DiscardExclusivePending`) → `Consumed`,
    - handed to the game (R3b/R3c release) → `Passed`,
    - if that code's held-back events already include its release → `None`.
  - **Release** of a `Consumed` or `Passed` code: the fate returns to `None` after the release is filtered or passed.

## Pure helpers (`include/Input/HotkeyMatcher.h`, no `RE::` types)

| Helper | Meaning |
|---|---|
| `ComboKeyCount(combo)` | Number of bound keys (`!= -1`) |
| `ComboHeldCount(combo, down)` | Number of the combo's keys currently held |
| `ComboStrictSubset(a, b)` | Every bound key of `a` is in `b`, and `b` has more bound keys |
| `ComboSameKeys(a, b)` | Same set of bound keys, ignoring order; both non-empty |

## Effective code of an input event

`EffectiveComboCode(dev, rawIdCode)`, declared in `include/Input/InputFilter.h`, maps an event to the code that combos
store:

| Device | Effective code |
|---|---|
| Keyboard | scancode |
| Mouse | `kMouseButtonBase + id` (the wheel is already excluded, spec 003) |
| Gamepad | `GamepadIdToIndex(rawIdCode)` (0–15) |

It is used to compare held-back events (`RetainedEvent`, which stores the raw id) with combos.

## Held-back button lifecycle (spec Key Entity "Held-back button")

```text
fresh press not completing any candidate combo (fate = HeldBack)
   └─▶ held back under owner slot s (retained[s]); filter window 50 ms opens   [log: held for slot=s]
        │   (while fate = HeldBack: its release joins retained[s]; its held repeats are filtered, not stored)
        ├─ any slot whose combo contains it activates ───────────────▶ CONSUMED (dropped), fate Consumed  [log: consumed by slot=w]
        ├─ window ends, a slot containing it is pending (≤ 0.5 s) ───▶ window re-armed  [log: window extended (pending), once]
        ├─ window ends, 2+ of a combo held (≤ 100 ms) ────────────────▶ window re-armed  [log: window extended (chord|complete), once]
        └─ window ends, none of the above (or a cap reached) ────────▶ HANDED TO GAME (replay), fate Passed  [log: handed to game]

then: held repeats / release of a Consumed code → filtered; of a Passed code → reach the game; release → fate None
```

## Slot activation with overlap resolution (per frame, `RecomputeSlotEdges`)

```text
1. For each slot: kbNow/gpNow/rawNow, prevAcc.
   - suppressedUntilRelease && rawNow  → accNow = false (skip acceptance logic).
   - suppressedUntilRelease && !rawNow → clear the flag, then compute normally.
   - Otherwise                         → accNow from ComputeAcceptedExclusive / the single-key rule.
2. Among slots rising this frame (accNow && !prevAcc):
     A loses if some slot B (same device) has ComboStrictSubset(A, B) and B is rising or pending;
     or ComboSameKeys(A, B), B rising, and B < A.
     loser: accNow = false, DiscardExclusivePending(A), suppressedUntilRelease[A] = true   [log: SUPPRESSED by slot=B]
3. Apply edges (pressed/released masks).
   For each slot that rose: consume held-back events whose code is in its combo (all slots + deferred queue).
```
