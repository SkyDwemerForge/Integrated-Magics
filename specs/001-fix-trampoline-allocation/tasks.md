---

description: "Task list for fixing the startup crash from hook-space (trampoline) exhaustion"
---

# Tasks: Fix startup crash from hook-space (trampoline) exhaustion

**Input**: Design documents from `specs/001-fix-trampoline-allocation/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md),
[contracts/hook-installation.md](contracts/hook-installation.md), [quickstart.md](quickstart.md)

**Tests**: no automated test tasks. The spec does not request them, and no harness can load the game
(Constitution VI). Each story ends with an in-game **validation task** driven by [quickstart.md](quickstart.md) and
`docs/TEST_MATRIX.md`.

**Organization**: tasks are grouped by user story so each story can be implemented and validated on its own.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: user story from spec.md (US1, US2, US3)
- Paths are relative to the repository root. This is a single-project SKSE plugin (`include/`, `src/`, `extern/`,
  `docs/`).

---

## Phase 1: Setup

**Purpose**: confirm the defect before changing anything, for a before/after comparison.

- [ ] T001 Reproduce the failure with the current build: deploy `build/release/IntegratedMagic.dll` (post-build
  already copies it to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`), launch through SKSE, and confirm
  a MessageBox captioned `IntegratedMagic.dll` with body `SKSE/Trampoline.cpp(164): Failed to handle allocation
  request`, plus a `critical` line with the same text in
  `Documents\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`. Append a line
  `Reproduced: <date>, <SE|AE runtime version>` under R1 in `specs/001-fix-trampoline-allocation/research.md`.

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the hook-space budget that US1 and US3 both depend on.

**⚠️ CRITICAL**: no user story work can begin until this phase is complete.

- [X] T002 Add the hook-space budget to `include/Hooks.h`. Inside `namespace IntegratedMagic::Hooks`, before
  `void Install_Hooks();`, add:
  - `inline constexpr std::size_t kCallHookBytes = 14;` with a comment: bytes per `write_call<5>`/`write_jmp`
    trampoline entry.
  - `inline constexpr std::size_t kCallHookCount = 3;` with a comment listing the counted hooks:
    `PollInputDevicesHook`, `D3DInitHook`, `DXGIPresentHook`.
  - `inline constexpr std::size_t kHookSpaceBytes = 128;`
  - `static_assert(kCallHookCount * kCallHookBytes * 4 <= kHookSpaceBytes * 3, "hook space usage must stay <= 75%
    of kHookSpaceBytes; raise kHookSpaceBytes");`
  - `#include <cstddef>` at the top.

  The budget must satisfy the data-model rule verbatim: "The sum of `hookSpaceBytes` over all hooks MUST be ≤ 75% of
  `HookSpaceReservation.capacity`".

**Checkpoint**: `kHookSpaceBytes` is available to `src/plugin.cpp` and `src/Hooks.cpp`.

---

## Phase 3: User Story 1 - Game starts with Integrated Magic enabled (Priority: P1) 🎯 MVP

**Goal**: one reservation made before any hook installs, so all hooks fit. The plugin loads with no error, and the
log reports every hook and the space used (FR-001, FR-002, FR-003, FR-004, FR-006, FR-012).

**Independent Test**: launch to the main menu with no dialog, load a save, see the HUD, cast from a slot hotkey, and
find six `[Hooks] ... installed` lines plus `[Hooks] hook space: 42 / 128 B used (33%), SKSE trampoline interface:
yes` in `IntegratedMagic.log` ([quickstart.md](quickstart.md) §2–§3).

### Implementation for User Story 1

- [X] T003 [US1] In `src/plugin.cpp`, inside `SKSE_PLUGIN_LOAD(...)`, replace `SKSE::Init(skse);` with
  `SKSE::Init(skse, { .trampoline = true, .trampolineSize = IntegratedMagic::Hooks::kHookSpaceBytes });`
  (`Hooks.h` is already included).
  - Keep the user's staged `SKSE_PLUGIN_LOAD` macro change as is.
  - Do not call `SKSE::AllocTrampoline` anywhere (contract §1).
