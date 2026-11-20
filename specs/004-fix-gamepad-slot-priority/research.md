# Phase 0 Research: Fix gamepad slots 5+ losing input priority to the game's default actions

Evidence comes from the code on `fix/gamepad-slot-priority` (branched from `main` at `0de67cf`), the distributed
`SKSE/Plugins/IntegratedMagic.ini` and the player report in `.claude/tasks/bug-03-gamepad-slots-5-plus.md`.

---

## R1. Root cause: three mechanisms (confirmed in code)

The task note's hypothesis, "a list or loop capped at 4 slots", is **refuted**:
- The input stores are sized `kInputMaxSlots` (64).
- The loops use `SlotEdgeStore::ActiveSlots()`, which follows the configured slot count. `LoadHotkeyCache_FromConfig`
  stores it at every hotkey load.
- `GamepadIdToIndex` maps the 16 buttons to distinct indices 0–15.

**Default gamepad layout**, from the distributed INI:

| Slot | Buttons | Meaning |
|---|---|---|
| 1 | `12, 15` | X + RT |
| 2 | `13, 15` | Y + RT |
| 3 | `11, 15` | B + RT |
| 4 | `10, 15` | A + RT |
| HUD popup | `9, 8` | RB + LB |

- `[Modifier] GamepadPosition = 2`: `RT` is the modifier, and `MagicConfig::Load` copies it into position 2 of every
  slot.
- So **every slot shares `RT`**, and the four face buttons belong to slots 1–4.
- A slot 5+ combo shares `RT` with all of them and, in the natural "second layer" layout (`face + RT + LB`), also
  shares its face button with one of slots 1–4.

### M1. First owner wins (`ShouldFilterAndSave`, `src/Input/InputFilter.cpp`)

For every button event, the loop over slots **returns at the first slot whose combo contains the button**. Later
slots are never examined.
- If that slot is down or accepted, or its combo is fully held, the event is filtered. Correct.
- Otherwise the event is **held back for that slot** (`retained[s]`) and a 40 ms filter window opens
  (`filterWindowActive[s]`).
- A later slot whose combo the player is actually completing is never asked.

### M2. Held-back events are released by a timer, not by the outcome (`TickFilterWindows`, `src/Input/ExclusiveTracker.cpp`)

When slot `s`'s 40 ms window ends, every event in `retained[s]` is queued for replay to the game. Its replay state
then lets the copy pass `ShouldFilterAndSave` (`ReplayMatchesEvent`).
- This happens **even when another slot that contains the button has just activated**, or is still deciding in its
  own pending window.
- It even happens when slot `s` itself is still pending. Pending acceptance takes 40 ms after the **full** combo is
  seen, while the filter window counts from the **first** held-back button.
- **Consequence on the default layout**:
  - **Slot 5 = X + RT + LB**: `RT` is held back for slot 1 and `X` for slot 1 too (M1). Both are replayed after
    40 ms, so the game performs the right-hand attack/cast and X's default action, while slot 5 activates. The
    player sees the game action, and the cast may be interrupted.
  - **Slots 2–4**: `RT` leaks the same way (held back for slot 1). It is mostly invisible because it is the
    right-hand cast button.
  - **Any slot, face button pressed before `RT`**: the face button is held back for its own slot. If `RT` arrives
    10–40 ms later, the window expires before the pending acceptance, so the face button leaks (for example a jump
    on `Y`). That is an order-dependent leak (spec US1 scenarios 4–6).

**M2 confirmed in game by the user (2026-11-20)**, on default slot 2 `R2 + Triangle` (`RT + Y`):
- "R2 slightly before Triangle" casts. R2's attack leaks, but the spell equip overrides it.
- "Triangle slightly before R2" makes the character **jump**, and the spell cannot be used.

Timeline of the jump case, at 60 fps:

| t | Event | Effect |
|---|---|---|
| 0 ms | `Y` down; slot 2 combo incomplete | `Y` held back under slot 2; filter window ends at 40 ms |
| Δ ms (Δ < 40) | `RT` down; combo complete | slot 2 pending starts; `fullComboSeen` is set next frame, and acceptance follows about 40 ms later (≈ Δ + 56 ms) |
| 40 ms | `TickFilterWindows` | window ends before acceptance, so `Y` is replayed to the game: **jump** |
| ≈ Δ + 56 ms | slot 2 accepted | the slot activates, but the jump already started |

Only a same-frame press avoids it: `ComboDown` is then already true when `FilterEvents` sees `Y`, so nothing is held
back.

### M3. Overlapping combos are not resolved (`RecomputeSlotEdges` + `DispatchSlotEvents`)

Each slot's edge is computed on its own.
- When a combo contained in another (slot 1 `X + RT` inside slot 5 `X + RT + LB`) or an identical combo completes,
  several slots can rise.
