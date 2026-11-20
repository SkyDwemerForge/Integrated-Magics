# Phase 0 Research: Fix slot hotkeys that stop working after using the mouse wheel

Evidence comes from the code on `fix/mouse-wheel-hotkeys` (branched from `main` at `8fc41d1`), the two player reports
in `.claude/tasks/integrated_magic_bugs_e_melhorias.md` (BUG-01) and `.claude/tasks/bug-02-hotkeys-roda-mouse-exclusive.md`,
and the CommonLibSSE-NG v16.9.0 headers.

---

## R1. Root cause (confirmed in code)

**Decision**: the wheel is recorded as a held key and never released. With Require exclusive hotkey ON, that phantom
key makes every slot fail the exclusivity test.

| Step | Code | What happens |
|---|---|---|
| 1 | `RE::BSWin32MouseDevice::Keys` | The wheel is reported as mouse buttons `kWheelUp = 8` and `kWheelDown = 9` |
| 2 | `RE::ButtonEvent` (`IsDown() = value > 0 && heldDownSecs == 0`, `IsUp() = value == 0 && heldDownSecs > 0`) | A wheel notch arrives as one `IsDown()` event. No `IsUp()` event follows: the wheel has no held phase |
| 3 | `src/Input/InputFilter.cpp` `ProcessButtonEvents`, mouse branch | `keys.kbDown[256 + code].store(btn->IsDown())` sets `kbDown[264]` / `kbDown[265]` to `true`. Nothing ever sets them back to `false` |
| 4 | `src/Input/PhysicalReconciler.cpp` `ReconcilePhysicalKeyState` | The stuck-key recovery checks keyboard codes, mouse codes 256–260 (`VK_LBUTTON` … `VK_XBUTTON2`) and gamepad. It never checks 264/265 |
| 5 | `include/Input/HotkeyMatcher.h` `ComboExclusiveNow` | It scans every held code. A held code that is not in the combo and not W/A/S/D (`IsAllowedExtra_Keyboard_MoveOrCamera`) makes the slot "not exclusive" |
| 6 | `src/Input/ExclusiveTracker.cpp:192` / `:209` | When the combo is not exclusive, the slot is refused **silently**: the code falls through to `return false` with no log line |
| 7 | `src/Input/ExclusiveTracker.cpp` `ClearLikelyStuckKeysAfterMenuClose` | When a menu that blocks input closes, every held code that is not part of a slot combo is cleared. This is why "opening Esc, Favorites or the inventory fixes it" |

**Why arrow keys in QuickLoot IE do not cause it**: keyboard keys send a real `IsUp()` event, and step 4 would clear
them anyway.

**Why it only shows with Require exclusive hotkey ON**: with the patch OFF, the code paths that read held keys
(`AnyComboKeyDown`, `ComboDown`, HUD toggle) only look at codes inside a combo. The phantom wheel code is recorded but
never read. See R8.

---

## R2. Fix: ignore the wheel at the input boundary

**Decision**: in `ProcessButtonEvents`, a mouse event whose code is the wheel (`kWheelUp` / `kWheelDown`) is skipped
before hotkey capture and before any held-input write. The wheel never enters `KeyStateStore`. The predicate is a
pure helper, `IsMouseWheelId(int mouseId)`, in `include/Shared/InputConstants.h` next to `kMouseButtonBase`, with
no `RE::` types (Constitution VI, testable logic). See data-model.md for the constants.

**Rationale**:
- It removes the cause instead of compensating for it. FR-001 holds by construction: the record never contains an
  input that cannot be held.
- FR-002 and FR-003 follow directly. The exclusivity scan can never see the wheel, not even for one frame.
- It matches Clarification Q1: the wheel is not a hotkey input.
- It is one early `continue` on a path that already special-cases mouse events. There is no new state and no
  per-frame cost.

**Alternatives considered**:
- *Treat a notch as press + release in the same frame (pulse)*. Rejected: the wheel would still be "held" during
  that frame's slot evaluation, so a scroll during a hotkey press could still cancel it (FR-003). It would only be
  useful if the wheel were bindable, which Q1 rules out.
- *Add the wheel codes to `IsAllowedExtra_Keyboard_MoveOrCamera`*. Rejected: it hides the symptom for the
  exclusive rule only. The code stays recorded as held (FR-001), and every future reader of held keys would inherit
  the bug.
- *Let the stuck-key recovery clear the wheel*. Rejected: the recovery runs every 200 ms and is skipped while a slot
  session or an exclusive press is pending. A hotkey pressed right after a scroll would still be refused, which
  breaks FR-002 (first press).

