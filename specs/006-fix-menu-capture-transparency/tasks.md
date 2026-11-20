---

description: "Task list for spec 006: fix the SKSE menu turning transparent when capturing hotkeys"
---

# Tasks: Fix the SKSE menu turning transparent when capturing hotkeys

**Input**: design documents in `specs/006-fix-menu-capture-transparency/` ([plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md), [contracts/menu-ui-balance.md](contracts/menu-ui-balance.md),
[quickstart.md](quickstart.md))

**Tests**: no automated harness exists (Constitution VI). Validation is in game, through the quickstart and the
TEST_MATRIX tasks marked **(in-game)**, on the **DEBUG** build (research R4).

**Organization**: tasks are grouped by user story. The fix is one change, so there is no foundational phase.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1, US2, see spec.md)

## Conventions for every code task

- Identifiers and comments are in English. `.clang-format` applies (120 columns, 4-space indent).
- Touch only `src/UI/MENU.cpp` for code. No change in `src/Config`, `include/Config`, `src/Input`, `src/Domain`,
  `src/Application` or the INI (spec FR-006).

---

## Phase 1: Setup

**Purpose**: confirm the defect before the change. Optional; it does not block implementation.

- [ ] T001 **(in-game, user, optional)** On the current build, open the SKSE menu → Integrated Magic → slot hotkeys,
  and click "Cap" 10 times on different fields, cancelling each capture with "X".
  - **Expected (bug)**: the page fades a little more with each started capture (research R1).
  - Record the outcome in this file's implementation log.

---

## Phase 2: Foundational

No foundational tasks: the fix is a single change in `src/UI/MENU.cpp`, and both user stories only need it built.

---

## Phase 3: User Story 1 - Binding hotkeys never makes the menu fade (Priority: P1) 🎯 MVP

**Goal**: any number of hotkey captures leaves the plugin's page and the whole SKSE menu fully opaque.

**Independent Test**: [quickstart.md](quickstart.md) §1. 20 captures in a row, keyboard and gamepad, completed and
cancelled. Then another mod's SKSE menu page.

- [X] T002 [US1] In `src/UI/MENU.cpp`, `DrawKeyValueCapture`, `else` branch (today lines 108–114), apply research R2
  and contract §1.
  - Replace `if (g_fieldCapture.active) ImGuiMCP::BeginDisabled(true);` with
    `ImGuiMCP::BeginDisabled(g_fieldCapture.active);`.
  - Replace `if (g_fieldCapture.active) ImGuiMCP::EndDisabled();` with `ImGuiMCP::EndDisabled();`.
  - Leave the `SmallButton("Btn_Cap")` block between them unchanged.
  - Add one comment above the pair: `// Unconditional pair: the click below starts a capture and flips
    g_fieldCapture.active, so the end must not re-read it (spec 006, contract §1).`
- [X] T003 [P] [US1] In `docs/TEST_MATRIX.md`, section 10, add after 10.8, in Portuguese and in the file's style:
  - **10.9**: "Captura de hotkey repetida (20× seguidas, teclado e gamepad, concluídas e canceladas com X) → o menu
    do SKSE continua totalmente opaco; a página de outro mod no menu do SKSE fica normal" (spec
    `specs/006-fix-menu-capture-transparency/`).
  - **10.10**: "'- Vertex', 'Apply changes', botão 'M', grade de âncora do HUD e cantos do rótulo, 10× cada → o menu
    nunca desbota e cada controle mantém a aparência".
  - **Must be done before T005 and T007**, which tick these cases.
- [X] T004 [US1] Build: release first, then **debug last**, so MO2 holds the DEBUG DLL.
  - Use the scratchpad `build.ps1` with presets `release` then `debug`, or run `cmake --preset <p>` then `cmake --build
    build/<p>`.
  - Expect 0 errors and 0 new compiler warnings.
  - Keep the debug `IntegratedMagic.pdb` in `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`.
  - Check that the MO2 DLL hash matches `build\debug\IntegratedMagic.dll`.
- [X] T005 [US1] **(in-game, user)** Validate [quickstart.md](quickstart.md) §1 (SC-001, SC-002, SC-003).
  - **First**, check `skse64.log`: the `IntegratedMagic` line must not show `couldn't load plugin 4551`. If it does,
    Windows still blocks the DEBUG DLL: stop and ask the user to approve the release-build fallback (research R4).
  - **Expected**:
    - the page stays fully opaque through 20 captures,
    - the other fields' "Cap" buttons look disabled during a capture and come back afterwards,
    - each completed capture binds the key, each cancelled one changes nothing,
    - another mod's page looks normal.
  - **Afterwards**: tick TEST_MATRIX 10.9.

**Checkpoint**: the reported bug is fixed (MVP).

---

## Phase 4: User Story 2 - The rest of the menu keeps working (Priority: P2)

**Goal**: the other disabled or highlighted controls keep their look and never fade the menu.

**Independent Test**: [quickstart.md](quickstart.md) §2.

- [X] T006 [US2] Review `src/UI/MENU.cpp` against research R3 after T002.
  - Search `BeginDisabled`, `EndDisabled`, `PushStyleColor`, `PopStyleColor`, `PushStyleVar`, `PopStyleVar`,
    `PushID` and `PopID`.
  - Confirm that every conditional pair tests one `const` local computed before its control, and that every
    `Begin`/`EndDisabled` pair is unconditional (contract §1).
  - No change is expected. If one is needed, apply contract §1 and record it in the implementation log.
- [X] T007 [US2] **(in-game, user)** Validate [quickstart.md](quickstart.md) §2 (SC-004) on the T004 build.
  - **Expected**: "- Vertex", "Apply changes", "M", the HUD anchor grid and the label corners, 10 uses each: no fading,
    same look as today.
  - **Afterwards**: tick TEST_MATRIX 10.10.