- [X] T004 [P] [US1] In `extern/Detours/HookUtil.hpp`, delete all five `SKSE::AllocTrampoline(...)` lines: the
  `14` in each of the three `write_call` overloads and the `8` in each of the two `write_jmp` overloads. Per
  contract §2, "Helpers MUST NOT request or reserve hook space".
- [X] T005 [US1] In `src/Hooks.cpp`, at the top of `Install_Hooks()` (before
  `Inbound::PollInputDevicesHook::Install();`), add the fallback for a missing SKSE trampoline interface (FR-006,
  research R4):

  ```cpp
  auto& trampoline = SKSE::GetTrampoline();
  if (trampoline.empty()) {
      spdlog::warn("[Hooks] SKSE trampoline interface unavailable or reservation empty; allocating {} B near the game module", kHookSpaceBytes);
      trampoline.create(kHookSpaceBytes);
  }
  ```

  Add `#include "PCH.h"` if `SKSE::GetTrampoline`/`spdlog` are not visible.
- [X] T006 [US1] In `src/Hooks.cpp`, after the last `Install()` call in `Install_Hooks()`, log the usage summary
  (FR-004, research R6, contract §4). Depends on T005 (same file).
  - Compute `used = trampoline.allocated_size()`, `cap = trampoline.capacity()` and
    `pct = cap ? 100.0 * used / cap : 0.0`.
  - Log `spdlog::info("[Hooks] hook space: {} / {} B used ({:.0f}%), SKSE trampoline interface: {}", used, cap,
    pct, SKSE::GetTrampolineInterface() ? "yes" : "no");`
  - If `used * 4 > cap * 3`, also log `spdlog::warn("[Hooks] hook space usage above 75%: {} / {} B — raise
    kHookSpaceBytes", used, cap);`
- [X] T007 [P] [US1] In `src/Adapters/Inbound/PollInputDevicesHook.cpp` `Install()`, after the
  `Hook::stl::write_call<Impl>(...)` line, add `spdlog::info("[Hooks] PollInputDevicesHook installed");`
- [X] T008 [P] [US1] In `src/Adapters/Inbound/D3DInitHook.cpp` `Install()`, replace
  `MAGIC_DEBUG_LOG("[Hooks] D3DInitHook installed");` with `spdlog::info("[Hooks] D3DInitHook installed");` so it
  also appears in release builds (research R6).
- [X] T009 [P] [US1] In `src/Adapters/Inbound/DXGIPresentHook.cpp` `Install()`, replace
  `MAGIC_DEBUG_LOG("[Hooks] DXGIPresentHook installed");` with
  `spdlog::info("[Hooks] DXGIPresentHook installed");`
- [X] T010 [P] [US1] In `src/Adapters/Inbound/PlayerAnimGraphHook.cpp` `Install()`, after
  `Impl::_orig = reinterpret_cast<Impl::Fn>(orig);`, add `spdlog::info("[Hooks] PlayerAnimGraphHook installed");`.
  Do not change the hook target or the thunk (FR-008).
- [X] T011 [P] [US1] In `src/Adapters/Inbound/MagicCasterInterruptHook.cpp` `Install()`, after the `write_vfunc(8,
  ...)` line, add `spdlog::info("[Hooks] MagicCasterInterruptHook installed");`
- [X] T012 [P] [US1] In `src/Adapters/Inbound/MagicCasterStartCastHook.cpp` `Install()`, after the
  `write_vfunc(6, ...)` line, add `spdlog::info("[Hooks] MagicCasterStartCastHook installed");`
- [X] T013 [P] [US1] In `docs/TEST_MATRIX.md`, insert a section `## 0. Startup & hooks` before
  `## 1. Casos críticos de cast`, written in Portuguese like the rest of the file, with unchecked items (FR-012):
  - **0.1** launch through SKSE → main menu with no `IntegratedMagic.dll` dialog.
  - **0.2** `IntegratedMagic.log` has six `[Hooks] ... installed` lines and `[Hooks] hook space: 42 / 128 B used
    (33%), ...`, with no `[Hooks]` warn or critical lines.
  - **0.3** launch with the full SKSE load order → same as 0.1/0.2.
  - **0.4** repeat 0.1–0.3 on SE and on AE.

  Also add `0. **Seção 0** (startup & hooks) — bloqueante` as the first item of `## Prioridade de execução
  sugerida`.
