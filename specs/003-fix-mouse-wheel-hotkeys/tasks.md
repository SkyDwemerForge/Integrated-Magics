---

description: "Task list for fixing slot hotkeys that stop working after using the mouse wheel"
---

# Tasks: Fix slot hotkeys that stop working after using the mouse wheel

**Input**: Design documents from `specs/003-fix-mouse-wheel-hotkeys/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md),
[contracts/input-wheel.md](contracts/input-wheel.md), [quickstart.md](quickstart.md)

**Tests**: there is no automated test harness (Constitution VI). Validation is the in-game TEST_MATRIX cases from
[quickstart.md](quickstart.md). Tasks marked **(in-game)** are done by the user, on the **release** DLL (Clarification
Q2, plan Complexity Tracking).

**Organization**: tasks are grouped by user story. Because the root-cause change (T005) also removes the wheel from
US2's and part of US3's paths, the US2 phase is validation only. FR-007 (mouse buttons 5–7) is a cross-cutting
phase without a story label.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1, US2 or US3 from spec.md
- Paths are relative to the repository root.

## Build procedure (referenced by the build tasks)

Run it in a Visual Studio 2026 developer shell. `Launch-VsDevShell.ps1` comes from
`D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools`, run with `-Arch amd64 -HostArch amd64`.

1. `cmake --preset debug` and `cmake --build build/debug`
2. `cmake --preset release` and `cmake --build build/release`. Release goes **last**, so the post-build step leaves
   the release DLL in MO2 (research R9).
3. Delete `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins\IntegratedMagic.pdb`, the debug PDB that the debug
   build copies there.
4. Confirm that the MO2 DLL hash equals `build\release\IntegratedMagic.dll`.
5. **Expected**: 0 errors and no new warnings. The `vcpkg builtin-baseline` warning already exists.

---

## Phase 1: Setup

**Purpose**: test environment and acceptance cases before code changes.

- [ ] T001 **(in-game, optional baseline)** Prepare the test setup from [quickstart.md](quickstart.md) Prerequisites:
  - slots 1–4 on keys `1`–`4`,
  - `Shift + 5` multi-key slot,
  - a Hold-mode Flames slot on `6`,
  - QuickLoot IE enabled.

  On the **current** build, with Require exclusive hotkey ON, zoom the camera and press `1`. Confirm that the slot
  does not activate and that the log has **no** line explaining why. That silence is the gap R7 closes.
- [X] T002 [P] Add TEST_MATRIX cases 6.8–6.14 to `docs/TEST_MATRIX.md` section `## 6. Patches de input`, after 6.7,
  in Portuguese like the rest of the file:
  - **6.8** `requireExclusiveHotkey` ON: zoom da câmera com a roda → hotkey do slot ativa no 1º toque
  - **6.9** `requireExclusiveHotkey` ON: scroll no QuickLoot IE (>1 item) → hotkey ativa
  - **6.10** `requireExclusiveHotkey` ON: vários scrolls nos dois sentidos + alternar 1ª/3ª pessoa → todos os slots
    ativam, sem abrir menu
  - **6.11** `requireExclusiveHotkey` ON: segurar slot Hold e rolar a roda → o cast continua até soltar a tecla. Rolar
    entre e durante o aperto de `Shift + 5` → o slot ativa
  - **6.12** Captura de hotkey: rolar a roda durante a captura → a roda não é gravada
  - **6.13** `requireExclusiveHotkey` OFF: passos do 1º relato (zoom, alternar 1ª/3ª pessoa, teclas 1–4) → 10/10
    ativam. Falha = segunda causa, abrir spec separada
  - **6.14** `requireExclusiveHotkey` ON: segurar tecla fora do combo (ex.: `G`) e apertar o slot → não ativa, e o
    log mostra `REFUSED (not exclusive) extra=kb:0x22` uma vez

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: shared constants and the refusal diagnostics that every story's validation reads (FR-009).

**⚠️ CRITICAL**: US1 validation (T008) checks for the **absence** of wheel refusal lines, so T005 must be in the
build (T007) first.