---

## R3. The wheel keeps reaching the game (FR-004)

**Decision**: `FilterEvents` (`src/Input/InputFilter.cpp`) never filters a wheel event. The check comes before
`ShouldFilterAndSave` and `ShouldFilterHudToggle`.

**Rationale**:
- Today the wheel already passes, because `ShouldFilterAndSave` only withholds codes that belong to a slot combo,
  and the UI capture cannot put the wheel in a combo.
- The explicit skip guarantees the same result for a hand-edited INI combo (R5). Without it, such a combo would make
  the plugin withhold and replay wheel events, which would delay camera zoom and QuickLoot scrolling.

**Unchanged on purpose**: while a hotkey capture is waiting (`wantCaptureBefore && !blocked` in
`InputController::ProcessAndFilter`), every button event is withheld from the game, the wheel included. That is the
capture UI's existing behavior and is not part of this bug.

---

## R4. Hotkey capture never records the wheel (FR-005)

**Decision**: the wheel skip in R2 sits **before** `TryHandleCapture` in the mouse branch. The two UI capture paths
already never record the wheel:
- `WndProcHook` handles `WM_KEYDOWN`, `WM_LBUTTONDOWN`, `WM_RBUTTONDOWN` and `WM_MBUTTONDOWN`, but not
  `WM_MOUSEWHEEL`.
- `DXGIPresentHook::PollCapturedInput` reads `GetAsyncKeyState` for five mouse virtual keys, and the wheel has no
  virtual key.

**Rationale**: the game-event path was the only way the wheel could be captured, for example when scrolling while a
capture was waiting.

---

## R5. A wheel code in a hand-edited combo

**Decision**: no INI rewriting or sanitizing.
- **Effect**: the wheel part of such a combo is never held, so that combo never completes and its slot cannot
  activate through it. It cannot block other slots, because nothing records the wheel (R2), and wheel events pass
  through (R3).
- **Log**: `LoadHotkeyCache_FromConfig` logs one line per affected slot when it loads the hotkeys:
  `[Input] HotkeyCache: slot=<n> combo contains the mouse wheel (code=<c>), not supported; this combo cannot activate`.

**Rationale**: this matches the spec's edge case and Clarification Q1 with no new configuration behavior. Rewriting
the INI could silently turn `Shift + wheel` into `Shift` alone, which would be worse.

---

## R6. Stuck-key recovery for mouse buttons without a Windows key state (FR-007)

**Finding**: Skyrim reports eight mouse buttons (`kLeftButton` … `kButton7`, codes 0–7). Windows virtual keys only
exist for five of them (`VK_LBUTTON`, `VK_RBUTTON`, `VK_MBUTTON`, `VK_XBUTTON1`, `VK_XBUTTON2` → codes 0–4).
- **Codes 5–7** cannot be checked with `GetAsyncKeyState`. If their release is lost (alt-tab while holding, or a
  synthetic press from another mod), they stay held until a menu closes.
- **Everything else is already covered**: keyboard codes (via `MapVirtualKeyA` + `GetAsyncKeyState`), mouse codes
  0–4 and gamepad buttons and triggers (XInput) are checked every 200 ms while no slot session or exclusive press is
  pending.

**Decision**: a heartbeat for mouse codes 5–7.
- The game sends a button event every frame while a button is held: `IsDown()` on the first frame, `IsHeld()`
  (`value > 0 && heldDownSecs > 0`) after that.
- `ProcessButtonEvents` records the time of the last pressed event (down or held) for codes 5–7, before its existing
  "only down/up" filter.
- `ReconcilePhysicalKeyState` clears codes 5–7 that are recorded as held but had no pressed event for more than
  **500 ms**.
- With the recovery's 200 ms cadence, a lost release is cleared within about 0.7 s, under the 1 s that FR-007 sets.
- Clearing uses the existing `clearedAny` → `ClearEdgeStateOnly` path and its log line.

**Rationale**: this is the only signal available for those codes, and it is local to three codes. A held event does
not re-mark a cleared button as held, which keeps the menu-close behavior the same as for keyboard keys.

**Premise not yet confirmed in game** (`/speckit-analyze` A1): the heartbeat assumes that held events reach
`PollInputDevicesHook` every frame. `ButtonEvent::IsHeld()` exists for that case, and the game's own hold-to-act
handlers depend on it, but this plugin has never observed it.
- **Verification**: T015 logs the first held mouse event of the session for any button. T018 step 1 checks it with
  the left or right button, so no 6+ button mouse is needed.
