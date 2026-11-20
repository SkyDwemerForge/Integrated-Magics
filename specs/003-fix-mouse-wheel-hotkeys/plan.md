# Implementation Plan: Fix slot hotkeys that stop working after using the mouse wheel

**Branch**: `fix/mouse-wheel-hotkeys` | **Date**: 2026-11-19 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/003-fix-mouse-wheel-hotkeys/spec.md`

## Summary

A wheel notch reaches the plugin as a mouse "button pressed" event with no "released" event. The input layer
records it as a key held forever, and the stuck-key recovery never checks it. With Require exclusive hotkey ON, that
phantom key makes every slot fail the exclusivity test, silently, until a menu closes and clears it (research R1).

The fix works at the input boundary:

1. **Ignore the wheel for hotkeys.** `ProcessButtonEvents` skips wheel events before hotkey capture and before any
   held-input write, so the wheel is never recorded and never captured (R2, R4).
2. **Always pass the wheel to the game.** `FilterEvents` never withholds a wheel event, even for a hand-edited combo
   (R3, R5).
3. **Cover the remaining blind spot of the stuck-key recovery.** Mouse buttons 5–7 have no Windows key state, so a
   500 ms heartbeat clears them when their release is lost (R6).
4. **Make exclusive refusals diagnosable.** Log the input that blocked the slot, once per press, on the paths that
   are silent today (R7).

The patch-OFF case needs no code; SC-008 checks it (R8). In-game validation runs on the release DLL (R9,
Complexity Tracking).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: CommonLibSSE-NG v16.9.0 (`RE::ButtonEvent`, `RE::BSWin32MouseDevice::Keys`,
`RE::INPUT_DEVICE`), Win32 (`GetAsyncKeyState`, `MapVirtualKeyA`), XInput, spdlog 1.17

**Storage**: N/A. No INI, hotkey encoding or persistence changes.

**Testing**: no automated harness. The new predicates are pure (no `RE::` types) and could be unit-tested if a
harness is added. Validation uses the TEST_MATRIX cases in [quickstart.md](quickstart.md) on the release DLL
(Constitution VI deviation, see Complexity Tracking).

**Target Platform**: Windows x64, Skyrim SE / AE via Address Library

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**: no measurable per-frame cost.
- One integer comparison per mouse event.
- A three-entry timestamp check inside the existing 200 ms recovery pass.
- Refusal logs only on the frame a combo becomes held.

**Constraints**:
- Main thread only (Constitution II).
- `KeyStateStore` is written only from the `PollInputDevicesHook` path.
- Wheel events must reach the game unchanged (FR-004).
- No patch flag or INI changes (Constitution V).

**Scale/Scope**: 7 source/header files in `include/Input`, `include/Shared` and `src/Input`, plus `docs/TEST_MATRIX.md`
and `.claude/CLAUDE.md`. No `Domain/` or `Application/` change.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle / gate | Assessment | Status |
|---|---|---|
| **I. Layered architecture** | All changes stay in the input layer (`Input::`, `Shared/InputConstants.h`). There is no domain logic, and no new game write; the wheel skip only stops a plugin-side write. | ✅ Pass |
| **II. Main-thread state machine integrity** | Every touched function runs inside `PollInputDevicesHook` → `ProcessAndFilter`, and `KeyStateStore` is still written only from that path. No new flags in `MagicState`. **But** the new state lives outside the structs this principle lists: the heartbeat timestamps in `KeyStateStore` and the one-time log guards are function-local `static` values. See Complexity Tracking. | ⚠️ Deviation (justified) |
| **III. Never strand the player** | Improves it: a scroll can no longer leave every hotkey dead until a menu is opened, and lost releases on mouse buttons 5–7 are now recovered. | ✅ Pass (improves) |
| **IV. Dual-cast synchronization** | Not touched. No change to `MagicState`, synthetic attack dispatch or the pump. | ✅ Pass |
| **V. Opt-in & compatibility** | No patch setting, default or INI key changes. The wheel keeps reaching the game and other mods. A hand-edited wheel combo is kept as written and only logged. | ✅ Pass |
| **VI. Observable & verified** | Adds `[Input]` lines for refusals (with the culprit), the wheel skip, the first held mouse event and hand-edited wheel combos. Adds TEST_MATRIX cases 6.8–6.14. Two deviations, both in Complexity Tracking. (a) In-game validation runs on the **release** DLL, because the DEBUG DLL fails to load. (b) That validation relies on debug logs being present in release builds, which this principle forbids (existing `#define DEBUG` in `include/PCH.h`). | ⚠️ Deviation (accepted, Clarification Q2 + known debt) |

**Post-design re-check (after Phase 1, revised after `/speckit-analyze` C1/C2)**:
- **Design**: the data model adds one main-thread array to `KeyStateStore` and pure helpers in
  `Shared/InputConstants.h`. The contract keeps wheel delivery to the game unchanged.