- [X] T003 [P] In `include/Shared/InputConstants.h`, add the mouse id constants and pure helpers from
  [data-model.md](data-model.md). Use no `RE::` types and no new includes:
  - `inline constexpr int kMouseWheelUpId = 8;` and `inline constexpr int kMouseWheelDownId = 9;`
    (`RE::BSWin32MouseDevice::Keys::kWheelUp` / `kWheelDown`)
  - `inline constexpr int kMouseFirstNoVkId = 5;` and `inline constexpr int kMouseLastNoVkId = 7;`
    ("first/last mouse button with no Windows virtual key")
  - `inline constexpr int kMouseHeartbeatTimeoutMs = 500;`
  - `[[nodiscard]] constexpr bool IsMouseWheelId(int id) noexcept`, returning `id == kMouseWheelUpId || id ==
    kMouseWheelDownId`
  - `[[nodiscard]] constexpr bool IsMouseNoVkId(int id) noexcept`, returning `id >= kMouseFirstNoVkId && id <=
    kMouseLastNoVkId`
  - A one-line comment above the block: mouse ids are stored as `kMouseButtonBase + id`.
- [X] T004 In `include/Input/HotkeyMatcher.h`, add `template <class DownArr, class AllowedFn> [[nodiscard]] int
  FindExtraHeldCode(const std::array<int, 3>& combo, const DownArr& down, AllowedFn isAllowedExtra)`. It returns the
  first code `0..kMaxCode-1` that is held, not in `combo` and not `isAllowedExtra(code)`, or `-1` if there is none.
  Rewrite `ComboExclusiveNow` as `return AnyEnabled(combo) && FindExtraHeldCode(combo, down, isAllowedExtra) < 0;`
  so its behavior does not change (research R7).
