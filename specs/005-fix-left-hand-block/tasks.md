---

description: "Task list for spec 005: fix left-hand casting stuck by the block button"
---

# Tasks: Fix left-hand casting stuck by the block button

**Input**: design documents in `specs/005-fix-left-hand-block/` ([plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md), [contracts/hand-button-gate.md](contracts/hand-button-gate.md),
[quickstart.md](quickstart.md))

**Tests**: no automated harness exists (Constitution VI). Validation is in game, through the quickstart and
TEST_MATRIX tasks marked **(in-game)**, on the release DLL with logging (approved by the user, plan Complexity
Tracking).

**Organization**: tasks are grouped by user story. The gate, its wiring and the domain constants are shared by every
story, so they are in Phase 2.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1–US5, see spec.md)

## Conventions for every code task

- Identifiers, comments and log messages are in English. `.clang-format` applies (120 columns, 4-space indent).
- Every log line uses `MAGIC_DEBUG_LOG` with the prefix from research R8. No line is written for per-frame held
  repeats.
- Code with no game dependency stays free of `RE::` types (Constitution VI).
- Do not touch `src/Config`, `include/Config` or the INI (FR-013, Constitution V).

---

## Phase 1: Setup

**Purpose**: confirm the mechanism before the code changes. Optional; it does not block implementation.

- [ ] T001 **(in-game, user, optional)** Run [quickstart.md](quickstart.md) §0 on the **current** release DLL and keep
  the log.
  - **Expected**: in §0 steps 1–2, `StartRequested for 0.15s → UP` or `pre-cast spurious` repeating for the left
    hand. In §0 step 3, the slot stays active.
  - Note whether `AnimEvent: blockStart` appears. Record the outcome in this file's implementation log (research R1,
    R11).

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the hand-button gate, its wiring in the controllers, and the domain constants. Every user story depends
on this phase.

- [X] T002 [P] Create `include/Input/HandButtonStore.h`: pure header, no `RE::` types, namespace `Input`.
  - **Includes**: `<cstdint>`, `<optional>`, `Input/ExclusiveStore.h` (for `PressFate`), `Shared/Hand.h`.
  - **`struct HandButtonState`**:
    - `bool gameHeld{false};` with the comment `// The game sees this hand's attack/block action held by the
      player; updated only from events that reach the game (spec 005, R4).`
    - `float lastHeldSecs{0.f};`
    - `PressFate fate{PressFate::None};` with the comment `// None or Consumed: a Consumed press is kept from the
      game until its release (R4).`
  - **`struct HandButtonStore`**:
    - members `HandButtonState left{}; HandButtonState right{}; bool cancelRequested{false};`
    - `HandButtonState& For(IntegratedMagic::Hand h) noexcept`
    - `void ResetHands() noexcept { left = {}; right = {}; }`
  - **`struct HandGateFlags`**:
    - members `bool pluginCastLeft{false}; bool pluginCastRight{false}; bool mayCancel{false};`
    - `[[nodiscard]] bool PluginCast(IntegratedMagic::Hand h) const noexcept`
  - **Enums**: `enum class HandEventPhase : std::uint8_t { Press, Held, Release };` and `enum class HandGateOutcome :
    std::uint8_t { Pass, KeptCancel, KeptIgnored, PassCancel, KeptUntilRelease, Cleared, KeptOrphan };`
  - **Result**: `struct HandGateDecision { bool filter{false}; bool requestCancel{false}; HandGateOutcome
    outcome{HandGateOutcome::Pass}; };`
  - **`[[nodiscard]] inline HandGateDecision DecideHandEvent(HandButtonState& st, bool thisHandPluginCast, bool
    otherHandPluginCast, bool mayCancel, HandEventPhase phase, float heldSecs) noexcept`**, implementing research R4
    and contract §2 exactly:
    - **`Press`**: first set `st.fate = PressFate::None` ("a fresh press always starts a new fate"). Let
      `cancel = mayCancel && (thisHandPluginCast || otherHandPluginCast)`.
      - If `thisHandPluginCast`: `st.fate = PressFate::Consumed`, `filter = true`, `requestCancel = cancel`, outcome
        `KeptCancel` if `cancel`, otherwise `KeptIgnored`.
      - Otherwise: `filter = false`, `requestCancel = cancel`, outcome `PassCancel` if `cancel`, otherwise `Pass`.
    - **`Held` / `Release`**:
      - If `st.fate == PressFate::Consumed`: `filter = true`. On `Release`, set `st.fate = PressFate::None` with
        outcome `Cleared`; otherwise outcome `KeptUntilRelease`.
      - Else if `thisHandPluginCast && !st.gameHeld`: `filter = true`, outcome `KeptOrphan`.
      - Else: `filter = false`, outcome `Pass`.
    - **When `!filter`**: `st.gameHeld = (phase != HandEventPhase::Release)` and `st.lastHeldSecs = heldSecs`.
  - **`[[nodiscard]] inline std::optional<float> TakeHeldForCast(HandButtonState& st) noexcept`**:
    - if `st.gameHeld && st.fate != PressFate::Consumed`: set `st.fate = PressFate::Consumed` and `st.gameHeld =
      false`, and return `st.lastHeldSecs > 0.f ? st.lastHeldSecs : 0.1f`;
    - otherwise return `std::nullopt`.