- `DispatchSlotEvents` then handles them from the lowest index up, each through `OnSlotPressed`. The later one
  overwrites the earlier one.
- There is no "longest combo wins" rule, and identical combos have no defined winner (spec FR-009, Clarification
  2026-11-19).

**Most likely primary cause of the report: M2.**
- Slots 5+ normally use 3-button combos (`face + RT + LB`), which leave more time between the first and last button.
  So the first button's window runs out far more often than with the 2-button combos of slots 1–4.
- Players also tend to press `RT` first for slots 1–4, which makes the remaining `RT` leak invisible.
- M1 and M3 are real too, and they matter for the same 3-button layout.

The fix covers all three, and the validation matrix in `quickstart.md` exercises each one.

---

## R2. Fix M1: evaluate every candidate slot

**Decision**: `ShouldFilterAndSave` collects the **candidate slots** (every slot whose combo, on the event's device,
contains the button) and decides in phases:
0. **Release and held repeat**: these follow the **press fate** of the button (R11), not the slot state. They are
   consumed, passed or held back exactly as their press was.
1. **Filter (fresh press)**: if any candidate is down (`slotDown`) or has its combo fully held, filter the press and
   mark it **consumed**. `slotWasAccepted` is **not** used for fresh presses (R11, finding H3).
2. **Replay pass-through**: if the event matches any candidate's replay state, reset that slot's replay and let it
   pass, marked **passed**.
3. **Press-mode deactivation**: keep the existing `deactivatedThisPress` release handling, applied to the candidate
   that has the flag.
4. **Hold back**: otherwise hold the event back under one **owner**, marked **held back**. The owner is the candidate
   with the most of its combo currently held on that device. Ties go to the longest combo, then the lowest slot
   number.

**Rationale**: the outcome no longer depends on slot number. A candidate that is actually being completed always
gets the chance to filter. The owner choice only decides which window tracks the event, and R3 makes the final
outcome independent of the owner.

**Alternatives considered**:
- *Hold the event back under every candidate*: rejected. It would replay the same event several times.
- *Keep first-match and only fix the release (R3)*: rejected. Phase 1 is needed so a later slot's fully held combo
  filters its held repeats instead of re-holding them under slot 1 every frame.

---

## R3. Fix M2: held-back events are resolved by outcome

**Decision**: two rules.
- **(a) Consumed on activation**:
  - When a slot activates, every event held back under **any** slot whose button belongs to the activating slot's
    combo (same device) is **dropped**. The same goes for the deferred replay queue. "Activates" means a rising
    edge, or a pending press that resolves to `Success`.
  - It is logged as consumed. The game never sees it.
- **(b) Contested hold**:
  - When slot `s`'s filter window ends, the events are not replayed if any slot whose combo contains one of them is
    still **pending** (`pendingSrc != None`). The window is re-armed instead.
  - It is capped at **0.5 s** since the first held-back event (`kMaxContestedHoldSec`). After that the events are
    replayed as today.
  - When no candidate is pending, the existing replay happens, logged as handed to the game.
- **(c) Chord in progress** (Clarification 2026-11-20, resolves analysis findings H1/H2):
  - When the window ends and no candidate is pending, the events are still not replayed if some candidate's combo has
    **two or more** of its buttons held but is **not complete**.
  - This is capped at **100 ms** since the first held-back event (`kMaxChordHoldSec` = 2 × `kComboToleranceSec`;
    0.15 s before the 2026-11-20 tuning). After that they are replayed, as a slow chord.
  - The same rule covers a combo **completed in the frame the window ends** (`complete`): its slot only goes pending
    in pass 1 of `RecomputeSlotEdges`, after `TickFilterWindows`, so without it the first button would leak when the
    second one arrives in that frame. With a 50 ms window (about 3 frames at 60 fps) that frame still matters.
  - The cap counts the frame that crosses it (`heldBackSecs − dt < kMaxChordHoldSec`): input seen in this frame
    arrived during the last `dt`.
  - A button pressed alone never has a second combo button held, so it is still released at 50 ms (FR-003).
  - Example, slot 6 `Y + RT + LB` pressed `Y`, `RT` (within 50 ms), then `LB` (at 90 ms): at 50 ms two of slot 6's
    buttons are held, so `Y` stays held back. `LB` completes the combo, slot 6 is accepted, and `Y` and `RT` are
    consumed.
- **(d) Cancelled pending press** (`ClearExclusivePending` with a non-`Success` result): the held-back events are no
  longer replayed immediately. The window is expired, so (b) and (c) decide on the next frame. This costs about one
  frame (~16 ms) in that case only, and is accepted (finding L3).

**Rationale**:
- (a) makes FR-001 and FR-002 hold whichever slot owns the event.
- (b) closes the timing gap between the filter window (counted from the first button) and pending acceptance
  (counted from the full combo). It also fixes the order-dependent leak.