- [X] T014 [US1] Build both presets into `build/release` and `build/debug` from a VS Developer PowerShell (x64):
  `cmake --preset release; cmake --build build/release` and `cmake --preset debug; cmake --build build/debug`
  ([quickstart.md](quickstart.md) §1).
  Expect 0 errors and no new warnings. Depends on T002–T012.
- [ ] T015 [US1] Validate in game per [quickstart.md](quickstart.md) §2, §3 and §5, and tick the matching
  checkboxes in `docs/TEST_MATRIX.md`.
  - **Startup**: 10 consecutive launches with no dialog (SC-001); TEST_MATRIX 0.1, 0.2, 0.3 (0.4 if both runtimes
    are available).
  - **Gameplay**: TEST_MATRIX 2.3, 6.1–6.7, 10.1, 10.4, 10.5, 10.6, 10.8.
  - **Single install**: after loading a save and then loading another save, each `[Hooks] ... installed` line
    still appears exactly once in the session log (data-model rule "Each hook installs exactly once per process").

**Checkpoint**: the plugin loads and works. MVP is complete.

---

## Phase 4: User Story 2 - Animation-driven behavior keeps working (Priority: P2)

**Goal**: confirm with evidence that animation events reach the state machine without an adapter instance, remove
the misleading include, and document the path (FR-008, FR-009, FR-011).

**Independent Test**: TEST_MATRIX 7.10, 8.1, 8.2 and 9.6 pass on the DEBUG build, and the log shows `[State]`/`[FLOW]`
lines reacting to player animation tags ([quickstart.md](quickstart.md) §4).

### Implementation for User Story 2

- [X] T016 [US2] In `src/Hooks.cpp`, remove the unused line `#include "Adapters/Inbound/AnimEventAdapter.h"`
  (FR-009). Do not touch `src/Adapters/Inbound/PlayerAnimGraphHook.cpp`, which still includes it and calls
  `AnimListener::HandleAnimEvent`. Depends on T006 (same file).
- [X] T017 [US2] In `.claude/CLAUDE.md`, add a section `## Hooks` right after the `Hook installation:
  include/Hooks.h / src/Hooks.cpp.` line (after the Architecture layers table), containing:
  - **(a)** a table of the 7 hooks from `specs/001-fix-trampoline-allocation/data-model.md` "Current inventory"
    (hook, kind, bytes, target).
  - **(b)** a subsection `### Animation events`. It explains that `PlayerAnimGraphHook` patches
    `RE::VTABLE_PlayerCharacter[2]` slot 1 (`BSTEventSink<BSAnimationGraphEvent>::ProcessEvent`), calls the
    original, then `AnimListener::HandleAnimEvent` (`include/Adapters/Inbound/AnimEventAdapter.h`), which forwards
    the tag to `Application::SpellSystemController::NotifyAnimEvent`. It states that the adapter is a stateless free
    function and needs no instance or `AddEventSink` registration.
- [ ] T018 [US2] Rebuild `debug` (`cmake --build build/debug`), then validate per [quickstart.md](quickstart.md) §4.
  - Run TEST_MATRIX 7.10, 8.1, 8.2 and 9.6, and confirm in `IntegratedMagic.log` that `[State]`/`[FLOW]` lines
    react to the sheathe, block/stagger and `shoutStop` animation events.
  - Tick those checkboxes in `docs/TEST_MATRIX.md`.

**Checkpoint**: US1 and US2 both validated.

---

## Phase 5: User Story 3 - Hook problems are diagnosable and cannot silently come back (Priority: P3)

**Goal**: a named failure message instead of the generic library error, a correct `write_jmp` size, and a documented
policy for adding hooks (FR-005, FR-007, FR-011).

**Independent Test**: with `kHookSpaceBytes` temporarily set to 16 in a local debug build, launch and get the
MessageBox and critical log line naming `D3DInitHook::Install` with `need 14 B, free 2 B of 16 B reserved`
([quickstart.md](quickstart.md) §6).

