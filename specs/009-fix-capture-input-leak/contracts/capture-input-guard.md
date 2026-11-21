# Contract: input during a hotkey capture (research R2–R7, R9, R12)

Rules for what the SKSE menu receives while one of the plugin's hotkey fields captures input. The state is in
[data-model.md](../data-model.md).

## §1. Capture sources

| Device | Source | Rule |
|---|---|---|
| Gamepad | XInput, read once per frame in `PollCapturedInput`, only while a capture waits or the guard is on | Only a button that goes from up to down while the capture waits. The first waiting frame records the buttons already held and captures nothing |
| Mouse | `GetAsyncKeyState` in `PollCapturedInput`, and `WM_*BUTTONDOWN` in `WndProcHook` | The same edge rule, primed on the first waiting frame |
| Keyboard | `WM_KEYDOWN` / `WM_SYSKEYDOWN` in `WndProcHook` | Ignored when `lParam` bit 30 is set (auto-repeat of a key held from before). `Escape` is never captured, and it cancels the capture |

The captured value is otherwise unchanged (spec FR-003).

## §2. Outcome of a captured input

| Field waits for | Input | Outcome |
|---|---|---|
| keyboard | key or mouse button | stored in the field, as today |
| gamepad | gamepad button or trigger | stored in the field, as today |
| keyboard | gamepad button or trigger | capture cancelled, field unchanged (FR-010) |
| gamepad | key or mouse button | capture cancelled, field unchanged (FR-010) |
| any | `Escape` | capture cancelled, field unchanged. The SKSE menu does not see it, so it stays open (R12) |

## §3. What the SKSE menu receives (the guard)

- **While a capture waits**:
  - **no input events** (research R12): `MenuInputShieldHook`, the outermost hook at the input call site, hands every
    hook inside it an empty event list. That includes SKSE Menu Framework's hook, the plugin's `PollInputDevicesHook`
    and the game. It holds only while the plugin's page was drawn in the last 0.25 s;
  - every named ImGui key (keyboard, gamepad, mouse buttons and wheel) is claimed with `igSetKeyOwner`, owner
    `0x494D4347` and `ImGuiInputFlags_LockUntilRelease`, once per frame. This covers input ImGui reads by itself
    (research R2b);
  - the framework's open/close hotkey is disabled (`SetHotkeyEnabled(false)`).
- **After the capture ends**, by value or by cancel: all three continue until every gamepad button and trigger, mouse
  button and keyboard key is released, for at most 3 s.
- **Then**:
  - events pass again on the same frame;
  - the hotkey state is restored as found;
  - unheld keys lose the owner at the next `NewFrame`, and `LockUntilRelease` keeps a held key owned until its
    release.
- **Not used**:
  - config flags: the framework rewrites them every frame (research R2, R8);
  - the framework's `AddInputEvent` callback: it is called, but the framework acts on the raw events before it
    (research R12).

## §4. Ending a capture

A capture ends on exactly one of these paths, and the guard always ends after it:

1. a value is stored (§2);
2. the other device or `Escape` cancels it (§2);
3. the slot selection changes (`CancelFieldCapture`, as today);
4. the page has not been drawn for 0.25 s, because the menu closed or another section is shown.

`DrawKeyValueCapture` clears a stale `g_fieldCapture` when no capture waits and nothing can be polled.

## §5. Guarantees

- **Captured input**: it never drives the SKSE menu, neither when pressed, while held nor at its release (FR-001,
  FR-002, FR-010).
- **The starting input**: the press that started the capture is not taken as the value (FR-004).
- **Outside captures**: the SKSE menu's input is exactly as before, and the guard is off (FR-005).
- **Context left as found**: no capture leaves a key owned, the hotkey disabled or events hidden after its input is
  released (FR-006). The shared ImGui context ends as the plugin found it, as spec 006 requires.
- **Other hooks at the site**: they lose events only during a capture on the plugin's page, until its input is
  released, and the hook always calls the next one (Constitution V).

## §6. Logs

| Line | When |
|---|---|
| `[Hooks] MenuInputShieldHook installed (outer, previous target in <module>)` | Once, on the first capture. `<module>` is the hook that was outermost before |
| `[UI] CaptureGuard: on (menu input hidden, keys claimed, hotkey off)` | The guard turns on |
| `[UI] CaptureGuard: <n> ImGui keys claimed` | Once per guard period, after the first claim |
| `[UI] CaptureGuard: waiting for release of <gp:n\|vk:0x..>` | After the capture, when the input that keeps the guard on changes |
| `[UI] CaptureGuard: off after <s>s (released\|cap), input frames hidden from the menu=<n>` | The guard ends: events pass again and the hotkey is restored |
| `[UI] Capture: <kb\|gp> input during a <gp\|kb> capture -> cancelled` | §2, other device |
| `[UI] Capture: Escape -> cancelled` | §2, `Escape` |
| `[UI] Capture: page not shown -> cancelled` | §4, path 4 |
| `[UI] Capture: stale field capture cleared` | §4, reconcile |