- [X] T005 In `src/Input/ExclusiveTracker.cpp`, add the refusal diagnostics from contract §3–§4 (FR-009, research
  R7). Depends on T004.
  - **Label helper**: in the anonymous namespace, add `[[maybe_unused]] std::string DescribeHeldCode(int code, bool
    gamepad)`. It returns `std::format("gp:{}", code)` for gamepad, `std::format("mouse:{}", code -
    kMouseButtonBase)` when `code >= kMouseButtonBase`, and `std::format("kb:{:#04x}", code)` otherwise. Add
    `#include <format>` and `#include <string>`; the project uses `std::format`, as in `src/Config/Config.cpp`.
  - **Rising edge**: in `ComputeAcceptedExclusive`, **before** the existing `excl.prevRawKbDown[s] = kbNow;` /
    `excl.prevRawGpDown[s] = gpNow;` lines, compute `[[maybe_unused]] const bool kbRose = kbNow &&
    !excl.prevRawKbDown[s];` and the same `gpRose`.
  - **Pending press**: replace the `stillExcl` computation in the pending branch. Compute `extra` with
    `FindExtraHeldCode` on the pending source's combo, down array and allowed-extra function, then `stillExcl =
    AnyEnabled(combo) && extra < 0`. Append `extra={}`, preceded by a space, to the existing `pending CANCELLED (no
    longer exclusive)` line, with `extra >= 0 ? DescribeHeldCode(extra, src == PendingSrc::Gp) : "none"`.
  - **Keyboard initial check** (the `if (kbNow)` block): when `requireExcl` is ON and the combo is not exclusive,
    log once with `if (kbRose)`:
    `MAGIC_DEBUG_LOG("[Input] ComputeAcceptedExclusive: slot={} kb REFUSED (not exclusive) extra={}", slot,
    DescribeHeldCode(FindExtraHeldCode(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera), false));`
    Then fall through to the gamepad check exactly as today.
  - **Gamepad initial check**: the same in the `if (gpNow)` block, with `gpRose`, `hk.gp`, `keys.gpDown`,
    `IsAllowedExtra_Gamepad_MoveOrCamera` and the label `gp REFUSED`.
  - **Unchanged**: the return values. Diagnostics only.

**Checkpoint**: builds with no behavior change. Exclusive refusals now name their culprit.

---

## Phase 3: User Story 1 - Hotkeys keep working after scrolling with Require exclusive hotkey ON (Priority: P1) 🎯 MVP

**Goal**: a scroll never leaves a phantom held key, so the first hotkey press after scrolling activates its slot
(FR-001, FR-002).

**Independent Test**: [quickstart.md](quickstart.md) §1. With Require exclusive hotkey ON, scroll and then press a
slot key: the slot activates on the first press, without any menu.

- [X] T006 [US1] In `src/Input/InputFilter.cpp` `ProcessButtonEvents`, add the wheel skip at the top of the
  `if (dev == RE::INPUT_DEVICE::kMouse)` branch, **before** `TryHandleCapture` and the `kbDown` store (research R2,
  R4; FR-001, FR-002, FR-003, FR-005). Depends on T003.

  ```cpp
  if (IsMouseWheelId(code)) {
      // The wheel sends a press with no release: never a held input, never a hotkey (spec 003, research R2).
      static bool s_wheelLogged = false;
      if (!s_wheelLogged) {
          s_wheelLogged = true;
          MAGIC_DEBUG_LOG("[Input] ProcessButtonEvents: mouse wheel ignored for hotkeys (id={} value={:.2f} heldSecs={:.3f})",
                          code, btn->Value(), btn->HeldDuration());
      }
      continue;
  }
  ```

- [X] T007 [US1] Build per the **Build procedure** above. Depends on T003–T006.
- [X] T008 [US1] **(in-game)** Validate [quickstart.md](quickstart.md) §1 (SC-001, SC-002). Depends on T007.
  - **Expected**: 20 out of 20 activations on the first press.
  - **Log**: `mouse wheel ignored for hotkeys` appears once, and no `REFUSED` or `CANCELLED` line has
    `extra=mouse:8` or `extra=mouse:9`.
  - **Afterwards**: tick 6.8, 6.9 and 6.10 in `docs/TEST_MATRIX.md`.

**Checkpoint**: the reported bug is fixed. MVP.

---

## Phase 4: User Story 2 - Scrolling during a hotkey press does not cancel it (Priority: P2)

**Goal**: a scroll while a hotkey is pressed or held never cancels it (FR-003).

**Independent Test**: [quickstart.md](quickstart.md) §2.

No code task: T006 removes the wheel before the held-input record, so the exclusive rule can never see it, not even
for one frame (research R2). This phase validates that.

- [X] T009 [US2] **(in-game)** Validate [quickstart.md](quickstart.md) §2 (SC-003), on the T007 build or a later one.
  - **Expected**:
    - 0 out of 10 Hold casts are cut short by scrolling,
    - no `pending CANCELLED ... extra=mouse:` line,
    - the Press-mode slot deactivates on its second press after a scroll,
    - 5 out of 5 `Shift + 5` activations with scrolls between and during the key presses (quickstart §2 step 4,
      spec US2 scenario 2).
  - **Afterwards**: tick 6.11 in `docs/TEST_MATRIX.md`.

**Checkpoint**: US1 and US2 both hold.

---

## Phase 5: User Story 3 - The wheel keeps working for the game and other mods (Priority: P3)

**Goal**:
- wheel events always reach the game (FR-004),
- capture never records the wheel (FR-005),
- a hand-edited wheel combo is reported (research R5),
- the exclusive rule still refuses real extra keys (FR-006).

**Independent Test**: [quickstart.md](quickstart.md) §3, §4 and §5.

- [X] T010 [US3] In `src/Input/InputFilter.cpp` `FilterEvents`, never filter wheel events (research R3, FR-004).
  Depends on T003, and on T006 because it is the same file.
  - In the `if (const auto* btn = cur->AsButtonEvent())` block, after `code` is computed, add `const bool isWheel =
    dev == RE::INPUT_DEVICE::kMouse && IsMouseWheelId(code);`.
  - Change the guard to `if (!isWheel && code >= 0 && code < kMaxCode)`, so `remove` stays `false` and the event
    passes untouched. This also skips `ShouldFilterHudToggle`.
- [X] T011 [P] [US3] In `src/Input/HotkeyMatcher.cpp` `LoadHotkeyCache_FromConfig`, warn when a combo contains the
  wheel (research R5). Depends on T003.
  - Inside the per-slot loop, after the existing `LoadHotkeyCache` log: for each `c` in `hk.kb` with `c >=
    kMouseButtonBase && IsMouseWheelId(c - kMouseButtonBase)`, log
    `MAGIC_DEBUG_LOG("[Input] HotkeyCache: slot={} combo contains the mouse wheel (code={}), not supported; this combo cannot activate", i, c);`
  - After `cache.hud` is set, do the same for `cache.hud.kb` with `slot=hud`.
  - Do **not** modify the combo (it is kept as written).
- [X] T012 [US3] Build per the **Build procedure**. Depends on T010 and T011.
- [X] T013 [US3] **(in-game)** Validate [quickstart.md](quickstart.md) §3, §4 and §5 (SC-004, SC-005, SC-006,
  SC-007). Depends on T012.
  - **Expected**:
    - camera zoom, view switching and QuickLoot IE scrolling behave as before (10/10 each),
    - holding `G` refuses slot 1 with exactly one `slot=0 kb REFUSED (not exclusive) extra=kb:0x22` line,
    - scrolling during capture never records the wheel (5/5),
    - TEST_MATRIX 6.1–6.7 pass.
  - **Afterwards**: tick 6.12, 6.14 and the regression cases in `docs/TEST_MATRIX.md`.

**Checkpoint**: all three user stories validated.

---

## Phase 6: Cross-cutting - stuck-key recovery for mouse buttons 5–7 (FR-007)

**Purpose**: a lost release on a mouse button that Windows cannot report must not block hotkeys until a menu opens
(research R6, contract §2).

- [X] T014 [P] In `include/Input/KeyStateStore.h`, add the heartbeat state (data-model.md):
  - `#include <cstdint>` and `#include <chrono>`.
  - In `struct KeyStateStore`, add `std::array<std::uint64_t, 3> mouseNoVkLastPressedMs{};` with the comment
    `// Last pressed event (down or held) for mouse ids 5..7, steady-clock ms. Main thread only.`
  - In `namespace Input`, add `[[nodiscard]] inline std::uint64_t SteadyNowMs() noexcept`, returning
    `static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())`.