### Implementation for User Story 3

- [X] T019 [US3] In `extern/Detours/HookUtil.hpp`, add the named space check (FR-005, FR-007, research R5/R7,
  contract §2). Depends on T004.
  - **(a)** Add `#include <format>` and `#include <source_location>`.
  - **(b)** Inside `namespace Hook::stl`, add:

    ```cpp
    namespace detail {
        inline void EnsureHookSpace(std::size_t a_bytes, std::source_location a_loc) {
            auto& t = SKSE::GetTrampoline();
            if (t.empty() || t.free_size() < a_bytes) {
                SKSE::stl::report_and_fail(std::format("Integrated Magic: not enough hook space to install hook at {}: need {} B, free {} B of {} B reserved", a_loc.function_name(), a_bytes, t.free_size(), t.capacity()));
            }
        }
    }
    ```

  - **(c)** Append `std::source_location a_loc = std::source_location::current()` as the last parameter of all
    three `write_call` overloads and both `write_jmp` overloads.
  - **(d)** In each `write_call`, call `detail::EnsureHookSpace(Size == 6 ? 8 : 14, a_loc);` before
    `SKSE::GetTrampoline()` is used.
  - **(e)** In each `write_jmp`, call `detail::EnsureHookSpace(14, a_loc);` before `write_branch<5>`.
  - Call sites do not change.