- [X] T003 [P] In `include/Domain/State.h`:
  - **`HandMode`**: add `float startFailSecs{0.f};` after `startRequestSecs`, with the comment `// Time the current
    attempt has spent without a stable cast; restarts do not reset it (spec 005, R7).`
  - **`MagicState`**: next to `kMaxActiveTimeoutSecs`, add `static constexpr float kMaxCastStartSecs = 1.0f;`
    (comment: retry bound, spec Q2) and `static constexpr float kSpuriousInterruptWindowSecs = 0.2f;` (comment:
    interruptions below it are spurious, and a cast past it is real).
  - **Private helper**: `[[nodiscard]] static bool IsPluginCast(const HandMode& hm) noexcept { return !hm.finished &&
    (hm.autoActive || (hm.holdActive && hm.wantAutoAttack)); }`
  - **Public accessor** for the Application layer: `[[nodiscard]] bool IsHandPluginCast(Hand h) const noexcept {
    return _session.active && IsPluginCast(ModeFor(h)); }`
- [X] T004 In `include/Input/InputFilter.h` and `src/Input/InputFilter.cpp` (depends on T002):
  - **Header**: include `Input/HandButtonStore.h`. Declare `[[nodiscard]] bool GateHandEvent(const
    RE::BSFixedString& userEvent, float value, float heldSecs, HandButtonStore& hands, const HandGateFlags& flags);`.
  - **Change `FilterEvents`** to take `HandButtonStore& hands, const HandGateFlags& flags` after `RetainedArr&
    retained`.
  - **In `InputFilter.cpp`, anonymous namespace**:
    - `std::optional<IntegratedMagic::Hand> HandOfUserEvent(const RE::BSFixedString& ue)` compares with function-local
      `static const RE::BSFixedString` values `"Left Attack/Block"` → `Left` and `"Right Attack/Block"` → `Right`
      (research R2).
    - `HandEventPhase PhaseOf(float value, float heldSecs)` returns `Press` if `value > 0 && heldSecs <= 0`, `Held` if
      `value > 0`, and `Release` otherwise.
  - **Define `GateHandEvent`** (outside the anonymous namespace):
    - If there is no hand, return `false`.
    - Otherwise call `DecideHandEvent(hands.For(hand), flags.PluginCast(hand), flags.PluginCast(other),
      flags.mayCancel, phase, heldSecs)`.
    - If `requestCancel`, set `hands.cancelRequested = true`.
    - Log by outcome, with the exact strings of contract §6:
      - `KeptCancel` → `[Input] HandGate: {} press during plugin cast -> cancel session`,
      - `KeptIgnored` → `... -> ignored (Press mode)`,
      - `PassCancel` → `[Input] HandGate: {} other-hand press -> passes, cancel session`,
      - `Cleared` → `[Input] HandGate: {} button released -> gate cleared`,
      - no line for `Pass`, `KeptUntilRelease` or `KeptOrphan`.
    - Return `decision.filter`.
  - **In `FilterEvents`**, after the existing `remove = ShouldFilterAndSave(...) || ShouldFilterHudToggle(...)` block
    and outside the `code` range check: `if (!remove && !isWheel) remove = GateHandEvent(btn->QUserEvent(),
    btn->Value(), btn->HeldDuration(), hands, flags);`. Declare `isWheel` before that block if needed. The gate runs
    only on events that passed the combo rules (research R3).
