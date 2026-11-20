# Contract: shared buttons, held-back input and overlapping combos

This contract defines how the plugin decides whether a button reaches the game when slot combos share buttons, and
which slot fires when combos overlap. It is the reference for the TEST_MATRIX cases (FR-008) and the
`.claude/CLAUDE.md` Input system section.

## 1. Candidates and filtering

For a button event (press, release, or held repeat), the **candidates** are every active slot whose combo contains
the button on that device (keyboard/mouse combo or gamepad combo).

| Situation | Outcome | Requirement |
|---|---|---|
| No candidate | Passes to the game immediately (unchanged) | FR-003 |
| **Held repeat or release** of a button | Follows the fate of that button's own press: consumed, passed, or held back with it | FR-010 |
| Fresh press, and any candidate is active (`slotDown`) or has its combo fully held | **Filtered** (fate consumed): the game never sees it | FR-001 |
| Fresh press that is a replay of a held-back event | Passes to the game (fate passed) | FR-003 |
| Otherwise (fresh press) | **Held back** under one owner candidate (most of its combo held, then longest combo, then lowest number) | FR-002 |

- The slot number never decides whether a candidate can filter an event (FR-004).
- An active session does **not** block new presses of its combo's buttons. Only presses that build a combo are
  consumed (FR-010). For example, `RT` still casts manually during a Press session.

## 2. Fate of a held-back button

| Fate | When | Requirement |
|---|---|---|
| **Consumed** (dropped) | Any slot whose combo contains the button activates while it is held back, or is queued for replay | FR-001, FR-002 |
| **Window extended (pending)** | The 50 ms window ends while a slot containing the button is still pending. Re-armed up to 0.5 s since it was first held | FR-001 |
| **Window extended (chord / complete)** | The 50 ms window ends while some candidate combo has 2 or more of its buttons held: a chord in progress (`chord`), or a combo completed in this frame whose slot goes pending later in the frame (`complete`). Re-armed up to 100 ms since it was first held, counting the frame that crosses it | FR-001 |
| **Handed to the game** | The window ends with neither of the above, or a cap is reached | FR-003 |

- A button pressed alone, with no combo in progress, reaches the game after 50 ms (40 ms before the 2026-11-20
  tuning; FR-003, SC-003).
- Its held repeats and release then follow it to the game (FR-010).
- **Combo tolerance** (spec FR-001, `kComboToleranceSec` = 50 ms): the second button within 50 ms of the first, and a
  3-button combo within 100 ms. A multi-button slot activates 50 ms after its full combo is held.

## 3. Overlapping and identical combos (FR-009, Clarification 2026-11-19)

| Case | Winner | Loser |
|---|---|---|
| Combo A strictly inside combo B, both complete at the same moment (same frame, or B still pending), i.e. B's extra button within the 50 ms A waits before it activates | B (longer) | A: does not activate; suppressed until its combo is released |
| A completed **before** B's extra button was pressed | A (already fired) | B may still fire later, as a normal second press (existing overwrite behavior) |
| Identical combos complete together | Lower slot number | Higher slot number: suppressed until release |
| Identical combos in the config | — | Warning at every hotkey load |

## 4. Unchanged

- Require exclusive hotkey and Press both at the same time keep their rules (FR-006).
- Wheel handling from spec 003, the HUD toggle filter, capture mode and menu blocking are unchanged.
- No INI or setting changes.

## 5. Diagnostic lines (FR-007, Constitution VI)

These are `MAGIC_DEBUG_LOG` lines. The release build also writes them (`PCH.h` defines `DEBUG`). None is written for
per-frame held repeats.

```text
[Input] HoldBack: <label> held for slot=<owner> (candidates=<n>)
[Input] HoldBack: <label> consumed by slot=<winner>
[Input] HoldBack: <label> handed to game (slot=<owner>)
[Input] HoldBack: slot=<s> window extended (slot=<t> pending|chord|complete)
[Input] RecomputeSlotEdges: slot=<a> SUPPRESSED by slot=<b> (longer combo|identical combo)
[Input] HotkeyCache: slots <a> and <b> have the same <kb|gp> combo; slot <a> fires
```

`<label>` is `kb:0x<scan>`, `mouse:<id>` or `gp:<index>`. Slot numbers in logs are 0-based (slot 1 = `slot=0`).
