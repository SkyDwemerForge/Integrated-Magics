# Research: Fix the SKSE menu turning transparent when capturing hotkeys

All findings come from the code on `fix/menu-capture-transparency` (`main` at `c301462`).

## R1. Mechanism

**Facts (code)**:
- **`DrawKeyValueCapture`** (`src/UI/MENU.cpp:75`) draws one hotkey field: a numeric input, the capture button, and
  for slot rows the modifier "M" button. `DrawInputDetailRow` calls it for every keyboard and gamepad field of every
  slot and of the HUD popup (`MENU.cpp:177`, `:197`).
- **The defective pair**: when the field is not the one being captured, the capture button is drawn between two
  checks of the same mutable global (`MENU.cpp:108–114`):

  ```text
  if (g_fieldCapture.active) BeginDisabled(true);   // false before the click
  if (SmallButton("Cap")) { g_fieldCapture = {&field, wantKeyboard, true}; ... }  // the click sets active = true
  if (g_fieldCapture.active) EndDisabled();         // true now → End without Begin
  ```

- **One unbalanced `EndDisabled` per started capture**: other buttons are disabled while a capture runs, so a click
  that starts one always comes from an idle state. That matches "more clicks, more fading".
- **The other transitions stay balanced**: a capture that completes or is cancelled clears `g_fieldCapture` inside the
  `isThis` branch, which has no disabled pair. The fields drawn after it in the same frame see the new value in both
  checks.
- **Shared context**: `ImGuiMCP` is the ImGui API of SKSE Menu Framework. The plugin's page is drawn into that shared
  context, so a broken disabled stack can carry over to the whole SKSE menu (spec Background item 4).

**What ImGui does with it**:
- `BeginDisabled(bool)` always pushes one entry onto the item-flag stack and increments `DisabledStackSize`, even when
  its argument is `false`. When it starts a disabled section, it also saves `Style.Alpha` and multiplies it by
  `DisabledAlpha`.
- `EndDisabled()` pops one entry and restores the saved alpha when it leaves a disabled section.
- An `EndDisabled` with no matching `BeginDisabled` pops an entry that belongs to an outer scope, and the alpha
  bookkeeping goes out of step.
- The assertion that would catch it (`DisabledStackSize > 0`) is compiled out of SKSE Menu Framework's release ImGui.
  The visible result is the report's progressive fading. The exact fading path depends on that ImGui version; it is
  not needed for the fix.

**Decision**: the defect is the pair at `MENU.cpp:108–114` deciding its start and its end from a value that the
button between them can change.

## R2. The fix

**Decision**: draw the capture button inside an **unconditional** pair:

```text
BeginDisabled(g_fieldCapture.active);
if (SmallButton("Cap")) { ... }      // unchanged
EndDisabled();
```

**Rationale**:
- `BeginDisabled(false)` still pushes, so `EndDisabled()` is always matched, whatever the click changes.
- The look is the same: disabled while a capture runs, enabled otherwise.
- It is the idiom the two other disabled buttons of the menu already use: "- Vertex" (`MENU.cpp:530–536`) and "Apply
  changes" (`MENU.cpp:1428–1437`). The file ends up with one pattern for disabled buttons.

**Alternatives considered**:
- *The reporter's pattern*: `const bool busy = g_fieldCapture.active;`, then `if (busy) BeginDisabled(true);` …
  `if (busy) EndDisabled();`. Correct as well. Rejected only because it keeps two conditional calls that a later
  edit could split again, while the unconditional pair cannot go out of balance.
- *Disable through a style color instead*: rejected. It changes the look and needs its own push/pop pair.

## R3. Audit of the rest of the UI (spec FR-003)

| Location | Pair | Decided from | Balanced |
|---|---|---|---|
| `MENU.cpp:108–114` | `BeginDisabled` / `EndDisabled` | `g_fieldCapture.active`, read twice, changed by the button | **No** (R1) |
| `MENU.cpp:530–536` ("- Vertex") | `BeginDisabled(cond)` / `EndDisabled()` | unconditional pair | Yes |
| `MENU.cpp:1428–1437` ("Apply changes") | `BeginDisabled(!g_pending)` / `EndDisabled()` | unconditional pair | Yes |
| `MENU.cpp:121 / 161` (modifier "M") | `PushStyleColor` / `PopStyleColor` | `const bool isMod`, read before the button | Yes |
| `MENU.cpp:446 / 451` (HUD anchor grid) | `PushStyleColor` / `PopStyleColor` | `const bool active` | Yes |
| `MENU.cpp:1076 / 1081`, `1104 / 1110`, `1114 / 1120` (label corner) | `PushStyleColor` / `PopStyleColor` | `const bool active` / `tc` / `ac` | Yes |
| `MENU.cpp:77 / 164`, `122 / 160` | `PushID` / `PopID` | unconditional, no early return in between | Yes |
| `HudManager.cpp:20–24`, `Popupdrawer.cpp:344–474` | `PushStyleVar` / `PopStyleVar(2)` | unconditional; the `return`s in between belong to helper lambdas | Yes; these use the plugin's own ImGui context, not the SKSE menu's |

**Decision**: one fix, at `MENU.cpp:108–114`. The other pairs stay as they are.

## R4. Validation

- **DEBUG build** (Constitution VI): Windows Smart App Control, which blocked the DEBUG DLL in specs 002–005, is off
  since 2026-11-20 (`VerifiedAndReputablePolicyState = 0`). So the DEBUG DLL is expected to load. If `skse64.log` still
  shows `couldn't load plugin 4551`, fall back to the release DLL; that needs the user's approval.
- **No automated harness**. The fix is one balanced pair in ImGui code, checked in game by repeated captures
  (quickstart).
- **Log**: no new log line. Drawing is not a state transition, and the capture already has its own flow.

## R5. TEST_MATRIX impact

- **New case 10.9**: 20 consecutive captures (keyboard and gamepad, completed and cancelled) leave the menu fully
  opaque, and another mod's SKSE menu page looks normal.
- **New case 10.10**: "- Vertex", "Apply changes", "M", HUD anchor and label corner, used 10 times each, never fade the
  menu.
- **Regression**: 10.7 (hotkey rebinding) and 6.12 (the wheel ignored during capture).