- [X] T005 In `include/Application/InputController.h` and `src/Application/InputController.cpp` (depends on T002, T003,
  T004):
  - **Members**: `Input::HandButtonStore m_handButtons{};` and `Input::HandGateFlags m_handFlags{};`.
  - **Public API**:
    - `[[nodiscard]] bool ConsumeHandCancelRequest() noexcept`, returning `std::exchange(m_handButtons.cancelRequested,
      false)`,
    - `struct TakenHandButtons { std::optional<float> left; std::optional<float> right; };`
    - `[[nodiscard]] TakenHandButtons TakeHeldHandButtons();`, which calls `Input::TakeHeldForCast` on both hands and
      logs, for each one taken, `[Input] HandGate: {} button taken at cast start -> released in game (held={:.2f}s)`.
  - **In `ProcessAndFilter`**, right after the `spellSystemActive` / `activeSlot` lines, build `m_handFlags` from
    `MagicState::Get()`:
    - `pluginCastLeft = ms.IsHandPluginCast(Hand::Left)` and the same for `Right`,
    - `mayCancel = (pluginCastLeft || pluginCastRight) && !ms.IsPressMode()` (the `blockStart` rule, research R4).
  - **Menu close** (`m_prevBlocked && !blocked`): also call `m_handButtons.ResetHands();` and set
    `m_handButtons.cancelRequested = false;`.
  - **Replays**: where `DrainOneDeferredReplayEvent` returns `replayEvent`, inject it only if
    `!Input::detail::GateHandEvent(ev.userEvent, ev.value, ev.heldSecs, m_handButtons, m_handFlags)`. Replays bypass
    `FilterEvents` (research R3).
  - **`FilterEvents` call**: pass `m_handButtons, m_handFlags`.
  - **`ResetInputState`**: also `m_handButtons = {};`.
- [X] T006 In `include/Application/SpellSystemController.h` and `src/Application/SpellSystemController.cpp`, at the
  very start of `OnFrame`, before `DispatchSlotEvents()` (depends on T005; research R6):
  - Consume the transform-shout forced exit, a pre-existing gap the user approved fixing: `if (auto fx =
    InputController::Get().ConsumeForceExit()) HandleForceExitResult(std::move(*fx));`.
  - Consume the gate's cancel request:
    - `if (InputController::Get().ConsumeHandCancelRequest())`,
    - then, when `state.IsActive() && !state.IsPressMode()`, log `[SpellSystem] HandGate cancel -> ForceExit` and call
      `HandleForceExitResult(state.ForceExit());`.
  - Both run even when `inputBlocked`, like `DispatchSlotEvents`.

**Checkpoint**: the gate keeps the plugin-cast hand's presses from the game, cancels sessions on request, and the
domain constants exist. Both presets build.

---

## Phase 3: User Story 1 - Left-hand slots cast correctly around the block button (Priority: P1) 🎯 MVP

**Goal**: no twitching with block held before, or pressed during, a left-hand or dual-cast slot. A press cancels Hold
and Automatic, and does nothing in Press.

**Independent Test**: [quickstart.md](quickstart.md) §1 steps 1–5 (left-hand slots A, B), §2 and §4, on keyboard/mouse
and gamepad.

