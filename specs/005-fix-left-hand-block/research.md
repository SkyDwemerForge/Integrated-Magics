# Research: Fix left-hand casting stuck by the block button

All findings come from the code on `fix/left-hand-block` (`main` at `c001181`, spec 004 included) and from the
user's in-game tests of 2026-11-20. File references are for the plan and tasks; the spec stays at player level.

## R1. Mechanism

### Facts (code)

- **The plugin's cast input is the player's button.**
  - `FireAttackDirect` (`src/Adapters/Outbound/SyntheticInput.cpp`) builds a `ButtonEvent` with the user event
    `"Left Attack/Block"` / `"Right Attack/Block"` and mouse id 1 / 0, and hands it straight to
    `PlayerControls::attackBlockHandler->ProcessButton`.
  - The right mouse button produces exactly that event, mouse id 1 `"Left Attack/Block"`. Gamepad `LT` produces the
    same user event from another device. The handler cannot tell them apart.
- **Nothing filters or tracks the player's attack/block buttons.** `FilterEvents` (`src/Input/InputFilter.cpp`) only
  filters combo buttons (spec 004) and the HUD toggle.
- **Order inside one frame** (`PollInputDevicesHook::thunk`):
  1. `InputController::ProcessAndFilter`: key states, slot edges, and filtering of the player's events.
  2. `SpellSystemController::OnFrame`: slot presses, equip, and synthetic attack intents (`DispatchAttack` queues
     them).
  3. `FlushSyntheticInput`: the queued attack events go **directly** to `attackBlockHandler`, in queue order.
  4. The game dispatches the player's events that passed the filter.

  So in every frame the handler gets the plugin's events for an action first, then the player's.
- **The retry loop has no total bound.**
  - In `StartRequested`, `PumpCastPhase` releases and re-presses every 0.15 s (`kRedispatchInterval`).
  - `OnCasterInterrupt` does the same after a pre-cast interruption, and after a "post-cast spurious" one (cast
    shorter than 0.2 s).
  - Every restart resets `startRequestSecs`. Only the 30 s session timeout ends the loop.
- **An interrupted cast leaves the session open.**
  - A non-spurious interruption reaches the last branch of `OnCasterInterrupt` (`src/Domain/MagicStatePump.cpp:1028`):
    `FinishHand(hand)` only.
  - `SpellSystemController::OnCastInterrupted` then dispatches the release, but nothing calls `TryFinalizeExit` or
    `ScheduleSpellFireFinalize`.
  - The session stays active with a finished hand. In Automatic it lasts until the 30 s timeout (`activeTimeoutSecs`).
    In Hold it lasts until `OnSlotReleased`.
- **Block during a session**: `NotifyAnimEvent` calls `ForceExit` on `blockStart` or `BashExit` unless
  `IsPressMode()`. With a spell in the left hand, the button casts instead of blocking, so `blockStart` never comes
  from it.

### Facts (user's tests, 2026-11-20; right-hand spell, shield in the left hand)

- **Raising the shield during a Hold or Automatic slot does not end it.** Automatic stays active "indefinitely", and
  Hold stays active until the hotkey is released, with no more casting.
  - The lasting part matches the interrupted-cast fact above.
  - The `blockStart` rule did not end the session either, for a cause not yet known: the event missing, renamed by a
    behavior mod, or the block not starting at all while the plugin holds the cast.
  - So the cancel must not rely on that event alone (R6).
- **The slot cannot be activated while blocking.**
  - With Require exclusive hotkey ON, the held block button is an extra key: `REFUSED (not exclusive)
    extra=mouse:1`. This is unchanged (R13).
  - With the patch OFF, the game does not cast the right hand while the shield is up, so the block must be lowered at
    the plugin's first press (R5).
- **A left-hand spell slot cannot block, as in vanilla.** Only mods that add their own block key change that. Such a
  block interrupts the cast, which R12 handles.

### Hypothesis for the reported twitching (to confirm with the baseline log, quickstart §0)

| Sequence | What the handler receives | Expected effect |
|---|---|---|
| Block held, then slot activated | The player's held repeats (value 1, `heldSecs` growing) around the plugin's down/up | The game stays in block or in a held state; the cast never reaches `kUnk02`; the retry loop twitches |
| Hotkey held, then block pressed | A player down/held/up in the middle of the plugin's hold | The cast is released or restarted by the player's events; the retry loop twitches |
| Session ends while block held | The plugin's up, then the player's held repeats with no down | The game's view and the button disagree, and the next session starts from that state |