- Single buttons pressed with no combo in progress still reach the game after the filter window, 50 ms since the
  2026-11-20 tuning (FR-003). (b) only waits while a combo containing the button is actually pending.
- The 0.5 s cap keeps a never-resolving pending state from swallowing a button.

**Coverage limit (chord tolerance)**:
- (b) protects a held-back button only if a slot containing it is **already pending** when its window ends.
- Pending starts once the full combo is held. (c) adds the chord-in-progress case.
- **Guaranteed**: the second button within 50 ms of the first, and a 3-button combo within 100 ms. That covers a
  normal quick chord, the user's "Triangle slightly before R2", and a 3-button chord.
- **Not covered**: a second button later than 50 ms. At that moment only one button is held, which cannot be told
  apart from a single press. Waiting longer would delay every single press (FR-003). Such a chord behaves as today
  (spec Edge Cases "Slow chord").
- **Tuning (Clarification 2026-11-20, user, after the in-game runs)**: the combo tolerance is **50 ms** for combos of
  2+ buttons (`kComboToleranceSec`, replacing `kFilterReplayDelaySec` = 40 ms). It is both the filter window and the
  wait before a multi-button slot activates once its full combo is held, so a longer combo whose extra button comes
  within 50 ms still wins (R4). Single-key slots keep 40 ms (`kSingleKeyAcceptDelaySec`). All are build-time
  constants, with no INI change.
  - It was first set to 25 ms. In the second run a Triangle → R2 gap of 100 ms still made the character jump (a slow
    chord), so the user settled on 50 ms: it covers more of those presses, and the delay on single presses is still
    not noticeable. Covering a 100 ms gap would need about 110 ms, a visible delay on every jump and attack.

**Alternatives considered**:
- *Lengthen the filter window*: rejected for this spec. It would delay every single-button game action (FR-003). It
  remains the lever if a larger chord tolerance is wanted.
- *Re-attribute events to the pending slot*: rejected. Same effect as (b) with more moving parts.

---

## R4. Fix M3: same-frame overlap resolution (longest combo wins)

**Decision**: `RecomputeSlotEdges` computes every slot's `accNow` first and resolves conflicts before applying edges.
- **Rule**: a slot `A` that rises this frame **loses** if there is a slot `B` on the same device whose combo
  strictly contains `A`'s combo, and `B` is also rising this frame or is pending (`pendingSrc != None`).
- **Identical combos rising together**: the higher slot number loses.
- **The loser**: its `accNow` becomes `false`, its pending state is discarded, and it is flagged
  `suppressedUntilRelease`. While the flag is set and the loser's combo is still held, it does not activate. The flag
  clears when the combo is released.
- The winner's activation consumes the loser's held-back buttons (R3a), because they are all in the winner's combo.
- **Log**: `[Input] RecomputeSlotEdges: slot=<a> SUPPRESSED by slot=<b> (longer|identical combo)`.

**Rationale**:
- It implements Clarification 2026-11-19, "longest combo completed at that moment wins, no added wait".
- "That moment" is the existing pending window, so multi-button combos that complete together resolve correctly
  without adding delay.
- A subset combo that completed **earlier** (before the extra button was pressed) has already fired and stays fired,
  as the spec allows.
- **Accepted edge (finding M2)**: if the winner was only *pending* and its pending press is then cancelled, for
  example `LB` released before acceptance, neither slot fires from that press. The loser stays suppressed until its
  buttons are released. Restoring the loser would need rewinding its acceptance, which is more state for a rare case.

**Edge (documented, unchanged)**: a single-key slot with Require exclusive hotkey OFF has no pending window. It fires
on the key press, so a longer combo containing that key can only win if its extra buttons were already held.

**Alternatives considered**:
- *Wait to see whether a longer combo completes*: rejected by the clarification (no added wait).
- *Let `DispatchSlotEvents` keep only one pressed slot per frame*: rejected. That changes the application layer, and
  it would not know about combos.

---

## R5. Identical-combo warning (FR-009)

**Decision**: `LoadHotkeyCache_FromConfig` normalizes each slot's keyboard and gamepad combo (drops `-1` and
sorts). It compares them pairwise among active slots, which is at most 2016 pairs and only at hotkey load.
- For each identical non-empty pair it logs: `[Input] HotkeyCache: slots <a> and <b> have the same <kb|gp> combo;
  slot <a> fires`.
- The runtime outcome comes from R4: the higher-numbered slot is suppressed.

---

## R6. Diagnostics (FR-007, Constitution VI)

**Decision**: these `[Input]` lines are written only for press and release events, never for per-frame held repeats:

