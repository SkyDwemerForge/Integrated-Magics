# Data Model: Fix the SKSE menu turning transparent when capturing hotkeys

No persisted data, settings, INI keys or runtime state change.

## Existing state involved (unchanged)

| State | Where | Meaning |
|---|---|---|
| `g_fieldCapture` (`active`, `field`, `wantKeyboard`) | `src/UI/MENU.cpp` | The hotkey field being captured, if any. While `active`, the other fields' capture buttons are shown disabled |
| `CaptureState` (`captureRequested`, `captureActive`, `capturedEncoded`) | `include/Shared/CaptureState.h` | The input layer's capture of the next key; `MENU.cpp` requests it and polls it |

## Invariant restored (FR-001, FR-003, FR-004)

Every `BeginDisabled` the settings page issues in a frame is matched by exactly one `EndDisabled` in the same frame,
whatever the controls in between do. The same holds for `PushStyleColor` / `PopStyleColor` and `PushID` / `PopID`.
After `MENU::DrawSettings` returns, the shared SKSE menu's ImGui stacks are as deep as they were before it was called.

## Capture-button states (unchanged behavior, research R2)

```text
idle (g_fieldCapture.active == false)
  └─ every field's "Cap" enabled
       └─ click "Cap" on field F → g_fieldCapture = {F, kb|gp, active}  (the pair around the button stays balanced)
capturing F
  ├─ F shows "..." and "X"; every other field's "Cap" disabled
  ├─ key of the wanted device pressed → value stored, back to idle
  ├─ key of the other device pressed → capture requested again, still capturing F
  └─ "X" clicked or the menu closed → back to idle
```