- **Principle II**: that array and the one-time log guards fall outside the state structs Principle II lists, so
  they are justified in Complexity Tracking.
- **Principle VI**: its log-stripping rule is already violated by `include/PCH.h`, and the release-build
  validation depends on that. It is also recorded there.

## Project Structure

### Documentation (this feature)

```text
specs/003-fix-mouse-wheel-hotkeys/
├── spec.md               # Feature spec (with Clarifications 2026-11-19)
├── plan.md               # This file
├── research.md           # Phase 0: R1–R10
├── data-model.md         # Phase 1: constants, KeyStateStore rule + heartbeat field
├── quickstart.md         # Phase 1: in-game validation guide
├── contracts/
│   └── input-wheel.md    # Phase 1: wheel behavior, recovery coverage, log lines
├── checklists/
│   └── requirements.md   # Spec quality checklist
└── tasks.md              # Phase 2 (/speckit-tasks, not created here)
```

### Source Code (repository root)

```text
include/
├── Shared/
│   └── InputConstants.h          # + wheel / no-VK mouse ids, heartbeat timeout, IsMouseWheelId, IsMouseNoVkId (R2, R6)
└── Input/
    ├── KeyStateStore.h           # + mouseNoVkLastPressedMs (R6)
    └── HotkeyMatcher.h           # + FindExtraHeldCode; ComboExclusiveNow delegates to it (R7)

src/
└── Input/
    ├── InputFilter.cpp           # ProcessButtonEvents: skip wheel before capture/state, one-time log,
    │                             #   heartbeat for mouse ids 5–7 (R2, R4, R6)
    │                             # FilterEvents: never filter wheel events (R3)
    ├── PhysicalReconciler.cpp    # clear mouse ids 5–7 with a stale heartbeat (R6)
    ├── ExclusiveTracker.cpp      # refusal logs with the culprit on the rising edge; culprit on pending cancel (R7)
    └── HotkeyMatcher.cpp         # LoadHotkeyCache_FromConfig: warn on a wheel code in a combo (R5)

docs/TEST_MATRIX.md               # section 6: new cases 6.8–6.14 (FR-010)
.claude/CLAUDE.md                 # Input system: wheel rule, recovery coverage, refusal log (Constitution II docs)
```

**Structure Decision**: single-project SKSE plugin. The fix stays in the existing input layer, at the point where
raw game input becomes plugin state (`ProcessButtonEvents`) and where events are filtered (`FilterEvents`), plus
the two existing consumers that need adjusting: stuck-key recovery and the exclusive rule. No new files.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **Principle VI**: in-game validation runs on the **release** DLL, not on a DEBUG build. Accepted by the user for this spec on 2026-11-19 (Clarification Q2). | The DEBUG DLL fails to load. SKSE reports "fatal error occurred while loading plugin", and the log stops after `[StyleConfig] styles.ini carregado.`; see spec 002. The release DLL writes the same `[Input]` lines (next row), so the evidence the principle asks for is still produced. | Fixing the DEBUG load failure first: it is unrelated to input handling, its size is unknown, and it goes to its own spec (Clarification Q2). |
| **Principle VI**: "Debug logging MUST compile out of release builds" is already violated: `include/PCH.h` defines `DEBUG` unconditionally, so every `MAGIC_DEBUG_LOG` ships in release. This feature does not introduce it, but it **relies** on it: the release-build validation (previous row) only produces logs because of it, and the new `[Input]` lines will ship in release too. | It is the only way to get log evidence while the DEBUG DLL cannot load. The new lines are low-volume (once per press, once per session or once per hotkey load), so shipping them costs nothing measurable. | Making `DEBUG` conditional now: that would remove the only log evidence available for validation. It belongs to the separate DEBUG-build spec (Clarification Q2), which must fix the load failure and this define **together**. **Dependency for that spec**: once `DEBUG` is conditional, release builds stop producing these logs, and validation has to move back to the DEBUG build. |
| **Principle II**: "New state MUST live in the existing structs (`SessionState`, `HandMode`, `RestoreContext`, `ShoutState`, `AutoAttackState`)". This feature adds state outside them: `KeyStateStore::mouseNoVkLastPressedMs` (research R6) and two function-local `static bool` log guards (`s_wheelLogged` in T006, `s_heldLogged` in T015). | It is input-layer data: physical-input timestamps and log-once guards, not session or state-machine flags. It cannot leak a session into the next, because no session logic reads it, which is the defect the principle exists to prevent. Its natural owner is `KeyStateStore`, which Principle II already governs (written only from `PollInputDevicesHook`). | Putting it in `HandMode` or `SessionState`: that would place input-layer data in `Domain/` and break Principle I's layering. Resetting it on session exit is not needed: timestamps refresh on every press and the guards only limit logging. **Follow-up (optional)**: a PATCH clarification of the constitution, "New **MagicState** state MUST…", via `/speckit-constitution`. |