---

## Phase 5: Polish & cross-cutting concerns

- [X] T008 [P] In `.claude/CLAUDE.md`, section `## HUD / UI`, add a short paragraph "SKSE menu pair rule (spec
  006)":
  - the settings page draws into SKSE Menu Framework's shared ImGui context,
  - `BeginDisabled(cond)` / `EndDisabled()` are always an unconditional pair,
  - a conditional `Push*` / `Pop*` pair tests one `const` local computed before its control,
  - an unbalanced pair fades the whole SKSE menu (the capture-button bug).
- [X] T009 Run clang-format in dry-run mode on the changed line ranges of `src/UI/MENU.cpp` only (pass `--lines`
  through `@()`), and fix any violation.
- [X] T010 Final build as in T004 (release, then debug last).
  - Check the scope with `git diff --stat -- src/Config include/Config src/Input src/Domain src/Application SKSE`: it
    must be empty.
  - `git diff --stat` must show only `src/UI/MENU.cpp`, `docs/TEST_MATRIX.md`, `.claude/CLAUDE.md` and this spec's
    files.
- [X] T011 **(in-game, user)** Regression: [quickstart.md](quickstart.md) §3. TEST_MATRIX 10.7 (hotkey rebinding) and
  6.12 (the wheel ignored during capture) (SC-005). Tick what passes, and record any failure with its log excerpt.
- [X] T012 Pre-commit review:
  - set `**Status**` in `specs/006-fix-menu-capture-transparency/spec.md`,
  - add an implementation log section to this file (build, DEBUG load result, T001 outcome),
  - review `git diff`,
  - commit and push only when the user asks.

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1** (T001): optional, before T002 if run (it needs the unfixed build).
- **Phase 3 (US1)**: T002 → T004 → T005. T003 can run beside T002 and must be done before T005.
- **Phase 4 (US2)**: T006 after T002. T007 after T004 and T003.
- **Phase 5**: T008 at any time. Then T009 → T010 → T011 → T012, last.

### User story dependencies

- **US1 (P1)**: independent. It is the MVP.
- **US2 (P2)**: validates the same build. It has no code of its own unless T006 finds a gap.

### Same-file sequencing

- `src/UI/MENU.cpp`: T002 → T006 (review) → T009.
- `docs/TEST_MATRIX.md`: T003 → T005 / T007 / T011 (ticks).

---

## Parallel Example

```text
# Together, different files:
T002  src/UI/MENU.cpp        (the fix)
T003  docs/TEST_MATRIX.md    (10.9, 10.10)
T008  .claude/CLAUDE.md      (pair rule note)
```

---

## Implementation Strategy

### MVP first (US1)

1. T002, T003, T004, then T005 in game.
2. **Stop and validate**: the menu no longer fades with repeated captures.

### Incremental delivery

1. US2 (T006–T007): the regression guard on the other controls, on the same build.
2. Polish (T008–T012).

## Notes

- If the DEBUG DLL is blocked again (`000011C7` / 4551), the release-build fallback needs the user's approval for this
  spec before T005.
- Commit or push only when the user asks.

## Implementation log (2026-11-20)

- **Code and doc tasks done**: T002, T003, T004, T006, T008, T009, T010, T012.
  - **T002**: the capture button now sits in an unconditional `BeginDisabled(g_fieldCapture.active)` …
    `EndDisabled()` pair (`src/UI/MENU.cpp`, `DrawKeyValueCapture`), with the comment from the task. The button body
    is unchanged.
  - **T006**: no other change needed.
    - All three `BeginDisabled`/`EndDisabled` pairs of `MENU.cpp` are unconditional ("Cap", "- Vertex", "Apply
      changes").
    - The conditional `PushStyleColor`/`PopStyleColor` pairs test `const` locals (`isMod`, `active`, `tc`, `ac`)
      computed before their buttons.
  - **Builds**: release, then debug last, with 0 errors and 0 compiler warnings (`build-006-release.log`,
    `build-006-debug.log`).
    - MO2 holds the **DEBUG** DLL (hash matches `build\debug\IntegratedMagic.dll`), with its PDB.
    - Smart App Control is off (`VerifiedAndReputablePolicyState = 0`), so it is expected to load.
  - **clang-format**: 0 violations on the changed line ranges.
  - **Scope**: `git diff --stat -- src/Config include/Config src/Input src/Domain src/Application SKSE` is empty. The
    code diff is `src/UI/MENU.cpp` only (6 lines).
- **Not this spec's change**: `.claude/tasks/integrated_magic_bugs_e_melhorias.md` was staged by the user. It is left
  as it is.
- **T001** (optional baseline) was not run.
- **DEBUG DLL did not load** (2026-11-20, 20:14), and Windows was not the cause (Smart App Control off).
  - `skse64.log`: `disabled, fatal error occurred while loading plugin 0`.
  - `IntegratedMagic.log` stops after `[StyleConfig] styles.ini carregado.`, with no `[Hooks] ... installed`. The
    fault is in `Hooks::Install_Hooks`, before the first hook finishes.
  - It is the long-standing DEBUG-only failure of specs 002–005. It is unrelated to this spec's one-line UI change.
  - The release DLL (built 20:01, with the fix) is now in MO2, and the debug PDB was removed.
  - Validation on the release DLL was **approved by the user** (spec Clarifications, plan Complexity Tracking).
- **In-game validation**: the user reported every test passing on the release DLL (2026-11-20): T005, T007 and T011. TEST_MATRIX 10.7, 10.9 and 10.10 are ticked, and 6.12 was already ticked.
  - First check `skse64.log` for `couldn't load plugin 4551`.
  - If Windows still blocks the DEBUG DLL, the release fallback needs the user's approval.