The design does not depend on that detail:
- the gate ends the mixing (R3, R4),
- the takeover brings the game's view back in step at the plugin's first press (R5),
- the bound stops the loop (R7),
- R12 closes interrupted sessions.

**Decision**: treat the bug as a coordination defect between two sources of the same game actions. Add two safety nets:
the retry bound, and closing interrupted sessions.

**Alternatives considered**:
- *Clear the game's block state directly* (behavior graph variables, the actor's block flag). Rejected: it does not
  stop the mixing, and it adds direct game mutations.
- *Only bound the retry loop*. Rejected: the stuck state and the open sessions would remain.

## R2. Identifying the player's attack/block events

**Decision**: identify them by **user event** (`ButtonEvent::QUserEvent()` equal to `"Left Attack/Block"` or
`"Right Attack/Block"`), compared with cached `RE::BSFixedString` values (pointer comparison).

**Rationale**:
- One rule covers the mouse, gamepad triggers, keyboard binds, and control-map remaps.
- It is the identity the game uses to route the event to `attackBlockHandler`.
- `ProcessButtonEvents` already relies on `QUserEvent()` (the `"Shout"` check), so the user event is set at this hook
  point.

**Alternatives considered**: device codes (mouse 0/1, gamepad `LT`/`RT`). Rejected: they miss remaps and keyboard binds.

## R3. Where the gate runs

**Decision**: a **hand-button gate** runs on every event of a hand action that would reach the game:
1. in `FilterEvents`, **after** `ShouldFilterAndSave` and the HUD toggle filter returned "pass",
2. on replayed events, before `InputController` injects them (`DrainOneDeferredReplayEvent` → `EnqueueRetainedEvent`).
   Replays bypass `FilterEvents`.

**Rationale**:
- Combo buttons stay under spec 004's rules. On the default gamepad layout `RT` is every slot's modifier. Its combo
  presses are consumed there and never reach the gate, so a combo press (for example `RT + X` to start another slot
  during an Automatic session) never cancels a session.
- The gate sees exactly what the game would see.

**Alternatives considered**:
- *Gate before `ShouldFilterAndSave`*. Rejected: combo presses would cancel sessions, and spec 004's press fates would
  miss presses.
- *Decide in the domain*. Rejected: the domain never sees input events (Constitution I).

## R4. Gate rules

**Definitions**:
- A hand is **plugin-cast** when the session is active and `!finished && (autoActive || (holdActive &&
  wantAutoAttack))`, the condition `RequestAutoAttackStart` and `OnCasterInterrupt` use. Press with auto-attack is
  `autoActive && pressActive` until its first cast ends ("press-autocast done").
- A press **may cancel** when at least one hand is plugin-cast and `!IsPressMode()`, the same rule as `blockStart`.

| Event of a hand action | State | Result |
|---|---|---|
| Fresh press (value > 0, `heldSecs` = 0), **that hand** plugin-cast | may cancel | **Kept**, fate `Consumed`, **cancel requested** (Q1) |
| Fresh press, that hand plugin-cast | Press mode | **Kept**, fate `Consumed`, no cancel (Q1) |
| Fresh press, **the other hand** (not plugin-cast), another hand plugin-cast | may cancel | **Passes** (block/attack at once), **cancel requested** (Q3) |
| Fresh press, the other hand | Press mode | **Passes**, no cancel (Q3). If it interrupts a cast in progress, R12 still ends the session |
| Held repeat or release | fate `Consumed` | **Kept**; a release clears the fate |
| Held repeat or release, no fate, that hand plugin-cast | the game sees it held (a press from before, not yet taken by R5) | **Passes**: it completes the player's own press |
| Held repeat or release, no fate, that hand plugin-cast | the game does not see it held | **Kept** (an orphan event) |
| Any other event | no hand plugin-cast, nothing kept | **Passes** (vanilla) |

- **A fresh press always starts a new fate.** If a release was missed, for example inside a menu, a stale `Consumed`
  cannot swallow the next press.
- **The game's view** (`gameHeld`) is updated only from events that reach the game (passed events and injected
  replays). A press consumed by spec 004 (`RT` as a modifier) never makes it "held".
- **Reset**: `ResetInputState` (game load) clears the store. The menu-close transition (`m_prevBlocked` → false) clears
  `gameHeld` and the fates.

## R5. Bringing the game's view back in step at the plugin's first press

