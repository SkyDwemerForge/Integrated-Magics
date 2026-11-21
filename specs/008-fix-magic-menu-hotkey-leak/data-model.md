# Data Model: Slot and wheel hotkeys must not trigger Magic Menu actions

No setting, INI key or persisted data changes (spec FR-012). Everything below is runtime input state, owned by
`Application::InputController` and used only on the main thread, inside `PollInputDevicesHook`.

## Constant

| Name | Where | Value | Meaning |
|---|---|---|---|
| `kMagicMenuHoldSec` | `include/Shared/InputConstants.h` | `0.3f` | Window of a held-back press in the Magic Menu (FR-001). Used by the Magic Menu hold only (FR-004) |

## `Input::MagicMenuHoldStore` (`include/Input/MagicMenuHold.h`)

| Field | Type | Meaning |
|---|---|---|
| `queue` | `std::vector<MenuHeldEvent>` | Held-back events, in arrival order (research R3) |
| `outbox` | `std::deque<MenuHeldEvent>` | Events handed to the menu, sent one per frame (R5) |
| `kbFate` | `std::array<MenuFate, kMaxCode>` | Fate of the latest press per keyboard/mouse code |
| `gpFate` | `std::array<MenuFate, kMaxCode>` | Fate of the latest press per gamepad index |

`MenuHeldEvent`:

| Field | Type | Meaning |
|---|---|---|
| `ev` | `RetainedEvent` | The original event: device, raw id, user event, value, held time |
| `code` | `int` | `EffectiveComboCode(dev, rawIdCode)` |
| `gp` | `bool` | Gamepad event |
| `isPress` | `bool` | A fresh press (value > 0, held time 0). Otherwise a release; held repeats are never stored |
| `ageSecs` | `float` | Time since the event arrived. It only matters for presses |

## `MenuFate` per code

| State | Meaning | Live held repeat | Live release |
|---|---|---|---|
| `None` | No press tracked, or its release was sent | passes | passes |
| `Held` | Its press is in `queue` | dropped | appended to `queue` |
| `Handing` | Its press is in `outbox`, not sent yet | dropped | appended to `outbox` |
| `Passed` | Its press reached the menu, or was never held | passes | passes, fate → `None` |
| `Consumed` | Its hotkey completed | dropped | dropped, fate → `None` |

### Transitions

```text
fresh press of a hold code (R2):
  a combo containing it is fully held ──────────────► Consumed (event dropped)
  otherwise ────────────────────────────────────────► Held (appended to queue, age 0)
fresh press of any other code ──────────────────────► not tracked (passes, no delay)

each frame, while the Magic Menu is open (TickMagicMenuHold):
  1. consume: for each slot combo and the wheel combo that is fully held,
     remove its codes' events from queue and outbox ─► Consumed
  2. age: every queued press ageSecs += dt
  3. hand over (FIFO): while the front of queue is due ─► moved to outbox; its code ─► Handing
       due press   = ageSecs >= kMagicMenuHoldSec, or its key is not held
       due release = always (its press went first)

each frame, after the tick: send outbox.front() through the synthetic stream
  press sent ───────────────────────────────────────► Passed
  release sent ─────────────────────────────────────► None

Magic Menu closes, game load, hotkey reload ────────► store cleared (queue and outbox dropped, fates None)
```

## `Input::QuickBindState` (`include/Input/QuickBindResolver.h`), research R13

| Field | Type | Meaning |
|---|---|---|
| `wasDown` | `std::uint64_t` | Slots whose hotkey was fully held last frame, used for rising edges |
| `pending` | `std::uint64_t` | Slots whose hotkey completed while a longer hotkey that contains it can still complete |
| `pendingSecs` | `std::array<float, kInputMaxSlots>` | Wait of each pending slot, at most `kMagicMenuHoldSec` |

- **Owner**: `InputController` (`m_quickBind`).
- **Writer**: `ResolveQuickBind`, called once per frame by `HudController::OnFrame` through
  `InputController::ResolveQuickBindSlots`.
- **Reset**: by `ResetQuickBind` on game load, hotkey reload and config change.
- **Inactive frames** (outside the Magic Menu, or the wheel open) drop `pending` and refresh `wasDown`.

## Rules

- **Order of a frame** (`InputController::ProcessAndFilter`), while the Magic Menu is open and no capture is pending:
  1. `ProcessButtonEvents`, which records the key state;
  2. `FilterMouseForPopup`, which handles the wheel's own controls;
  3. `FilterMagicMenuEvents`, which classifies this frame's events;
  4. `TickMagicMenuHold(dt)`;
  5. the outbox sends one event.
- **Combo detection** uses `ComboDown` on the same `KeyStateStore` the quick bind (`SlotComboDown`) and the wheel toggle
  (`UpdateHudToggleState`) read, so a consumed combo is always an assigned or toggled one.
- **Bound**: a press waits at most `kMagicMenuHoldSec` in the queue, plus the frames needed to send the outbox entries
  ahead of it. No wait is unbounded.
- **Separation**: the store is not part of `ExclusiveStore` and never touches gameplay fates (research R6). It is not
  `MagicState` state, so Constitution II's `MagicState` reset rules do not apply. It is cleared on every path that
  leaves the Magic Menu (R7).