| Event | Line |
|---|---|
| Held back | `[Input] HoldBack: <label> held for slot=<owner> (candidates=<n>)` |
| Consumed | `[Input] HoldBack: <label> consumed by slot=<winner>` |
| Released | `[Input] HoldBack: <label> handed to game (slot=<owner>)` |
| Window extended | `[Input] HoldBack: slot=<s> window extended (slot=<t> pending\|chord)`, logged when the window is first extended. `pending` is R3b and `chord` is R3c |
| Overlap | `[Input] RecomputeSlotEdges: slot=<a> SUPPRESSED by slot=<b> (...)` (R4) |

`<label>` reuses the spec 003 format: `kb:0x..`, `mouse:<id>` or `gp:<n>`.

---

## R7. Keyboard and mouse (FR-005)

The same functions handle every device. The effective code is the scancode for keyboard, `256 + id` for mouse, and
`GamepadIdToIndex` for gamepad. No device-specific branch is needed beyond picking the combo array (`kb` or `gp`).

---

## R8. Patches (FR-006)

- **Require exclusive hotkey**: it still uses `FindExtraHeldCode` on the slot's own combo. A button shared with
  other slots is part of the slot's own combo, so it is never an "extra" for that slot. For R4, a loser refused by
  the exclusive rule never rises, so there is nothing to resolve.
- **Press both at the same time**: its timing rule runs inside `ComputeAcceptedExclusive`, unchanged.
- No INI or setting changes.

---

## R9. In-game validation build (Constitution VI)

The DEBUG DLL still fails to load (specs 002 and 003). The plan records the same deviation, release DLL plus the
`#define DEBUG` debt, in Complexity Tracking. **Approved by the user for this spec on 2026-11-20.** Build order: debug
first, release last. Then delete the debug PDB from MO2.

---

## R10. Threading and invariants

- Every change runs inside `PollInputDevicesHook` → `InputController::ProcessAndFilter` on the main thread
  (Constitution II).
- New state lives in `ExclusiveStore` (input layer):
  - per slot: `suppressedUntilRelease`, `heldBackSecs` (the R3b/R3c caps) and `contestedLogged`,
  - per input code: `kbFate` / `gpFate` and `kbOwner` / `gpOwner` (R11).
- There are no `MagicState`, `Application/` or `Domain/` changes.

---

## R11. Press fate: a release follows its own press (FR-010, resolves analysis finding H3)

**Finding**: `RecomputeSlotEdges` keeps `slotWasAccepted[s]` **true for the whole session** of the active slot,
even after its combo is released (`if (!spellSystemActive || activeSlot != slot)` clears it only otherwise).
- Today only the *first* candidate checks the flag, so `RT` is blocked only during sessions of slot 1. That is
  already a hidden bug for slot 1 in Press mode without auto-attack.
- If R2 checked `slotWasAccepted` for **every** candidate, any active session whose combo contains `RT` would block
  every new `RT` press. The player could no longer cast manually with `RT` during a Press session on any slot.

**Decision**: per-code **press fate**, `kbFate[kMaxCode]` / `gpFate[kMaxCode]`, with values `None`, `HeldBack`,
`Consumed` or `Passed`. For `HeldBack`, the owner slot is kept in `kbOwner` / `gpOwner`.

| Event | Rule |
|---|---|
| **Fresh press** (`value > 0 && heldSecs == 0`) | Decided by R2 phases 1–4. Sets `Consumed`, `Passed` or `HeldBack` |
| **Held repeat** (`value > 0 && heldSecs > 0`) | Follows the fate. `Consumed` → filtered. `Passed` or `None` → passes. `HeldBack` → filtered **and not stored**. Replay drains one event per frame, so stored repeats could reach the game after the live release and leave the button stuck. Once the press is handed over, live repeats pass |
| **Release** (`value <= 0`) | Same as a held repeat, then the fate returns to `None` (for `HeldBack`, once resolved) |

**Resolution** sets the fate of every held-back code:
- **Consumed** (R3a), or a `Success` / discard in `ClearExclusivePending` / `DiscardExclusivePending` → `Consumed`.
- **Handed to the game** (R3b/R3c release) → `Passed`.
- **In both cases**, if the held-back events of that code already include its release, the fate becomes `None`, since
  the press is over. No key-state read is needed.
- **Resets** (`ResetExclusiveState`, `ClearEdgeStateOnly`, menu close): every fate becomes `None`.

**Rationale**: a press that the game never saw must not let its release through alone, and a press the game saw must
get its release, or the game would see an attack held down forever. Tying held repeats and releases to their own
press gives both, and new presses are decided on their own merits. `slotWasAccepted` keeps its role in
`RecomputeSlotEdges` (edge semantics), unchanged.

**Alternatives considered**:
- *Check `slotWasAccepted` only for the first candidate (today)*: rejected. It is order-dependent, which is the bug
  class being fixed.
- *Check `slotWasAccepted` only for releases and held repeats*: rejected. A release whose press reached the game
  would be filtered, so the game would never see `RT` released.
