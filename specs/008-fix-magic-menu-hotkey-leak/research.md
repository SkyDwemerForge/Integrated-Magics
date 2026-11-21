# Research: Slot and wheel hotkeys must not trigger Magic Menu actions

All facts below come from the code on `main` (`f33b99f`). File references are repository-relative.

## R1. Why the Magic Menu sees every hotkey press

**Facts**:
- **Input blocked**: `InputController::ProcessAndFilter` computes `blocked = IsInputBlockedByMenus()`, and the Magic Menu
  is in that list (`src/Input/InputFilter.cpp`).
- **What `blocked` skips**:
  - `UpdateSlotsIfAllowed` runs `DrainWhenBlocked` instead of `RecomputeSlotEdges`, so no slot edge, activation or
    filter window tick happens. That is intended: no slot casts in a menu.
  - `FilterEvents` only runs `if (!blocked)`. So in the Magic Menu no event is held back, consumed or gated, and every
    press of a hotkey key reaches the menu.
- **Quick bind**: `HudController::OnFrame`, in the Magic Menu with the wheel closed, compares `IsSlotHotkeyDown(i)`
  (`SlotComboDown` on `KeyStateStore`) with the previous frame and calls `ExecuteHotkeyAssignment` on the rising edge.
  `KeyStateStore` is written by `ProcessButtonEvents`, which runs before any filter. So quick bind does not depend on
  the filter, and holding events back cannot break it.
- **The wheel**: `UpdateHudToggleState` sets `g_hudTogglePending` on the rising edge of the wheel combo
  (`cache.hud`) while the Magic Menu is open, and `HudController` toggles the popup.
  - `ShouldFilterHudToggle` returns true only when the Magic Menu is open, but it is called only from `FilterEvents`,
    which never runs then.
  - So it is dead code, and the wheel combo reaches the menu.
- **Filtering here reaches the menu**:
  - `PollInputDevicesHook` hands the filtered list on to the game's input dispatch, which feeds the menus too.
  - `FilterMouseForPopup` already relies on this: while the wheel is open, it removes the wheel's clicks and buttons
    from the same list (`src/Input/InputFilter.cpp`), so that the hidden Magic Menu does not act on them.
  - The quickstart checks it in game for this spec.

**Decision**:
- **A filter for the Magic Menu**: while the Magic Menu is open, run a dedicated pass over the frame's button events,
  even though input is blocked for slots.
- **No other change to blocked handling**: no slot edge, no activation, and no gameplay filter in menus, as today.

**Alternatives considered**:
- **Running `FilterEvents` in the Magic Menu**. Rejected:
  - its hold-back is tied to slot activation (pending, chord, overlap resolution, `ConsumeHeldBackForCombo` on an
    edge), and slot edges must not run in menus;
  - it also runs the spec 005 hand-button gate, which is gameplay only;
  - its 50 ms window would have to become context-dependent across all of spec 004's rules.
- **Removing the Magic Menu from `IsInputBlockedByMenus`**. Rejected: slots would activate in the menu.

## R2. Which inputs are held back

**Decision**: a button event is a **hold code** in the Magic Menu when its code is in any active slot's combo or in the
wheel combo, on its own device family:
- keyboard and mouse codes use the `kb` combos, with mouse buttons stored as `kMouseButtonBase + id`;
- gamepad codes use the `gp` combos.

**Never held back**:
- mouse movement and thumbsticks (they are not button events);
- the mouse wheel (ids 8, 9), which keeps the spec 003 rule;
- any button outside every combo, which passes untouched (FR-005).

**Rationale**: these are exactly the keys a quick bind or the wheel toggle can use. Holding anything else would add
delay for nothing.

## R3. The window and the hand-over rule

**Decision**:
- **Window**: each held-back **press** has its own window of `kMagicMenuHoldSec` = 0.3 s from its arrival (FR-001).
  The constant is used only by the Magic Menu filter (FR-004).
- **Order**: held-back events form one queue in arrival order, and they are handed to the menu strictly in that order
  (FIFO). The front entry goes when it is due, then the next one is checked.
- **When a press is due**:
  - its window has ended, or
  - its key is no longer held. A released key can no longer be part of a fully held combo, so waiting longer cannot
    change the outcome (see below).
- **Other entries**: a release in the queue is due as soon as it reaches the front, because its press went before it.

**Rationale**:
- **FIFO**: the menu sees keys in the order the player pressed them.
- **Per-press window**: a key pressed late in another key's window still gets its full 0.3 s.
- **Early hand-over on release**: a tap of a hotkey key used alone (for example, gamepad `B` to go back) reaches the
  menu at its release, not 0.3 s later. This shortens the delay of US3 and keeps SC-003 easy to meet. Quick bind needs
  every key held in the same frame, so a released key can never complete one. This rule is an addition to the user's
  design, and it never changes the outcome. The user approved it (spec Clarifications, 2026-11-21).