- [X] T020 [US3] In `.claude/CLAUDE.md`, under the `## Hooks` section (created by task T017, which must run
  first), add `### Hook space (trampoline) policy` summarizing
  [contracts/hook-installation.md](contracts/hook-installation.md) §1–§3.
  - The reservation is made only in `SKSE::Init(..., { .trampoline = true, .trampolineSize = kHookSpaceBytes })` in
    `src/plugin.cpp`, with the budget in `include/Hooks.h`.
  - Bytes per helper: `write_call<5>`/`write_jmp` 14, `write_call<6>` 8, vtable/detour 0.
  - `SKSE::AllocTrampoline` is forbidden: it is deprecated, and only the first request is honored.
  - The 4-step "Adding a hook" checklist.
  - A note that commit `6188de8` (#64) dropped the old upfront reservation, which surfaced after the CommonLib
    v10.1.0 update.
- [ ] T021 [US3] Run the negative check per [quickstart.md](quickstart.md) §6.
  - **Locally only**: set `kHookSpaceBytes = 16` and comment out the `static_assert` in `include/Hooks.h`, build
    `debug`, launch, and confirm the MessageBox (caption `IntegratedMagic.dll`) and critical log line contain
    `D3DInitHook::Install` and `need 14 B, free 2 B of 16 B reserved` (SC-006).
  - Then revert `include/Hooks.h` and confirm `git diff -- include/Hooks.h` shows only the T002 change.

**Checkpoint**: all three user stories validated.

---

## Phase 6: Polish & cross-cutting concerns

- [X] T022 [P] Run clang-format with the repository `.clang-format` on `include/Hooks.h`, `src/Hooks.cpp`,
  `src/plugin.cpp`, `extern/Detours/HookUtil.hpp` and the six changed files in `src/Adapters/Inbound/`. Review that
  only touched lines changed.
- [X] T023 Final clean build of both presets (`cmake --build build/release`, `cmake --build build/debug`), with 0
  errors and no new warnings (Constitution merge gate 1).
- [ ] T024 Regression in game, recorded in `docs/TEST_MATRIX.md` (Constitution merge gate 3): TEST_MATRIX 5.1
  baseline (all patches OFF) plus smoke cases 1.1 and 1.5 (dual cast). The input hook's install path changed, although its thunk did not. Tick the
  boxes in `docs/TEST_MATRIX.md`.
- [X] T025 Pre-commit review of `src/plugin.cpp` and `specs/001-fix-trampoline-allocation/spec.md`.
  - In `specs/001-fix-trampoline-allocation/spec.md`, set `**Status**: Draft` to `**Status**: Implemented`.
  - Review `git diff --cached -- src/plugin.cpp` together with `git diff -- src/plugin.cpp` (the user's staged
    `SKSE_PLUGIN_LOAD` change plus T003), and confirm no `SKSE::AllocTrampoline` remains (`git grep -n
    AllocTrampoline -- src include extern/Detours` returns nothing).

---

## Dependencies & execution order

### Phase dependencies

- **Setup (Phase 1)**: none. T001 only reproduces the defect and can be skipped if already confirmed.
- **Foundational (Phase 2)**: T002 blocks every story (T003, T005 and T006 use `kHookSpaceBytes`).
- **US1 (Phase 3)**: after T002.
- **US2 (Phase 4)**: after T002. T016 edits `src/Hooks.cpp` after T005/T006. T018 needs a bootable build, so it
  needs US1 done.
- **US3 (Phase 5)**: after T002. T019 edits `HookUtil.hpp` after T004. T020 edits `.claude/CLAUDE.md` after T017.
- **Polish (Phase 6)**: after the desired stories.

### User story dependencies

- **US1 (P1)**: independent. It is the MVP.
- **US2 (P2)**: code change (T016) and docs (T017) are independent of US1. In-game validation (T018) requires US1,
  since the game must boot.
- **US3 (P3)**: code (T019) builds on T004. Validation (T021) requires US1 to boot past the first hook.

### Same-file sequencing (cannot be parallel)

- `src/Hooks.cpp`: T005 → T006 → T016
- `extern/Detours/HookUtil.hpp`: T004 → T019
- `.claude/CLAUDE.md`: T017 → T020
- `docs/TEST_MATRIX.md`: T013 → T015 → T018 → T024 (checkbox updates)

### Parallel opportunities

- After T002: T003, T004, T007–T013 touch different files and can run together.
- T005/T006 (`src/Hooks.cpp`) can run alongside the hook-file tasks T007–T012.
- In US2, T017 (docs) can run alongside T016 (code).

---

## Parallel example: User Story 1

```text
# After T002 completes, in parallel:
Task: "T003 SKSE::Init with InitInfo trampoline in src/plugin.cpp"
Task: "T004 Remove SKSE::AllocTrampoline from extern/Detours/HookUtil.hpp"
Task: "T007 Installed log in src/Adapters/Inbound/PollInputDevicesHook.cpp"
Task: "T008 Installed log in src/Adapters/Inbound/D3DInitHook.cpp"
Task: "T009 Installed log in src/Adapters/Inbound/DXGIPresentHook.cpp"
Task: "T010 Installed log in src/Adapters/Inbound/PlayerAnimGraphHook.cpp"
Task: "T011 Installed log in src/Adapters/Inbound/MagicCasterInterruptHook.cpp"
Task: "T012 Installed log in src/Adapters/Inbound/MagicCasterStartCastHook.cpp"
Task: "T013 TEST_MATRIX section 0 in docs/TEST_MATRIX.md"
# Sequential in src/Hooks.cpp:
Task: "T005 Empty-trampoline fallback" → "T006 Usage summary log"
# Then: T014 build → T015 in-game validation
```

---

## Implementation strategy

### MVP first (User Story 1 only)

1. T001 (optional reproduction) → T002 budget.
2. T003–T013 → T014 build → T015 validate in game.
3. **Stop and validate**: the game starts, the HUD and casting work, and the log shows 42 / 128 B. This alone
   unblocks players.

### Incremental delivery

1. US1: the crash is fixed. Commit.
2. US2: include cleanup plus animation-event documentation and evidence. Commit.
3. US3: named failure, `write_jmp` fix and hook-space policy documentation. Commit.
4. Polish: format, final builds, regression, pre-commit review.

---

## Notes

- No hook target, Address Library ID, offset or vtable slot changes in any task (contract §5).
- `MAGIC_DEBUG_LOG` stays as is everywhere else. Only the `[Hooks] ... installed` lines move to `spdlog::info`.
- `include/PCH.h`'s unconditional `#define DEBUG` is out of scope for this feature.
- Commit after each story checkpoint. Do not commit the T021 negative-check edit.
