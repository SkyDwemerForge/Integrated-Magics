# Data Model: Fix slot hotkeys that stop working after using the mouse wheel

No persisted data changes. The INI keys, hotkey encodings and save databases stay as they are. This model covers the
in-memory input state that the fix adds or constrains.

## Input codes (existing, `include/Shared/InputConstants.h`), new constants

| Name | Value | Meaning |
|---|---|---|
| `kMouseButtonBase` (existing) | `256` | Mouse codes are stored as `256 + mouse button id` in the keyboard/mouse record |
| `kMouseWheelUpId` | `8` | `RE::BSWin32MouseDevice::Keys::kWheelUp` |
| `kMouseWheelDownId` | `9` | `RE::BSWin32MouseDevice::Keys::kWheelDown` |
| `kMouseFirstNoVkId` | `5` | First mouse button with no Windows virtual key (`kButton5`) |
| `kMouseLastNoVkId` | `7` | Last one (`kButton7`) |
| `kMouseHeartbeatTimeoutMs` | `500` | A no-virtual-key button with no pressed event for longer than this is considered released |

Helpers (pure, no `RE::` types):

- `IsMouseWheelId(int mouseId)`: `mouseId == 8 || mouseId == 9`.
- `IsMouseNoVkId(int mouseId)`: `5 <= mouseId <= 7`.

## KeyStateStore (existing, `include/Input/KeyStateStore.h`)

| Field | Type | Change |
|---|---|---|
| `kbDown` | `std::array<std::atomic_bool, kMaxCode>` | **Rule change**: entries `256 + 8` and `256 + 9` (wheel) are never set. The wheel is filtered out before any write (research R2) |
| `gpDown` | `std::array<std::atomic_bool, kMaxCode>` | unchanged |
| `mouseNoVkLastPressedMs` | `std::array<std::uint64_t, 3>` | **New**. Steady-clock milliseconds of the last pressed event (`IsDown()` or `IsHeld()`) for mouse ids 5, 6 and 7 (research R6). Main thread only, so no atomics are needed |

### Validation rules

- **Invariant**: `kbDown[256 + 8]` and `kbDown[256 + 9]` are always `false`.
- `mouseNoVkLastPressedMs[i]` is updated on every pressed event for mouse id `5 + i`, including `IsHeld()` events
  that the rest of `ProcessButtonEvents` ignores. Release (`IsUp()`) events keep the existing behavior: they clear
  `kbDown`.
- **Premise**: held events arrive at least every 500 ms while a button is held. The first held event of **any**
  mouse button is logged once per session, so this can be checked in game (research R6, T018 step 1).

### Lifecycle

```text
mouse id 5..7:
  IsDown()  ──▶ kbDown = true,  lastPressedMs = now
  IsHeld()  ──▶ (kbDown unchanged), lastPressedMs = now
  IsUp()    ──▶ kbDown = false
  recovery pass: kbDown && now - lastPressedMs > 500 ms ──▶ kbDown = false, ClearEdgeStateOnly, log "cleared stuck keys"

mouse id 8, 9 (wheel):
  any event ──▶ ignored for hotkeys (no capture, no kbDown write); passed to the game unchanged
```

## SlotHotkeys / HotkeyCacheStore (existing), validation note

No structural change. `LoadHotkeyCache_FromConfig` logs a one-time warning per slot whose keyboard/mouse combo
contains `256 + 8` or `256 + 9` (research R5). The combo is kept as written in the INI. It can never complete,
because the wheel is never held.

## ExclusiveStore (existing)

No new fields. The previous raw state (`prevRawKbDown[s]`, `prevRawGpDown[s]`) is read **before** it is overwritten,
so the refusal log can fire only on the frame the combo becomes held (research R7).