**Alternatives considered**:
- **One window per batch** (spec 004 style, from the first held-back press). Rejected: a key pressed at 0.29 s would
  wait only 10 ms.
- **No early hand-over**: rejected, because every tap of a hotkey key would wait the full 0.3 s.
- **Extending the window while a chord is in progress** (spec 004's `kMaxChordHoldSec`): not needed, since 0.3 s
  already covers a 3-key chord pressed at a normal pace.

## R4. Consumption

**Decision**:
- **Fresh press**: if any combo that contains its code is fully held right now (`ComboDown` on `KeyStateStore`, this
  frame), the press is consumed at once. This covers single-key hotkeys and the last key of a chord.
- **Every frame**: for each slot combo and the wheel combo that is fully held, every queued event whose code belongs to
  it is removed, from the hold queue and from the outbox (R5). Its code's fate becomes `Consumed`.
- **Later events**: the held repeats and the release of a consumed code are dropped (FR-002).
- **Detection**: the check reads the same key state the quick bind and the wheel toggle read, in the same frame, so
  "consumed" and "assigned/toggled" always agree.
- **Logs**: `consumed by slot=<n>` or `consumed by wheel` (R10).

**Rationale**: this is the user's rule ("when the combo is detected, the keys are not sent back"), and it matches the
spec 004 rule outside menus (`ConsumeHeldBackForCombo` erases from both retained and deferred events).

## R5. Delivering held-back events to the menu (replay)

**Decision**:
- **Outbox**: due events move to a menu outbox. One event per frame is sent through
  `IntegratedMagic::detail::EnqueueRetainedEvent`, which creates a `ButtonEvent` with the original device, id code,
  user event, value and held time, and prepends it to the next dispatched list.
- **Fate on send**: a code's fate becomes `Passed` when its press is sent, and `None` when its release is sent.
- **While a code waits in the outbox**: its live held repeats are dropped, and its live release joins the outbox behind
  its press. So the menu never sees a repeat or release before the press.
- **Hand-button gate**: menu replays do not go through it. It is a gameplay rule (spec 005), and in a menu the user
  events are menu events.

**Rationale**:
- `DrainStream` (`src/Adapters/Outbound/SyntheticInput.cpp`) puts these events at the head of the list that the menu
  reads (R1).
- One event per frame matches the existing replay (`DrainOneDeferredReplayEvent`), so a press and its release never
  arrive in the same frame.
- The user event is the one the game resolved for the menu context when the event first arrived, so the replay is
  faithful.

**Alternatives considered**:
- **Reusing `DeferredVec`** (slot-indexed, with `ReplayArr` arming). Rejected: the wheel has no slot index, and the
  arming feeds a gameplay rule (phase 2 of `ShouldFilterAndSave`).
- **Sending the whole outbox in one frame**. Rejected: a press and its release would arrive in the same frame.

## R6. State

**Decision**: a new store, `Input::MagicMenuHoldStore` (`include/Input/MagicMenuHold.h`), separate from
`ExclusiveStore`. It holds:
- the hold queue,
- the outbox,
- the per-code menu fate for keyboard/mouse and gamepad.

States and transitions are in [data-model.md](data-model.md).

**Rationale**:
- `ExclusiveStore` is per slot and drives gameplay rules. The Magic Menu needs one queue for slots and the wheel
  together.
- Separate state means the gameplay path (spec 004) is untouched (FR-004, SC-005).

## R7. Transitions and interactions

**Decision**:
- **Magic Menu closes**: the store is cleared, and its queued events are dropped (spec edge case).
  - The physical release of a key still held then goes to gameplay, where `FilterEvents` passes a release with no
    fate. That is harmless.
  - `ClearLikelyStuckKeysAfterMenuClose` still runs on the blocked → unblocked transition, as today.
- **Magic Menu opens**: the store is already empty. A release of a key pressed before the menu opened has fate `None`
  in the menu store, so it passes (spec edge case).
- **Game load** (`ResetInputState`) and **hotkey reload** (`ConsumeHotkeyReloadRequest` / `OnConfigChanged`): the store
  is cleared too, since combos may have changed.
- **Wheel open**: `FilterMouseForPopup` runs first and removes the wheel's own controls. The menu hold then sees only
  what is left, so the wheel's controls are unchanged.
- **Hotkey capture pending** (`wantCaptureBefore`): the Magic Menu hold does not run, so capture keeps its behavior.

## R8. The dead wheel filter

**Decision**: remove `ShouldFilterHudToggle` (`include/Input/HudToggle.h`, `src/Input/Hudtoggle.cpp`) and its call in
`FilterEvents`.
- **Why it is dead**: it only returns true while the Magic Menu is open, and `FilterEvents` never runs then (R1).
- **What replaces it**: the wheel combo is covered by the Magic Menu hold (R2, R4).
- **Gameplay**: removing the call changes nothing, since the function returned false there.

## R9. Gameplay stays as it is

**Decision**:
- **Gating**: the new pass and its tick run only when the Magic Menu is open and no capture is pending.
- **Unchanged**: `kComboToleranceSec`, `FilterEvents` (apart from R8), `ShouldFilterAndSave`, `TickFilterWindows` and
  `RecomputeSlotEdges`.
- **Regression check**: TEST_MATRIX section 6 (SC-005).

## R10. Logs (FR-010, Constitution VI)

All lines use the prefix `[Input] MenuHold:`. They are written only for presses and releases, never for held repeats:
- `<label> held (queue=<n>)`;
- `<label> consumed by slot=<n>` / `<label> consumed by wheel`;
- `<label> handed to menu after <s>s (window|released)`;
- `<n> queued event(s) dropped (Magic Menu closed|reset)`.

`<label>` is `DescribeHeldCode` (`kb:0x..`, `mouse:<id>`, `gp:<n>`). Format details are in
[contracts/magic-menu-hold.md](contracts/magic-menu-hold.md).

## R11. TEST_MATRIX cases (FR-011)

New cases in section 10 (HUD), in Portuguese:
- 10.11: quick bind with a keyboard hotkey, single-key and multi-key;
- 10.12: quick bind on gamepad with the default layout, both orders;
- 10.13: the wheel toggle on keyboard and gamepad;
- 10.14: a hotkey button used alone in the menu;
- 10.15: buttons outside every hotkey have no delay;
- 10.16: closing the menu with a key held leaves nothing stuck.

The regression runs TEST_MATRIX section 6 (6.1–6.23) and 10.1–10.3.

## R12. Validation build (Constitution VI)

**Facts**: the DEBUG DLL still faults at start-up inside `Hooks::Install_Hooks`, a DEBUG-only defect unrelated to this
spec. `include/PCH.h` defines `DEBUG`, so the release DLL writes the same logs.

**Decision**: in-game validation on the release DLL, as in specs 002–007. The user approved it for this spec
(2026-11-21, spec Clarifications; plan Complexity Tracking).

## R13. Overlapping hotkeys in quick bind (FR-013, found in the first in-game test)

**Facts**:
- **The report**: gamepad hotkeys `RT + LT + A` (slot 3) and `RT + A` (slot 7). Quick-binding with the longer one gave
  Flames to both slots. The log shows two `[Assign] ComputeSpellAssignment` lines in the same frame (14:39:54.236).
  The same happened with slots 0 and 4 (14:40:15.898).
- **The cause**: `HudController::OnFrame` assigned every slot whose hotkey rose to fully held in that frame
  (`IsSlotHotkeyDown` per slot, a rising-edge mask, then `ExecuteHotkeyAssignment`). It had no overlap rule.
- **Gameplay already has one**: spec 004's `ResolveOverlaps` makes the longest combo win, and the lower slot win
  between identical combos. It also has a pending wait, so a longer combo completed shortly after still wins.

**Decision**: a quick-bind resolver in the input layer (`include/Input/QuickBindResolver.h`,
`src/Input/QuickBindResolver.cpp`), called by `HudController` through `InputController::ResolveQuickBindSlots`. Each
frame:
1. **Pending slots**:
   - a pending slot whose longer hotkey became fully held is suppressed;
   - otherwise it is assigned when its keys are released, or when it has waited `kMagicMenuHoldSec` (0.3 s).
2. **Slots that rose this frame**, in order:
   - suppressed when a held slot strictly contains their hotkey (longer combo);
   - suppressed when a lower slot rising now has the same keys (identical combo);
   - pending when some other slot strictly contains their hotkey, even if it is not held yet;
   - otherwise assigned at once.
3. **Inactive** (outside the Magic Menu, or the wheel open): the resolver only tracks the held state, so a hotkey held
   across the change does not fire later, and it drops pending slots.

**Rationale**:
- **Same-frame completion** (the reported case) is covered by step 2.
- **Shorter first**: the pending wait covers the shorter hotkey completing a frame before the longer one, since press
  order is never exact on a gamepad.
- **Same window**: 0.3 s is the Magic Menu window the user chose for completing a hotkey.
- **No delay elsewhere**: hotkeys contained in no other hotkey are assigned at once, as before.
- **Keys stay hidden while pending**: the hold of R4 already consumes the keys of every fully held hotkey, so a pending
  quick bind never lets them reach the menu.

**Alternatives considered**:
- **Only the same-frame rule**: rejected, because it fails when the shorter hotkey completes first.
- **Spec 004's 50 ms pending**: rejected. In the Magic Menu, the window for completing a hotkey is 0.3 s.
- **Moving `ExclusiveTracker`'s `ResolveOverlaps` into the menu**: rejected, since it is tied to slot activation and
  the gameplay pending state.