- [X] T007 [US1] In `include/Application/SpellSystemController.h` and `src/Application/SpellSystemController.cpp`, add
  the private const helper `void DispatchAttackPress(IntegratedMagic::Hand hand) const` (research R5, contract §3):
  - call `InputController::Get().TakeHeldHandButtons()`,
  - for each taken hand, `IntegratedMagic::detail::DispatchAttack(thatHand, 0.0f, *heldSecs)` (the release first),
  - then `IntegratedMagic::detail::DispatchAttack(hand, 1.0f, 0.0f)`.

  Replace the four synthetic **press** dispatches with it:
  - `DispatchSlotEvents`: `eqResult.dispatchLeft` and `eqResult.dispatchRight`,
  - `OnFrame`: `autoResult.startLeftAttack` and `autoResult.startRightAttack`.

  Keep their existing log lines. Do not change any value-0 dispatch. In dual cast both presses still go out in the
  same frame: the second call finds nothing left to take (Constitution IV).
- [X] T008 [US1] Build: debug first, then release **last** (the scratchpad `build.ps1`, presets `debug` / `release`).
  Expect 0 errors and 0 new compiler warnings. Delete `IntegratedMagic.pdb` from
  `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins` and check that the MO2 DLL hash matches
  `build\release\IntegratedMagic.dll`.
- [X] T009 [US1] **(in-game, user)** Validate [quickstart.md](quickstart.md) §1 steps 1–5 for slots A and B, §2 and §4
  (SC-001, SC-004). Depends on T021: the TEST_MATRIX cases it ticks must exist first.
  - **Expected**: no twitching. Block held before the slot is taken (`button taken at cast start`). A press cancels
    Hold and Automatic (`cancel session`, `HandGate cancel -> ForceExit`), does nothing in Press (`ignored (Press
    mode)`), and dual cast stays single.
  - **Afterwards**: tick TEST_MATRIX 8.9, 8.11, 8.12, 8.13, 8.15.

**Checkpoint**: the reported bug is fixed for the left hand (MVP).

---

## Phase 4: User Story 2 - Blocking with the other hand ends a slot at once (Priority: P1)

**Goal**: a shield block during a right-hand Hold or Automatic slot ends it and blocks at once. An interrupted cast
never leaves the slot active. A right-hand slot can be activated while blocking (patch OFF), and stays refused with
Require exclusive hotkey ON.

**Independent Test**: [quickstart.md](quickstart.md) §1 steps 2 (slot F) and 6, and §3, on keyboard/mouse and gamepad.

- [X] T010 [US2] In `src/Domain/MagicStatePump.cpp`, `OnCasterInterrupt` (depends on T003; research R12):
  - **New helper**: add the private member `void FinishAndFinalize(Hand hand, CastInterruptResult& result)` (declared
    in `include/Domain/State.h`). It calls `FinishHand(hand)`, stores the value in `result.finishedLeft` /
    `result.finishedRight`, then calls `ScheduleSpellFireFinalize(hand)`.
  - **Skip Channeling branch** (`hm.chargeSkipped`): replace its local `finishOne` lambda with the helper, with
    identical behavior.
  - **Spurious window**: replace the local `constexpr float kSpuriousInterruptWindow = 0.2f;` with
    `kSpuriousInterruptWindowSecs`.
  - **Last branch** (a real interruption, now `FinishHand(hand)` only):
    - log `[State] OnCasterInterrupt: hand={} real interrupt -> finish + finalize session`,
    - call `FinishAndFinalize(hand, result)`,
    - if `_session.isDualCasting` and the other hand is not finished, `FinishAndFinalize(other, result)`
      (Constitution IV).

  `SpellSystemController::OnCastInterrupted` already dispatches the `finished*` releases, so it needs no change.
- [X] T011 [US2] Confirm that the other-hand rule needs no extra code. It is covered by:
  - T002 (`PassCancel`) in `include/Input/HandButtonStore.h`,
  - T005 (`mayCancel`) in `src/Application/InputController.cpp`,
  - T006 (cancel consumption) and T007 (takeover of the held shield before a right-hand press) in
    `src/Application/SpellSystemController.cpp`.

  If review finds a gap, fix it in those files and note it in the implementation log.
