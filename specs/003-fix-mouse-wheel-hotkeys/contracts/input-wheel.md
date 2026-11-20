# Contract: mouse wheel and held-input handling

This contract defines what the plugin does with mouse wheel input, which inputs the stuck-key recovery covers, and
the diagnostic lines it writes. It is the reference for the TEST_MATRIX section 6 cases (FR-010) and the
`.claude/CLAUDE.md` Input system section.

## 1. Mouse wheel (`kWheelUp` = mouse id 8, `kWheelDown` = mouse id 9)

| Concern | Behavior | Requirement |
|---|---|---|
| Held-input record | Never recorded as held, in any frame | FR-001 |
| Slot hotkey matching | Never part of a completed combo | FR-002, Clarification Q1 |
| Require exclusive hotkey | Never counts as an extra key | FR-003 |
| Event delivery | Always passed to the game and other mods unchanged, never withheld, retained or replayed. **Exception (unchanged)**: while a hotkey capture is waiting, every button event is withheld, the wheel included | FR-004 |
| Hotkey capture (rebinding) | Never captured, in either the game-event path or the UI paths | FR-005 |
| Hand-edited combo with a wheel code | Kept as written; it can never complete; one warning per slot when the hotkeys load | Spec edge case, research R5 |
| Patch settings | No flag, default or INI key changes | FR-008 |

## 2. Stuck-key recovery coverage

Recovery runs every 200 ms while no slot session and no exclusive press is pending. It also runs on menu close
(`ClearLikelyStuckKeysAfterMenuClose`), which is unchanged.

| Input | How a lost release is detected | Change |
|---|---|---|
| Keyboard | Windows key state (`GetAsyncKeyState`) | unchanged |
| Mouse ids 0–4 | Windows key state (`VK_LBUTTON` … `VK_XBUTTON2`) | unchanged |
| Mouse ids 5–7 | **Heartbeat**: no pressed event for more than 500 ms | **new** (FR-007) |
| Mouse ids 8–9 (wheel) | Not needed: never recorded | **new** (FR-001) |
| Gamepad buttons and triggers | XInput state | unchanged |

Worst case from a lost release to a cleared key: about 0.7 s (500 ms timeout plus one 200 ms pass), which meets
FR-007's 1 s.

## 3. Require exclusive hotkey: refusal reporting

A slot refused by the exclusive rule is logged with the input that caused it. The log fires only on the frame the
slot's combo becomes held, so a combo held next to an extra key logs once.

## 4. Diagnostic lines (FR-009, Constitution VI)

These are `MAGIC_DEBUG_LOG` lines. They are also written by the release build, because `PCH.h` defines `DEBUG`.

```text
[Input] ComputeAcceptedExclusive: slot=<n> kb REFUSED (not exclusive) extra=<label>
[Input] ComputeAcceptedExclusive: slot=<n> gp REFUSED (not exclusive) extra=<label>
[Input] ComputeAcceptedExclusive: slot=<n> pending CANCELLED (no longer exclusive) extra=<label>
[Input] ProcessButtonEvents: mouse wheel ignored for hotkeys (id=<8|9> value=<v> heldSecs=<s>)   (once per game session)
[Input] ProcessButtonEvents: first held mouse event seen (id=<n> heldSecs=<s>)                     (once per game session; heartbeat premise, R6)
[Input] HotkeyCache: slot=<n> combo contains the mouse wheel (code=<c>), not supported; this combo cannot activate
[Input] ReconcilePhysicalKeyState: cleared stuck keys                                          (existing line)
```

`<label>` is `kb:0x<scan>` for keyboard, `mouse:<id>` for mouse, and `gp:<index>` for gamepad.