- [X] T015 In `src/Input/InputFilter.cpp` `ProcessButtonEvents`, record the heartbeat. Depends on T003, T014, and
  on T010 because it is the same file.
  - Split the first check `if (!btn || (!btn->IsDown() && !btn->IsUp())) continue;` into `if (!btn) continue;`, then
    the heartbeat, then `if (!btn->IsDown() && !btn->IsUp()) continue;`.
  - Heartbeat: `if (btn->GetDevice() == RE::INPUT_DEVICE::kMouse) { const int id = static_cast<int>(btn->GetIDCode());
    if (IsMouseNoVkId(id) && btn->IsPressed()) keys.mouseNoVkLastPressedMs[static_cast<std::size_t>(id -
    kMouseFirstNoVkId)] = Input::SteadyNowMs(); }`.
  - A held event updates only the timestamp, never `kbDown` (data-model "Lifecycle").
  - **Premise check** (research R6, `/speckit-analyze` A1): in the same mouse block, log the first held event of
    **any** mouse button, once per game session. Any player can then confirm that held events reach this hook
    without a 6+ button mouse.

    ```cpp
    static bool s_heldLogged = false;
    if (!s_heldLogged && btn->IsHeld()) {
        s_heldLogged = true;
        MAGIC_DEBUG_LOG("[Input] ProcessButtonEvents: first held mouse event seen (id={} heldSecs={:.3f})", id,
                        btn->HeldDuration());
    }
    ```

- [X] T016 In `src/Input/PhysicalReconciler.cpp` `ReconcilePhysicalKeyState`, clear stale buttons. Depends on T014.
  - Replace the local `now` expression with `const auto now = Input::SteadyNowMs();`.
  - After the `kMouseVKMap` loop, add a loop over `id = kMouseFirstNoVkId..kMouseLastNoVkId`. When
    `keys.kbDown[kMouseButtonBase + id]` is set and `now - keys.mouseNoVkLastPressedMs[id - kMouseFirstNoVkId] >
    static_cast<std::uint64_t>(kMouseHeartbeatTimeoutMs)`, store `false` and set `clearedAny = true`.
  - The existing `cleared stuck keys` log and the `ClearEdgeStateOnly` call stay as they are.
- [X] T017 Build per the **Build procedure**. Depends on T015 and T016.
- [X] T018 **(in-game)** Validate [quickstart.md](quickstart.md) §7. Depends on T017.
  - **Premise check (everyone)**, step 1:
    - hold the left or right mouse button for about 2 s (block, or draw a bow),
    - confirm the log shows `first held mouse event seen (id=<0|1> heldSecs=...)`.

    If the line never appears, the heartbeat premise is wrong. Stop and revisit research R6 before shipping T015
    and T016, because held buttons 6–8 would be cleared falsely.
  - **Lost release (only with a 6+ button mouse)**, step 2. Without such a mouse, note "step 2 not testable here".