- [X] T012 [US2] Rebuild as in T008.
- [X] T013 [US2] **(in-game, user)** Validate [quickstart.md](quickstart.md) §1 step 2 (slot F), step 6 (exclusive
  ON) and §3 (SC-002, SC-005).
  - **Expected**: the shield block ends Hold and Automatic slots and blocks at once (`other-hand press -> passes,
    cancel session`). Press is not cancelled by the press. No slot stays active after an interruption (`real
    interrupt -> finish + finalize session`). A right-hand slot activates while blocking with the patch OFF, and
    `REFUSED (not exclusive) extra=mouse:1` with it ON.
  - **Afterwards**: tick TEST_MATRIX 8.10, 8.14, 8.17.

**Checkpoint**: blocking ends slots, and interrupted slots close.

---

## Phase 5: User Story 3 - No stuck state after using block (Priority: P1)

**Goal**: after any block interaction, the next casts work on the first try. A taken button still held after a
session does nothing until pressed again.

**Independent Test**: [quickstart.md](quickstart.md) §5.

- [X] T014 [US3] Review the reset paths against data-model "Validation and reset rules":
  - a fresh press resets the fate (T002),
  - menu close resets the hands (T005),
  - `ResetInputState` resets the store (T005),
  - `cancelRequested` is consumed every frame (T006),
  - no gate state lives in `MagicState`, so `ForceExit` and every exit path leave it alone, and a taken button stays
    kept until its release (FR-003).

  Fix any gap in `include/Input/HandButtonStore.h` or `src/Application/InputController.cpp`.
- [X] T015 [US3] **(in-game, user)** Validate [quickstart.md](quickstart.md) §5 after the US1 and US2 runs (SC-003).
  - **Expected**: 10 out of 10 activations cast on the first try without touching block, and the click workaround is
    never needed. A taken button held after the session does nothing until it is pressed again (`button released ->
    gate cleared` once).
  - **Afterwards**: tick TEST_MATRIX 8.16.

**Checkpoint**: no lasting state survives a block interaction.

---

## Phase 6: User Story 4 - A cast that cannot start does not twitch forever (Priority: P2)

**Goal**: after about 1 s of failed starts, the session ends with the gear restored.

**Independent Test**: [quickstart.md](quickstart.md) §7.

- [X] T016 [US4] In `src/Domain/MagicStatePump.cpp` and `src/Domain/MagicStateSlot.cpp` (depends on T003; research
  R7):
  - **`RequestAutoAttackStart`**: set `hm.startFailSecs = 0.f;` next to `startRequestSecs`.
  - **`FinishHand`** (`MagicStateSlot.cpp`): set `hm.startFailSecs = 0.f;`.
  - **`PumpCastPhase`**: right after the `!_session.active || hm.finished` early return, and **before** the
    `pendingRestartNextFrame` branch, add `if (IsPluginCast(hm) && hm.autoCastPhase == AutoCastPhase::StartRequested)
    hm.startFailSecs += dt > 0.f ? dt : 0.f;`.
  - **Reset on a real cast**: in the existing `if (hm.autoCastPhase == AutoCastPhase::Casting)` block, after
    `castingElapsedSecs` grows, add `if (hm.castingElapsedSecs >= kSpuriousInterruptWindowSecs) hm.startFailSecs =
    0.f;`.
  - **Not reset by**: the deferred restart, `NotifyAttackEnabled` or the dual-cast sync. Leave those paths as they are.
- [X] T017 [US4] In `src/Domain/MagicStatePump.cpp`, `PumpAutomatic`: after the `pendingPowerRestore`,
  `pendingRestoreAfterSheathe` and `pendingRestore` branches, and before `const auto leftPhase = PumpCastPhase(Left,
  dt);`, add the give-up check (research R7, contract §4):
  - **Condition**: for `Left` then `Right`, if `_session.active && IsPluginCast(ModeFor(h)) &&
    ModeFor(h).autoCastPhase == AutoCastPhase::StartRequested && ModeFor(h).startFailSecs >= kMaxCastStartSecs`.
  - **Action**: log `[FLOW] PumpAutomatic: hand={} cast did not start in {:.2f}s -> ForceExit`, then
    `mergeForceExit(ForceExit()); return result;`, the same exit as the 30 s timeout.
  - **Dual cast**: `ForceExit` ends both hands (Constitution IV).
