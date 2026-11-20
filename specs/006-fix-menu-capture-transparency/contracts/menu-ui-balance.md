# Contract: balanced ImGui state on the plugin's SKSE menu page

The plugin's settings page is drawn into SKSE Menu Framework's shared ImGui context. This contract is the rule the
page follows, so it never leaves that context out of balance (spec FR-003, FR-004). It is the reference for
TEST_MATRIX 10.9 and 10.10.

## 1. Pair rule

| Pair | Rule |
|---|---|
| `BeginDisabled` / `EndDisabled` | Always called as an **unconditional** pair: `BeginDisabled(condition)` … `EndDisabled()`. Never guard the two calls with separate `if`s |
| `PushStyleColor` / `PopStyleColor`, `PushStyleVar` / `PopStyleVar` | When conditional, both calls test the **same `const` local** computed before the control. The control's click must never change that local |
| `PushID` / `PopID` | Unconditional, with no early `return` between them |

A click inside a pair may change any state, because the pair's end never re-reads it.

## 2. Capture button

- Disabled while any capture is in progress (`g_fieldCapture.active`), enabled otherwise, as today.
- Starting, completing or cancelling a capture never changes the opacity or look of the page or of the SKSE menu.

## 3. Unchanged

The capture flow (`CaptureState` request and poll), device matching, the modifier "M" button, the HUD, the assignment
popup, settings and INI keys.