**Decision**: every synthetic **press** for a hand (value 1, `heldSecs` 0) goes through one Application helper. For
**each hand** whose action the game sees held by the player (`gameHeld` and no `Consumed` fate):
1. the helper first dispatches a synthetic **release** of that action (value 0, the last seen `heldSecs`),
2. it marks that press `Consumed`, so its held repeats and release are kept from the game until the button is
   released,
3. it logs the takeover.

Both hands are checked because the block is the left action even when the plugin casts the right hand: a shield held
up keeps the game from casting the right hand (user's test). The helper is used at both press sites in
`SpellSystemController`: the equip dispatch (`OnEquipComplete` → `dispatchLeft/Right`) and the restarts
(`startLeftAttack` / `startRightAttack`).

**Rationale**:
- The first press is the moment the sources start to mix. It covers a fresh session, an overwrite to another slot, and
  a restart.
- The releases and the press share the `DispatchAttack` queue, so the handler receives them in order, in the same
  frame.
- **Dual cast (Constitution IV)**: the takeover adds releases only, before both presses. Both hands still go down in
  the same frame.

**Alternatives considered**:
- *Take the button at `OnSlotPressed`*. Rejected: it would not cover restarts or a press that is delayed after equip.
- *Wait for `blockStop` before the first press*. Rejected: it adds a wait state on a game event (Constitution III).
  The retries and the R7 bound already cover a late start.

## R6. The cancel path

**Decision**:
- The gate only records a cancel request (`HandButtonStore::cancelRequested`).
- `SpellSystemController::OnFrame` consumes it **first, before `DispatchSlotEvents`**, and runs `if
  (!state.IsPressMode()) HandleForceExitResult(state.ForceExit())`.
- For the other hand's press (Q3), the press passes in the same frame. In that frame the handler receives the
  plugin's releases (step 3 of R1), then the player's press (step 4). So the block starts right after the slot ends.

**Rationale**:
- Decisions and effects stay in their layers (Constitution I).
- It does not depend on the `blockStart` event, which did not end the session in the user's test.
- The `blockStart` rule is kept as well, for blocks that come from a mod's own key.

**Pre-existing gap fixed here** (user approved, Clarifications):
- `ProcessButtonEvents` sets `forceExit` for a shout with a transform power equipped. `InputController` stores
  `MagicState::ForceExitNoRestore()` in `m_pendingForceExit`.
- But `InputController::ConsumeForceExit` and `SpellSystemController::ConsumeForceExitResult` have no caller, so that
  exit's stop intents and finalize never run.
- `OnFrame` consumes it next to the cancel request.

**Alternatives considered**: call `ForceExit` from `InputController` directly. Rejected: `ForceExit` returns intents
that only `SpellSystemController` executes.

## R7. Retry bound

**Decision**:
- **New field** `HandMode::startFailSecs`: the time the current attempt has spent without a stable cast.
- **It accumulates** in `PumpCastPhase` while the hand is plugin-cast and in `StartRequested`, including the frames
  of a deferred restart. Restarts, `NotifyAttackEnabled` and the dual-cast sync do **not** reset it.
- **It is reset by**:
  - `RequestAutoAttackStart`,
  - `FinishHand`,
  - every value reset of `HandMode`,
  - a cast that stays in `Casting` for `kSpuriousInterruptWindowSecs` (0.2 s).
- **The spurious window becomes a constant**: the local `kSpuriousInterruptWindow` in `OnCasterInterrupt` moves to
  `MagicState::kSpuriousInterruptWindowSecs`.
- **Bound** `MagicState::kMaxCastStartSecs = 1.0f` (Q2).
- **Check**: `PumpAutomatic` checks the bound **before** pumping the cast phases. When a plugin-cast hand reaches it,
  it logs and runs `mergeForceExit(ForceExit()); return result;`, like `ShouldForceInterrupt` and the 30 s timeout.

**Rationale**:
- `ForceExit` releases every synthetic input and builds the restore plan (Constitution III).
- In dual cast it ends both hands together (Constitution IV).

**Alternatives considered**:
- *Finish only the failing hand*. Rejected: the answer to Q2 is that the session ends.
- *Count restarts*. Rejected: restarts come at different rates. Time is what the player sees.

## R8. Diagnostics (FR-009, Constitution VI)

`MAGIC_DEBUG_LOG` lines, never for per-frame held repeats:

```text
[Input] HandGate: <Left|Right> press during plugin cast -> cancel session
[Input] HandGate: <Left|Right> press during plugin cast -> ignored (Press mode)
[Input] HandGate: <Left|Right> other-hand press -> passes, cancel session
[Input] HandGate: <Left|Right> button taken at cast start -> released in game (held=<s>s)
[Input] HandGate: <Left|Right> button released -> gate cleared
[SpellSystem] HandGate cancel -> ForceExit
[FLOW] PumpAutomatic: hand=<L|R> cast did not start in <s>s -> ForceExit
[State] OnCasterInterrupt: hand=<Left|Right> real interrupt -> finish + finalize session
```

## R9. Constitution V and VI

- **V (no patch flag)**: this is a fix. It restores what auto-cast is meant to do, and a block now ends a Hold or
  Automatic slot as the existing `blockStart` rule already intends. With a flag OFF the stuck and open sessions would
  remain, and no user wants that. Require exclusive hotkey keeps its meaning (R13). The same reasoning was accepted
  for spec 004.
  - **One part is a real native-flow change**: the other hand's press cancelling Hold and Automatic slots (Q3)
    includes a left-hand weapon attack with dual-wielded weapons, which today does not end a slot. It ships with no
    flag, by the user's decision. The justification and the rejected alternatives (a patch flag; cancelling only with a
    shield or an empty hand) are in plan Complexity Tracking.