---

## Phase 7: Polish & cross-cutting concerns

- [X] T019 [P] Update `.claude/CLAUDE.md`, section `## Input system`:
  - **`KeyStateStore`**: the mouse wheel (mouse ids 8/9) is never recorded. It has no release event, and it is not
    a hotkey input. Mention `mouseNoVkLastPressedMs`.
  - **New paragraph "Stuck-key recovery"** (`ReconcilePhysicalKeyState`, every 200 ms while idle):
    - keyboard and mouse ids 0–4 use Windows key state,
    - mouse ids 5–7 use a 500 ms heartbeat,
    - gamepad uses XInput,
    - `ClearLikelyStuckKeysAfterMenuClose` runs on menu close.
  - **`ExclusiveStore` bullet**: refusals log `REFUSED (not exclusive) extra=<label>` once per press. Point to
    `specs/003-fix-mouse-wheel-hotkeys/`.
  - **Common tasks table, row "Hotkey not detected / double-firing"**: add `src/Input/InputFilter.cpp`
    (`ProcessButtonEvents`) and `src/Input/PhysicalReconciler.cpp`.
- [X] T020 Run `clang-format --style=file --dry-run` (x64 binary at
  `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`) on every touched
  source file. Fix only violations in lines this feature changed, and leave existing ones alone. Rebuild if
  anything changed.
- [X] T021 **(in-game)** Validate [quickstart.md](quickstart.md) §6, Require exclusive hotkey **OFF** (SC-008,
  Clarification Q3), on the final build.
  - **Expected**: 10 out of 10 activations. Tick 6.13 in `docs/TEST_MATRIX.md`.
  - **If any fails**: record the log excerpt in `specs/003-fix-mouse-wheel-hotkeys/research.md` under R8, and tell
    the user that a separate spec is needed. It does not block this feature.
- [X] T022 Pre-commit review.
  - Set `**Status**` in `specs/003-fix-mouse-wheel-hotkeys/spec.md` to `Implemented`, listing any pending in-game
    tasks.
  - Run `git grep -n "kbDown\[" -- src/Input include/Input` and confirm that the mouse branch of
    `ProcessButtonEvents` is the only mouse write, and that it is after the wheel skip.
  - Run `git diff --stat -- include/Config src/Config` and confirm that it is empty (FR-008: no setting or INI
    changes).
  - Review `git diff` for every touched file, `.claude/CLAUDE.md` and `docs/TEST_MATRIX.md` included.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: T001 is optional. T002 can run any time before the first TEST_MATRIX tick (T008).
- **Foundational (Phase 2)**:
  - T003 and T004 can run in parallel (different files).
  - T005 depends on T004.
  - All three block T007.
- **US1 (Phase 3)**: T006 needs T003. T007 needs T003–T006. T008 needs T007.
- **US2 (Phase 4)**: T009 needs a build containing T006 (T007 or later). It does not depend on US3.
- **US3 (Phase 5)**:
  - T010 needs T003 and comes after T006 (same file).
  - T011 is [P] with T010 (different file).
  - T012 needs T010 and T011, and T013 needs T012.
- **Phase 6**:
  - T014 is [P] with anything outside `KeyStateStore.h`.
  - T015 needs T014 and comes after T010 (same file).
  - T016 needs T014. T017 needs T015 and T016, and T018 needs T017.
- **Polish (Phase 7)**: T019 can run any time after the design is stable. T020 and T021 run after the last code
  change, and T022 runs last.

### User story dependencies

- **US1**: independent. It is the MVP.
- **US2**: its validation relies on US1's code (T006), with no extra code.
- **US3**: independent code (T010, T011). Its validation also covers FR-009 from Phase 2.

### Same-file sequencing

- `src/Input/InputFilter.cpp`: T006 → T010 → T015
- `src/Input/ExclusiveTracker.cpp`: T005
- `include/Input/HotkeyMatcher.h`: T004
- `src/Input/HotkeyMatcher.cpp`: T011
- `include/Input/KeyStateStore.h`: T014
- `src/Input/PhysicalReconciler.cpp`: T016
- `include/Shared/InputConstants.h`: T003
- `docs/TEST_MATRIX.md`: T002, then ticks in T008, T009, T013 and T021
- `.claude/CLAUDE.md`: T019