- [X] T018 [US4] Rebuild as in T008.
- [X] T019 [US4] **(in-game, user)** Validate [quickstart.md](quickstart.md) §7 (SC-006).
  - **Expected**: either a cast on landing, or within about 1 s the session ends with the gear restored (`cast did not
    start in 1.0Xs -> ForceExit`). No longer twitching.
  - **Afterwards**: tick TEST_MATRIX 8.18.

**Checkpoint**: the retry loop is bounded.

---

## Phase 7: User Story 5 - Block and manual casting stay vanilla when no slot is involved (Priority: P2)

**Goal**: no change outside plugin casts.

**Independent Test**: [quickstart.md](quickstart.md) §8.

- [X] T020 [US5] **(in-game, user)** Validate [quickstart.md](quickstart.md) §8 (SC-007).
  - **Expected**: block, bash and manual casting as in vanilla with no session, with no `HandGate` lines. Manual cast
    with the button in a Press session without auto-attack, 10 out of 10.
  - **Afterwards**: tick TEST_MATRIX 8.19.

---

## Phase 8: Polish & cross-cutting concerns

- [X] T021 [P] In `docs/TEST_MATRIX.md`, section 8, add the cases below after 8.8, in Portuguese and in the file's
  style. Each one is tested on keyboard/mouse and on gamepad (`LT`). It is listed here with the docs, but it **must be
  done before T009**: T009, T013, T015, T019 and T020 tick these cases.
  - 8.9: block held → left-hand slot (Hold/Automatic), exclusive OFF, shield and one-handed weapon.
  - 8.10: block held → right-hand slot, exclusive OFF; refused with it ON (`REFUSED ... extra=mouse:1`).
  - 8.11: block during a left-hand Hold or Automatic cast → cancel, gear restored, block only after a re-press.
  - 8.12: left-hand Press with auto-attack + block → no effect.
  - 8.13: quick block clicks during 8.11 / 8.12.
  - 8.14: right-hand slot with a shield + block → Hold/Automatic end at once and the character blocks; Press is not
    cancelled.
  - 8.15: dual cast with 8.9 and 8.11.
  - 8.16: no stuck state.
  - 8.17: an interrupted cast never leaves the slot active (no Automatic slot active for 30 s).
  - 8.18: activation at the start of a jump (1 s bound).
  - 8.19: vanilla block, bash and manual cast unaffected.
- [X] T022 [P] Update `.claude/CLAUDE.md`:
  - **`HandMode`**: add `startFailSecs` to the field list.
  - **Automatic mode — cast phase loop**: in step 1, add the 1.0 s give-up (`kMaxCastStartSecs`, `ForceExit`), and add
    the real-interrupt exit (finish + finalize, dual partner included).
  - **Input system**: add a paragraph "Hand-button gate (spec 005)":
    - user events `"Left Attack/Block"` / `"Right Attack/Block"`,
    - it runs after the combo rules and on replays,
    - plugin-cast hand presses are kept (cancel in Hold/Automatic, ignored in Press),
    - other-hand presses pass and cancel,
    - the takeover at the plugin's first press,
    - `HandButtonStore` and its resets,
    - the log lines of contract §6.
  - **Common tasks row**: "Block / left-hand twitching or a slot not ending" → `include/Input/HandButtonStore.h`,
    `src/Input/InputFilter.cpp` (`GateHandEvent`), `src/Application/SpellSystemController.cpp`
    (`DispatchAttackPress`, `OnFrame`), `src/Domain/MagicStatePump.cpp` (`OnCasterInterrupt`, `PumpAutomatic`).