- **VI (validation on a DEBUG build)**: the DEBUG DLL still fails to load (specs 002–004). Validation on the release
  DLL (with `#define DEBUG` in `PCH.h`, so the logs are written) was approved by the user on 2026-11-20. See plan
  Complexity Tracking.

## R10. TEST_MATRIX impact

- **New cases 8.9–8.19** (section 8, interruptions):
  - block held before a left-hand slot (shield; one-handed weapon) and before a right-hand slot, with Require
    exclusive hotkey OFF; refused with it ON,
  - block pressed or clicked during left-hand Hold, Automatic and Press-with-auto-attack casts,
  - block pressed during right-hand Hold, Automatic and Press slots with a shield,
  - the same with gamepad `LT`,
  - dual cast with block,
  - a session ending while block is held,
  - no stuck state,
  - interrupted casts never leaving the slot active,
  - activation mid-jump (the 1 s bound),
  - vanilla block, bash and manual cast unaffected.
- **Regression**:
  - 1.1–1.6 (Constitution IV),
  - 2.x and 4.1–4.3, because closing interrupted sessions touches every mode,
  - 6.1–6.2 (Require exclusive hotkey),
  - 8.1,
  - spec 004 6.15–6.23 (the gate must not touch combo presses).

## R11. Out of scope

- **The second report** (Automatic, left hand: equip → cast → unequip with nothing cast): verified by quickstart §6. If
  it remains, it gets its own spec.
- **Resuming a block automatically** after a session for a button the plugin took (spec Assumptions).
- **Mid-jump activation**: only the bound applies. Making the cast wait for landing is not attempted.
- **Why `blockStart` did not end the session** in the user's test: the gate and R12 make it unnecessary. The baseline
  log will show whether the event arrived.

## R12. An interrupted cast closes the session

**Decision**: in the last branch of `OnCasterInterrupt` (a real, non-spurious interruption of a plugin-cast hand), do
what the Skip Channeling failure path already does:
- `FinishHand(hand)`, plus the other hand in dual cast (Constitution IV),
- `ScheduleSpellFireFinalize` for each finished hand.

`PumpSpellFireFinalize` then waits for the caster to go idle (at most 0.7 s) and calls `TryFinalizeExit`. That ends the
session and restores the gear once every relevant hand is finished.

**Rationale**:
- It reuses a finalize path that is already tested (spec 002, TEST_MATRIX 5b.10).
- It covers every interruption, including blocks from mods that add their own block key, and it does not depend on
  `blockStart`.
- In a slot with two different spells, the session ends when the other hand is done too, as for a normal finish.

**Alternatives considered**: `ForceExit` on every real interruption. Rejected: a slot with two different spells would
lose the hand that is still casting. The finalize path already ends the session once nothing is left.

## R13. Require exclusive hotkey with a held block button (Q4)

**Decision**: no change. With the patch ON, a held attack/block button is an extra key, so the slot is refused and the
log shows `REFUSED (not exclusive) extra=mouse:1` (or `extra=gp:<n>`). Activating a slot while blocking is covered
only with the patch OFF (spec User Story 2 scenarios 4 and 5).

**Rationale**: the user chose to keep the patch's meaning (Constitution V: patch meaning unchanged).
