# Contract: input hold in the Magic Menu (research R2–R7, R10)

The rules below define which input events the Magic Menu receives while it is open. States and fields are in
[data-model.md](../data-model.md).

## §1. Scope

- **Applies while**:
  - the Magic Menu (`"MagicMenu"`) is open,
  - and no hotkey capture is pending.
- **Event kinds**:
  - only button events are classified;
  - mouse movement, thumbsticks and the mouse wheel (ids 8, 9) pass untouched.
- **Hold codes**: a button event is a hold code when its code is in an active slot's combo or in the wheel combo, for
  its device family (R2). Any other button passes untouched.

## §2. Per event (`FilterMagicMenuEvents`)

| Event | Fate of its code | Result |
|---|---|---|
| Fresh press of a hold code | any | consumed if a combo containing it is fully held now. Otherwise held: appended to the queue, fate `Held` |
| Held repeat | `Held`, `Handing`, `Consumed` | dropped |
| Held repeat | `None`, `Passed` | passes |
| Release | `Held` | appended to the queue |
| Release | `Handing` | appended to the outbox |
| Release | `Consumed` | dropped, fate `None` |
| Release | `None`, `Passed` | passes, fate `None` |

## §3. Per frame (`TickMagicMenuHold`)

1. **Consume**: for every slot combo and the wheel combo that is fully held now, remove every queued or outbox event of
   its codes. Their fate becomes `Consumed`.
2. **Age**: add `dt` to every queued press.
3. **Hand over**, in FIFO order:
   - **A press** at the front is due when `ageSecs >= kMagicMenuHoldSec` (0.3 s), or when its key is no longer held.
   - **A release** at the front is always due.
   - **Due entries** move to the outbox, and their code becomes `Handing`. The hand-over stops at the first entry that
     is not due.

Then the outbox sends one event per frame through the synthetic input stream:
- a press sets its code to `Passed`;
- a release sets it to `None`.

## §4. Guarantees

- **Consumed for good**: a hotkey that completes in the Magic Menu reaches it with none of its keys: no press, no held
  repeat, no release (FR-002).
- **Keys used alone**: a hold code that completes no hotkey reaches the menu once, in order, at most 0.3 s after its
  press, plus one frame per event queued ahead of it (FR-003).
- **No reordering**: the menu never sees a repeat or a release before the press it belongs to.
- **No delay elsewhere**: input that is not a hold code is never delayed (FR-005).
- **No stuck key**: closing the Magic Menu, a game load or a hotkey reload clears the store. Nothing queued is sent
  afterwards (FR-009).
- **Gameplay**: nothing here runs outside the Magic Menu. The gameplay filter of spec 004 is unchanged (FR-004).

## §5. Logs

All lines use the prefix `[Input] MenuHold:`, for presses and releases only:

| Line | When |
|---|---|
| `<label> held (queue=<n>)` | A press is held back |
| `<label> consumed by slot=<n>` / `<label> consumed by wheel` | A queued press, a fresh press or a release is consumed |
| `<label> handed to menu after <s>s (window\|released)` | A press moves to the outbox |
| `<n> queued event(s) dropped (Magic Menu closed\|reset)` | The store is cleared with events still in it |

`<label>` is `DescribeHeldCode`: `kb:0x..`, `mouse:<id>` or `gp:<n>`.

## §6. Which slots a quick bind assigns (`ResolveQuickBind`, research R13)

Runs every frame. It assigns only while the Magic Menu is open and the wheel is closed. A hotkey is "held" on a device
when all its keys on that device are held.

1. **Pending slots**:
   - suppressed if a slot whose hotkey strictly contains theirs is now held;
   - otherwise assigned when released, or when they have waited `kMagicMenuHoldSec` (0.3 s).
2. **Slots whose hotkey became held this frame**:
   - suppressed if a held slot strictly contains their hotkey;
   - suppressed if a lower slot rising now has the same keys;
   - pending if any other slot strictly contains their hotkey;
   - otherwise assigned at once.
3. **Inactive**: nothing is assigned, pending slots are dropped, and the held state is tracked.

**Guarantees**:
- one quick bind assigns one slot: the longest hotkey, or the lower slot between identical ones;
- a hotkey contained in no other hotkey is assigned with no delay;
- the keys of a pending hotkey never reach the menu, because §3 consumes every fully held hotkey.

**Logs** (`[Input] QuickBind:`):

| Line | When |
|---|---|
| `slot=<s> waiting for a longer combo (slot=<t>)` | A slot goes pending |
| `slot=<s> SUPPRESSED by slot=<t> (longer combo\|identical combo)` | A slot loses an overlap |
| `slot=<s> assigned after <x>s (released\|window)` | A pending slot is assigned |
| `pending slot(s) dropped (mask=<m>)` | The resolver went inactive with slots pending |