- [X] T023 Run clang-format in dry-run mode on the changed line ranges only (pass `--lines` through `@()`), and fix
  any violations. The files are `include/Input/HandButtonStore.h`, `include/Input/InputFilter.h`,
  `src/Input/InputFilter.cpp`, `include/Application/InputController.h`, `src/Application/InputController.cpp`,
  `include/Application/SpellSystemController.h`, `src/Application/SpellSystemController.cpp`,
  `include/Domain/State.h`, `src/Domain/MagicStatePump.cpp` and `src/Domain/MagicStateSlot.cpp`.
- [X] T024 Final build as in T008, with no new warnings. Check the scope with `git diff --stat -- src/Config
  include/Config SKSE`: it must be empty.
- [X] T025 **(in-game, user)** Regression: [quickstart.md](quickstart.md) §6 and §9 (SC-009). TEST_MATRIX 1.1–1.6, 2.x,
  4.1–4.3, 6.1–6.2, 8.1 and spec 004 6.15–6.23, on the final build. Tick what passes, and record any failure with its
  log excerpt.
- [X] T026 Pre-commit review:
  - set `**Status**` in `specs/005-fix-left-hand-block/spec.md`,
  - add an implementation log section to this file (deviations, T001 outcome, known edge cases),
  - review `git diff` of every file touched,
  - remind the user to commit and push only when asked.

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1** (T001): optional, any time before T009.
- **Phase 2**: T002 and T003 in parallel, then T004 (needs T002), then T005 (needs T002–T004), then T006 (needs
  T005). **Blocks every user story.**
- **Phase 3 (US1)**: T007 → T008 → T009. T009 also needs T021.
- **Phase 4 (US2)**: T010 needs T003; T011 needs T002 and T005–T007; then T012 → T013.
- **Phase 5 (US3)**: T014 after Phase 2 and T007; T015 after T009 and T013.
- **Phase 6 (US4)**: T016 needs T003; T017 needs T016; then T018 → T019.
- **Phase 7 (US5)**: T020 after any build that includes Phase 2.
- **Phase 8**:
  - T021 is done **before T009**, since every in-game validation task ticks the cases it creates.
  - T022 can be done at any time after the design is stable.
  - T023 → T024 → T025 → T026 last.

### User story dependencies

- **US1 (P1)**: needs Phase 2 only. It is the MVP.
- **US2 (P1)**: needs Phase 2. It reuses T007 for activating a right-hand slot while blocking. T010 is independent of
  US1.
- **US3 (P1)**: validation of the Phase 2 reset rules after US1 and US2 runs.
- **US4 (P2)**: needs T003 only. Independent of the gate.
- **US5 (P2)**: needs Phase 2.

### Same-file sequencing

- `src/Domain/MagicStatePump.cpp`: T010 → T016 → T017.
- `src/Application/SpellSystemController.cpp`: T006 → T007.
- `src/Application/InputController.cpp`: T005, then T014 if it fixes anything.

---

## Parallel Example

```text
# Phase 2 start:
T002  include/Input/HandButtonStore.h        (pure gate state and decision)
T003  include/Domain/State.h                 (HandMode::startFailSecs, constants, IsPluginCast)

# After Phase 2, the domain work can run beside the gate wiring:
T010  src/Domain/MagicStatePump.cpp           (US2: real interrupt → finish + finalize)
T007  src/Application/SpellSystemController.cpp (US1: DispatchAttackPress)

# Docs at any time:
T021  docs/TEST_MATRIX.md
T022  .claude/CLAUDE.md
```

---

## Implementation Strategy

### MVP first (US1)

1. Phase 2 (T002–T006), then US1 (T007–T009).
2. **Stop and validate**: the reported left-hand bug is fixed in game.

### Incremental delivery

1. US2 (T010–T013): the shield block ends slots, and interrupted slots close. This is the user's own report and should
   follow right after the MVP.
2. US3 (T014–T015): no stuck state, validated after US1 and US2.
3. US4 (T016–T019): the retry bound.
4. US5 (T020) and Polish (T021–T026).

## Notes

- **Known edge case**: a player press that reaches the game in the very frame a session starts is dispatched after the
  plugin's first press of that frame (R1 frame order). The takeover still keeps its held repeats and release from the
  game, and the retry loop absorbs the one-frame overlap. Record it in the implementation log if it shows in testing.
