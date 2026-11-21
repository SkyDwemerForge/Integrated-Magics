# Data Model: The SKSE menu must not react to input pressed during a hotkey capture

No setting, INI key or persisted data changes (spec FR-009). Everything below is runtime UI and input state, used on
the main thread: the Present hook, the window procedure, and the SKSE menu page draw.

## Constants (`include/UI/CaptureInputGuard.h`)

| Name | Value | Meaning |
|---|---|---|
| `kCaptureGuardReleaseCapSec` | `3.0f` | Longest time the guard stays on after the capture ended, waiting for releases (R3) |
| `kCapturePageTimeoutSec` | `0.25f` | A waiting capture whose page was not drawn for this long is cancelled (R6) |

## `UI::CaptureInputGuard` state (`src/UI/CaptureInputGuard.cpp`, file-local)

| Field | Type | Meaning |
|---|---|---|
| `on` | `bool` | The guard hides the input events from the SKSE menu, claims its keys and has the hotkey disabled |
| `savedHotkeyEnabled` | `bool` | `SKSEMenuFramework::IsHotkeyEnabled()` when the guard turned on |
| `claimLogged` | `bool` | The "keys claimed" line was already written for this guard period |
| `hiddenFrames` | `int` | Input frames `MenuInputShieldHook` emptied in this guard period, reported by the `off` line |
| `releaseWaitSecs` | `float` | Time since the capture ended while inputs are still held |
| `onSince` / `lastUpdate` | steady-clock time points | When the guard turned on, and the last update (its frame time) |
| `pageEverDrawn` / `lastPageDraw` | `bool` / steady-clock time point | The page was drawn at least once, and when last (R6) |

**Hiding the events** (R12): `ShouldHideMenuInput()` is `(on || captureWaiting) && page drawn within 0.25 s`.
`MenuInputShieldHook` reads it once per input frame.

**Key owner** (R2b): `kGuardOwnerId = 0x494D4347`. While on, every named ImGui key is claimed once per frame with
`ImGuiInputFlags_LockUntilRelease`. A first version saved and restored three `ImGuiConfigFlags` bits instead; it was
dropped because the framework rewrites those flags every frame (R2, R8).

## Edge state for captures (`src/Adapters/Inbound/DXGIPresentHook.cpp`, file-local)

| Field | Type | Meaning |
|---|---|---|
| `s_prevGamepad` | `std::uint32_t` | Gamepad buttons held last frame: XInput button bits mapped to the plugin's indices 0–13, with triggers as 14 and 15 |
| `s_prevMouse[5]` | `bool[5]` | Mouse buttons held last frame |
| `s_wasWaiting` | `bool` | A capture waited last frame. On the first waiting frame, both previous states are primed with what is held, and nothing is captured (R4) |

Both previous states are read only while a capture waits or the guard is on, because XInput is slow with no controller
connected.

## Guard input (per frame)

`UI::CaptureGuardInput`:

| Field | Meaning |
|---|---|
| `captureWaiting` | `CaptureState::captureRequested \|\| CaptureState::captureActive` |
| `anyInputHeld` | Any gamepad button or trigger, mouse button or keyboard key is held. Computed only while the guard is on and no capture waits |

The guard measures its own time from the steady clock.

## Transitions

```text
off ── captureWaiting ─────────────────────────────► on   (save the hotkey state, hotkey off, claim the keys, hide the events; log "on")
on  ── each frame ─────────────────────────────────► claim every named key again (LockUntilRelease)
on  ── !captureWaiting && !anyInputHeld ───────────► off  (restore the hotkey; keys lose the owner by themselves; log "off ... (released)")
on  ── !captureWaiting && releaseWaitSecs >= 3 s ──► off  (restore; log "off ... (cap)")
any ── captureWaiting && page not drawn for 0.25 s ► capture cancelled (log "page not shown"), then the rules above
```

## Rules

- **Nothing to restore for the keys**: once the guard stops claiming them, unheld keys lose the owner at the next
  `NewFrame`, and held keys at their release. Only the hotkey state is restored.
- **No framework**: `GetMenuFrameworkModule() == nullptr` means no SKSE menu, so no capture can start. The guard does
  nothing and never calls a null export.
- **Separate from gameplay**: the guard is not `MagicState` and not `InputController` state. It touches only the SKSE
  menu's ImGui key owners, its hotkey flag and, through `MenuInputShieldHook`, the event list of the input call site
  while the plugin's page is shown.