---

## Parallel Example

```text
# Phase 2, at the start:
Task: "T003 Add mouse id constants and helpers in include/Shared/InputConstants.h"
Task: "T004 Add FindExtraHeldCode in include/Input/HotkeyMatcher.h"
Task: "T002 Add TEST_MATRIX cases 6.8–6.14 in docs/TEST_MATRIX.md"

# Phase 5 + 6, once T006 is done:
Task: "T011 Warn on wheel codes in src/Input/HotkeyMatcher.cpp"
Task: "T014 Add heartbeat state in include/Input/KeyStateStore.h"
Task: "T019 Update .claude/CLAUDE.md Input system section"
```

---

## Implementation Strategy

### MVP (User Story 1)

1. T002 → T003 + T004 → T005 → T006 → T007.
2. The user runs T008. If 20/20 pass, the reported bug is fixed.

### Incremental delivery

1. MVP as above.
2. T009 (US2 validation, no code).
3. T010 + T011 → T012 → T013 (US3).
4. T014 → T015 + T016 → T017 → T018 (FR-007).
5. T019 → T020 → T021 → T022, then commit when the user asks.

The build tasks can be merged when several code phases are done in one session. One build after T006, T010, T011,
T015 and T016, with debug first and release last, serves T008, T009, T013, T018 and T021.

---

## Notes

- **Commits**: only when the user asks. Pushes also need the user's go-ahead.
- **Scope**: no `Domain/`, `Application/` or `Config/` file changes. If one seems necessary, stop and revisit the
  plan.
- **Logs**: every log line uses `MAGIC_DEBUG_LOG` with the `[Input]` prefix (Constitution VI).

## Implementation log (2026-11-19)

- **Code tasks done** (T002–T007, T010–T012, T014–T017, T019, T020, T022) in one session.
  - One build after all code changes, **debug first and release last**, covered T007, T012 and T017.
  - Result: 0 errors and 0 compiler warnings (only the existing `vcpkg builtin-baseline` warning).
  - The MO2 DLL hash matches `build\release\IntegratedMagic.dll`, and the debug PDB was removed.
- **Deviations from the task text**:
  - T006 and T015 put their logic in two helpers in the anonymous namespace of `src/Input/InputFilter.cpp`:
    `NoteWheelIgnored` (the wheel skip log) and `RecordMouseHeartbeat` (the heartbeat and the first-held-event
    log). They are not inline in `ProcessButtonEvents`.
  - Reason: inline, the code raised the function's cognitive complexity above Sonar's limit (25 → 28). Behavior is
    identical.
  - T011 uses a local helper, `IsWheelCode`, for `c >= kMouseButtonBase && IsMouseWheelId(c - kMouseButtonBase)`.
- **T020**: violations were fixed only in this feature's lines. The existing violation at
  `src/Input/HotkeyMatcher.cpp` (`ConsumeHotkeyReloadRequest`) was left as is.
- **T022**:
  - The only mouse `true` write is `InputFilter.cpp` `ProcessButtonEvents`, after the wheel skip.
  - `git diff -- include/Config src/Config` is empty.
- **In-game validation (2026-11-19, release DLL)**: the user confirmed T008, T009, T013, T018 and T021 ("Tudo
  funcionando"). TEST_MATRIX 6.8–6.14 are ticked. T001, an optional baseline, was not run.
  - **Evidence in the final session log** (21:08–21:10):
    - `mouse wheel ignored for hotkeys (id=9 value=1.00 heldSecs=0.000)` confirms that the wheel sends a press with
      no release (R1, R2).
    - `first held mouse event seen (id=0 heldSecs=0.016)` confirms that held events arrive every frame. This is the
      heartbeat premise of R6 (T018 step 1).
    - There are 0 `extra=mouse:8|9` lines.
  - **Not in that log**: the 6.14 `REFUSED ... extra=kb:0x22` line. The log is overwritten on each launch, so this
    test presumably ran in an earlier session.
  - **T018 step 2** (a lost release on a 6+ button mouse) was not specifically confirmed.
  - **Regression cases 6.1–6.7** were not individually re-ticked.