- Commit or push only when the user asks.

## Implementation log (2026-11-20)

- **Code and doc tasks done**: T002–T008, T010–T012, T014, T016–T018, T021–T024, T026.
  - **Builds**: debug first, then release last, with 0 errors and 0 compiler warnings (`build-005-debug.log`,
    `build-005-release.log`).
  - **MO2**: the DLL hash matches `build\release\IntegratedMagic.dll`, and the debug PDB was removed.
  - **clang-format**: 0 violations on the changed line ranges and in the new `HandButtonStore.h`.
  - **Scope**: `git diff --stat -- src/Config include/Config SKSE` is empty. No INI or setting change (FR-013).
- **Deviations from the task text**, all with the same behavior:
  - **One build covers T008, T012 and T018**: all code landed before the first build, so the three rebuilds are one.
  - **`ConsumeInputExitRequests()`** is a private helper in `SpellSystemController`, called first in `OnFrame` (T006).
    It holds both the transform-shout forced exit and the gate's cancel.
  - **`BuildHandGateFlags`** is a helper in the anonymous namespace of `InputController.cpp`, the name the plan uses
    (T005).
  - **`HandOfUserEvent`, `PhaseOf` and `HandName`** live in the anonymous namespace of `InputFilter.cpp`, as T004
    says. The plan's "in the header" wording (analysis finding L3) was not applied.
  - **`FinishAndFinalize`** also replaces the Skip Channeling branch's local lambda (T010), with identical behavior.
- **Behavior note** (T006, approved by the user): the transform-shout forced exit now actually runs. It is
  `ForceExitNoRestore`, so the session ends **without** a gear restore, which is what that path was written to do.
  Before spec 005 its result was dropped. An empty result (no session) only writes the `HandleForceExitResult` log line.
- **T011**: no extra code needed. The other-hand rule is `PassCancel` in `DecideHandEvent`, plus `mayCancel`, plus
  `ConsumeInputExitRequests`, plus the takeover in `DispatchAttackPress`.
- **T014**: every reset rule of the data model is in place:
  - a fresh press resets the fate,
  - menu close calls `ResetHands` and clears `cancelRequested`,
  - `ResetInputState` resets the store,
  - `cancelRequested` is consumed every frame,
  - no gate state lives in `MagicState`.
- **T001** (optional baseline log) was not run.
- **Pending in game** (user, release DLL, approved): T009, T013, T015, T019, T020, T025, and optionally T001.
  T021 is done, so the TEST_MATRIX cases 8.9–8.19 exist for them to tick.
- **First in-game attempt blocked by Windows, not by the code** (2026-11-20, 19:28):
  - `skse64.log`: `plugin IntegratedMagic.dll (... 01060001) couldn't load plugin 4551`. Error 4551 is
    `ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION`.
  - The Windows log `Microsoft-Windows-CodeIntegrity/Operational` has events 3077/3033 for this DLL with policy
    `{0283ac0f-fff1-49ae-ada1-8a933130cad6}`.
  - Smart App Control is ON (`VerifiedAndReputablePolicyState = 1`).
  - **Cause**: the unsigned DLL has no reputation, and Windows blocks it before any plugin code runs. This is also the
    likely cause of the "DEBUG DLL fails to load" noted since spec 002.
  - **Fix options** are on the user's side: turn Smart App Control off, or sign the DLL with a certificate Windows
    trusts.
- **Second attempt**: after a forced full recompile of the plugin (release, 51 sources, 0 warnings, MO2 hash
  matching), the DLL loaded.
- **In-game validation**: the user reported every test passing (2026-11-20, release DLL): T009, T013, T015, T019,
  T020 and T025.
  - TEST_MATRIX 8.1 and 8.9–8.19 are ticked.
  - The broader regression cells listed in T025 that were not already ticked (1.2, 1.4, 1.6, 2.x, 4.1–4.3) are left
    for the user to tick.
- **T001** (optional baseline) was not run; it is not needed after the fix passed.