- **If the line never appears**, the premise is wrong and T015/T016 must not ship. A wrong premise would clear held
  buttons 6–8 falsely after 500 ms.

**Alternatives considered**:
- *Clear codes 5–7 on every recovery pass*. Rejected: it would break a combo that uses a sixth or higher mouse button
  while the player is actually holding it.
- *A heartbeat for every input*. Rejected: the other inputs already have a reliable physical check, so a second
  mechanism would only add risk.

---

## R7. Logging the key that blocks a slot (FR-009)

**Finding**: there are three places where Require exclusive hotkey refuses a slot.

| Where | Today |
|---|---|
| `ComputeAcceptedExclusive`, keyboard initial check (`:192`) | silent |
| `ComputeAcceptedExclusive`, gamepad initial check (`:209`) | silent |
| Pending press cancelled (`:80`) | `pending CANCELLED (no longer exclusive)`, without saying which key |

**Decision**:
- **Helper**: add `FindExtraHeldCode(combo, down, isAllowedExtra)` to `include/Input/HotkeyMatcher.h`. It returns the
  first held code that blocks exclusivity, or -1. `ComboExclusiveNow` becomes `AnyEnabled(combo) &&
  FindExtraHeldCode(...) < 0`, so there is one implementation.
- **Initial checks**: when the keyboard or gamepad check fails, log
  `[Input] ComputeAcceptedExclusive: slot=<n> <kb|gp> REFUSED (not exclusive) extra=<label>`.
  - Only on the frame the combo becomes held (the rising edge of `kbNow` / `gpNow`), so a combo held next to an
    extra key logs once, not every frame.
  - The previous raw state is read before `excl.prevRawKbDown[s]` / `prevRawGpDown[s]` are overwritten at the top
    of the function.
- **Pending cancel**: extend the existing line with `extra=<label>`.
- **Label format**: `kb:0x<scan>` for keyboard, `mouse:<n>` for mouse (code minus 256), `gp:<n>` for gamepad.

**Rationale**: the original bug produced no log line at all, so the reports could not be diagnosed from a log. With
this change a "hotkey does nothing" report shows the culprit directly (SC-007).

---

## R8. Require exclusive hotkey OFF (Clarification Q3)

**Finding**: with the patch OFF, no code path reads a held code outside a combo.
- `AnyComboKeyDown` and `ComboDown` only look at combo codes.
- The HUD toggle only looks at its own combo.
- The Press both at the same time rule only uses combo codes and timing.
- The stuck wheel therefore has no visible effect with the patch OFF, before or after this fix.

**Decision**: no extra code for the patch-OFF case. SC-008 (TEST_MATRIX 6.13) repeats the first report's steps with
the patch OFF. If it fails, there is a second cause, which goes to a separate spec (Clarification Q3).

---

## R9. In-game validation build (Constitution VI, Clarification Q2)

**Decision**: in-game validation runs on the **release** DLL.
- The DEBUG DLL fails to load. SKSE reports "fatal error occurred while loading plugin" and the log stops after
  `[StyleConfig] styles.ini carregado.`; see spec 002.
- The release DLL writes the same `[Input]` lines, because `include/PCH.h` defines `DEBUG` unconditionally. That
  define itself violates Principle VI ("Debug logging MUST compile out of release builds"). It is existing debt
  that this validation relies on, recorded in the plan's Complexity Tracking. The DEBUG-build spec has to fix the
  load failure and this define together.
- The deviation is recorded in the plan's Complexity Tracking.
- Fixing the DEBUG load failure goes to a separate spec.
- **Build order**: debug first and release last, so MO2 keeps the release DLL. The post-build step also copies the
  debug PDB into MO2, so it has to be deleted after the release build.

---

## R10. Threading and invariants

- Every change runs inside `PollInputDevicesHook` → `InputController::ProcessAndFilter` on the main thread. That
  covers `ProcessButtonEvents`, `FilterEvents`, `ReconcilePhysicalKeyState` and `ComputeAcceptedExclusive`
  (Constitution II).
- `KeyStateStore` is still written only from that path (CLAUDE.md invariant).
- The new heartbeat timestamps are read and written only there, so plain integers are enough. The `atomic_bool`
  arrays stay the only state other threads read (the HUD).
- The timestamps and the two function-local log-once guards are input-layer state outside the structs that
  Principle II lists. They are not session flags and need no reset on exit. The deviation is justified in the
  plan's Complexity Tracking.
- No `Domain/` or `Application/` changes, and no change to the spell state machine (Constitution I and IV are not
  affected).
