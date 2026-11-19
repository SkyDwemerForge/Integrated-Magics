---

description: "Task list for fixing the Skip Channeling restart loop on bound weapon spells"
---

# Tasks: Fix Skip Channeling patch looping on bound weapon spells

**Input**: Design documents from `specs/002-fix-skip-channeling-bound/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md),
[contracts/skip-channeling.md](contracts/skip-channeling.md), [quickstart.md](quickstart.md)

**Tests**: no automated test tasks. The spec does not request them, and no harness can load the game
(Constitution VI). Validation tasks are in-game runs driven by [quickstart.md](quickstart.md) and
`docs/TEST_MATRIX.md`. Tasks marked **(in-game)** must be run by the user, **always with the DEBUG DLL deployed**:
every build task builds `debug` last, so the post-build step copies the DEBUG DLL and PDB to MO2 (Constitution VI).

**Organization**: tasks are grouped by user story so each story can be implemented and validated on its own.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: user story from spec.md (US1, US2, US3)
- Paths are relative to the repository root unless they are absolute.

---

## Phase 1: Setup

**Purpose**: a clean test environment, so the logs are attributable to the new build.

- [ ] T001 **(in-game)** Per the [quickstart.md](quickstart.md) prerequisites, in MO2 enable only the
  `IntegratedMagic` mod (build output) and disable the older `D:\ModdingSkyrim\MO2\mods\Integrated Magic` package.
  Both ship an `IntegratedMagic.dll` (research R9).
  - Prepare a test character with Bound Sword, Bound Bow, Fireball and Firebolt (or Candlelight).
  - All in-game tasks run with the DEBUG DLL that the build tasks deploy (debug is built last).
  - **Deviation accepted by the user (2026-11-19)**: the DEBUG DLL fails to load, so the in-game tasks run on the
    **release** DLL, built last and deployed to MO2. It writes the same log lines. Recorded in plan.md Complexity
    Tracking (Principle VI).
  - Note: the old `Integrated Magic` package also holds `IntegratedMagic.ini` and the distributed
    `IntegratedMagic_Strings.txt`. Disabling it drops both. The log already shows that the build-output DLL wins.

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: new state and intents, moving the game write to the outbound layer, removing the manual magicka drain,
diagnostics, and the developer-guide update for the new flag, intent and adapter. Every user story depends on this
phase.

**⚠️ CRITICAL**: no user story work can begin until this phase is complete.

- [X] T002 [P] In `include/Domain/State.h`, add `bool chargeSkipped{false};` to `struct HandMode`, right after
  `bool needsManualFireInKReady{false};`.
  - Add the comment: `// Skip Channeling forced this hand's charge in the current attempt (reset per attempt)`.
  - The data-model rule applies verbatim: "no flag may survive into the next session". Value-reset paths
    (`EnterHand`'s `hm = {}`, `ResetHandStates`' `_left = {}` / `_right = {}`, `ForceExit`) already cover it. T008
    adds the explicit resets.
- [X] T003 [P] In `include/Shared/PumpResults.h`:
  - Add `bool forceChargeComplete{false};` and `bool chargeSkipEligible{false};` to `struct PumpCastPhaseResult`
    (after `releaseAttack`). `chargeSkipEligible` is used in dual cast only; T006 pairs it (research R11, FR-012).
  - Add `bool forceChargeLeft{false};` and `bool forceChargeRight{false};` to `struct PumpAutomaticResult` (after
    `releaseRightAttack`).
- [X] T004 [P] Add the outbound adapter `ForceChargeComplete` in `include/Adapters/Outbound/MagicEquip.h` and
  `src/Adapters/Outbound/MagicEquip.cpp` (research R4, contract §2).
  - **Declaration**: in `include/Adapters/Outbound/MagicEquip.h`, inside `namespace IntegratedMagic::MagicAction`,
    declare `void ForceChargeComplete(RE::PlayerCharacter* player, Hand hand);`.
  - **Implementation**: in `src/Adapters/Outbound/MagicEquip.cpp`:
    - Return if `!player`.
    - Map `hand` to `RE::MagicSystem::CastingSource::kLeftHand` / `kRightHand`.
    - `auto* caster = player->GetMagicCaster(src); if (!caster) return;`
    - Log `MAGIC_DEBUG_LOG("[Action] ForceChargeComplete: hand={} castingTimer={:.3f} -> 0", ...)`.
    - Set `caster->castingTimer = 0.f;`.
  - **No magicka write.**
- [X] T005 Rewrite the Skip Channeling block in `src/Domain/MagicStatePump.cpp` (`PumpCastPhase`, the
  `if (hm.autoActive && hm.autoCastPhase == AutoCastPhase::Casting && hm.waitingChargeComplete)` block, currently
  lines ~373–393). Depends on T002–T004.
  - **(a)** Delete the `GetMagicCaster(...)` / `DamageActorValue(kMagicka, ...)` / `mc->castingTimer = 0.f` code.
    Domain must not write game state (Constitution I).
  - **(b)** New eligibility condition, using the plugin's own classifier (research R12). Add
    `#include "Shared/SpellTypeDetector.h"` and compute `const auto type = DetectSpellType(spell);`, then:
    `skipCh && !hm.chargeSkipped && castStateEnum == RE::MagicCaster::State::kUnk02 && spell->GetChargeTime() > 0.f
    && (type == SpellType::Cast || type == SpellType::Bound)`. Bound weapon spells qualify (FR-002); Concentration,
    Power and Unknown never do.
  - **(c)** In dual cast (`_session.isDualCasting`), when the condition holds: only set
    `result.chargeSkipEligible = true`.
    Do **not** set `hm.chargeSkipped` or `result.forceChargeComplete` here; T006 pairs both hands in `PumpAutomatic`
    (research R11, Constitution IV).
  - **(c2)** Outside dual cast, when the condition holds: set `hm.chargeSkipped = true;` and
    `result.forceChargeComplete = true;`, and log
    `MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: applied hand={} spell={:#010x} '{}' chargeTime={:.2f} castingTimer={:.3f}
    cost={:.1f} magicka={:.1f}", ...)`. Use `caster->castingTimer`, `GetSpellMagickaCost(player, spell)` and
    `GetPlayerMagicka(player)` (`Shared/InventoryUtil.h`, already included). Leave `chargeComplete` false this frame.
  - **(d)** Otherwise keep `chargeComplete = IsChargeComplete(caster, spell);`.
  - **(e)** In `ConfirmAutoCastStarted` (same file, line ~39), when `Config::MagicConfigAdapter::Get().SkipChanneling()`
    is ON and the hand's slot spell (`Slots::GetSlotSpell(_session.activeSlot, hand)`) is not eligible, log once
    `MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: skipped hand={} spell={:#010x} reason={}", ...)`, with reason
    `chargeTime=0` or `type=Concentration|Power|Unknown` from `DetectSpellType` (contract §4).
- [X] T006 In `src/Domain/MagicStatePump.cpp` `PumpAutomatic`, merge and pair the forced-charge intents (depends on
  tasks T003 and T005; same file).
  - **(a)** Extend the `mergePumpPhase` lambda: when `src.forceChargeComplete`, set `result.forceChargeLeft = true`
    for the left hand, otherwise `result.forceChargeRight = true`.
  - **(b)** Capture both phase results instead of passing temporaries:
    `const auto leftPhase = PumpCastPhase(Left, dt); const auto rightPhase = PumpCastPhase(Right, dt);
    mergePumpPhase(Left, leftPhase); mergePumpPhase(Right, rightPhase);`
  - **(c)** Dual-cast pairing (FR-012, research R11, Constitution IV). Right after the merges, before the existing
    `syncForRestart` block: `if (_session.isDualCasting && leftPhase.chargeSkipEligible &&
    rightPhase.chargeSkipEligible)` then
    - set `_left.chargeSkipped = _right.chargeSkipped = true;`
    - set `result.forceChargeLeft = result.forceChargeRight = true;`
    - log `MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: applied dual L+R spell={:#010x} '{}' castingTimerL={:.3f}
      castingTimerR={:.3f} magicka={:.1f}", ...)` (contract §4).
  - **(d)** If only one hand is eligible in dual cast, emit nothing: that hand keeps charging naturally until the
    other also reaches `kUnk02`.
- [X] T007 [P] In `src/Application/SpellSystemController.cpp` `OnFrame`, right after
  `const auto autoResult = IntegratedMagic::MagicState::Get().PumpAutomatic(dt);` and **before** the
  `releaseLeftAttack` dispatch, add:
  `if ((autoResult.forceChargeLeft || autoResult.forceChargeRight)) if (auto* pc = RE::PlayerCharacter::GetSingleton())
  { if (autoResult.forceChargeLeft) IntegratedMagic::MagicAction::ForceChargeComplete(pc, IntegratedMagic::Hand::Left);
  if (autoResult.forceChargeRight) IntegratedMagic::MagicAction::ForceChargeComplete(pc, IntegratedMagic::Hand::Right); }`.
  `MagicEquip.h` is already included. Depends on T003 and T004.
- [X] T008 Reset `chargeSkipped` in `src/Domain/MagicStateSlot.cpp` and `src/Domain/MagicStatePump.cpp` at the
  start of each attempt and on finish (data-model lifecycle).
  - `src/Domain/MagicStateSlot.cpp` `FinishHand`: add `hm.chargeSkipped = false;` after `hm.castingElapsedSecs = 0.f;`.
  - `src/Domain/MagicStatePump.cpp`:
    - `RequestAutoAttackStart`: add it after `hm.startRequestSecs = 0.f;`.
    - `OnCasterInterrupt`, post-cast spurious branch: add it after `hm.castingElapsedSecs = 0.f;`.

  Depends on T002 and T005.
- [X] T009 In `src/Domain/MagicStatePump.cpp` `OnSpellFired`, right after `auto& hm = ModeFor(hand);`, log the cost
  evidence when `hm.chargeSkipped`:
  `MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: fired hand={} magicka={:.1f}", IsLeft(hand) ? "L" : "R",
  GetPlayerMagicka(GetPlayer()))`. Depends on T008 (same file).
- [X] T010 [P] Update `.claude/CLAUDE.md` in the same change that adds the flag, intent and adapter (Constitution II
  "documented in `.claude/CLAUDE.md` in the same change"; merge gate 4):
  - **(a)** In the `HandMode` field list, add `chargeSkipped` (Skip Channeling forced this hand's charge; an
    interruption while set is a real failure, never spurious).
  - **(b)** In "Automatic mode — cast phase loop" step 2, replace the SkipChanneling description ("zeros
    castingTimer immediately (after draining magicka)...") with: the domain emits `forceChargeComplete` once per
    attempt for spells classified `Cast` or `Bound` (`DetectSpellType`), and `MagicAction::ForceChargeComplete`
    zeroes `castingTimer`. No magicka is drained, so the game charges the normal cost. In dual cast the forced charge
    is paired in `PumpAutomatic` (both hands in the same frame, or neither). A failure after the skip ends the
    hand(s) with no retry.
  - **(c)** In "Outbound adapters" → `MagicAction`, add `ForceChargeComplete(player, hand)`.
  - **(d)** In the "Common tasks → files" table, extend the "Skip channeling behavior" row with
    `src/Adapters/Outbound/MagicEquip.cpp` and `OnCasterInterrupt` (chargeSkipped).

  If T016 later adds the magicka-remainder fallback, update (b) and (c) again in that task.
- [X] T011 Build both presets into `build/release` and `build/debug` from a VS Developer PowerShell (x64):
  `cmake --preset release; cmake --build build/release`, then `cmake --preset debug; cmake --build build/debug`. **Debug goes last**, so MO2 receives the
  DEBUG DLL and PDB for in-game validation (Constitution VI). Expect 0 errors and no new warnings. Confirm
  `git grep -n "DamageActorValue\|castingTimer = " -- src/Domain` returns nothing.

**Checkpoint**: the patch no longer drains magicka, the game write lives in the outbound adapter, the
`[FLOW] SkipChanneling:` diagnostics are in place, `.claude/CLAUDE.md` documents the change, and the DEBUG DLL is
deployed.

---

## Phase 3: User Story 1 - Bound weapon slot works with Skip Channeling enabled (Priority: P1) 🎯 MVP

**Goal**: a bound weapon spell cast automatically with the patch ON summons once, with no loop; a failure after a
forced charge ends cleanly; the cost model is confirmed (FR-001–FR-004, FR-009–FR-011).

**Independent Test**: [quickstart.md](quickstart.md) §1–§4 pass on the DEBUG build. One `SkipChanneling: applied`,
then `fired`, no `spurious → UP` lines, the weapon appears, and control returns within 3 s.

### Implementation for User Story 1

- [X] T012 [US1] In `src/Domain/MagicStatePump.cpp` `OnCasterInterrupt`, right after the
  `[State] OnCasterInterrupt: hand={} phase={} ...` log and **before** the `StartRequested` branch, add the
  forced-charge failure path (research R3, contract §3). Depends on T008.

  ```cpp
  if (hm.chargeSkipped) {
      MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: interrupted after skip hand={} magicka={:.1f} -> finish (no retry)", ...);
      auto finishOne = [&](Hand h) {
          const float f = FinishHand(h);
          if (IsLeft(h)) result.finishedLeft = f; else result.finishedRight = f;
          ScheduleSpellFireFinalize(h);
      };
      finishOne(hand);
      if (_session.isDualCasting) {
          const Hand other = IsLeft(hand) ? Hand::Right : Hand::Left;
          if (!ModeFor(other).finished) finishOne(other);
      }
      return result;
  }
  ```

  `SpellSystemController::OnCastInterrupted` already releases each hand with `finished* != -1` (UP), and
  `PumpSpellFireFinalize` ends the session within 0.7 s.
- [X] T013 [P] [US1] In `docs/TEST_MATRIX.md`, add a subsection `### 5b. Skip Channeling × Bound weapons` after the
  section 5 table (in Portuguese, matching the file), with unchecked items (FR-010):
  - **5b.1** Bound Sword, Automatic, right hand → summons once, no pose de carga, arma aparece, sem loop.
  - **5b.2** same on the left hand.
  - **5b.3** Bound Sword, Press + auto-attack.
  - **5b.4** dual bound (same spell in both hands).
  - **5b.5** mixed slot: Bound Sword right + Firebolt left.
  - **5b.6** Bound Bow (two-handed).
  - **5b.7** Skip Equip Animations ON + Skip Channeling ON → no loop (note whether the weapon draw is shortened).
  - **5b.8** magicka spent with the patch ON equals the patch OFF (±1) for Bound Sword, Fireball and Firebolt.
  - **5b.9** Bound Sword já invocada (arma ainda ativa na mão) → ativar o slot de novo com o patch ON → sem loop;
    resultado igual ao do patch OFF (spec, caso de borda "bound weapon already summoned").
  - **5b.10** Bloquear, sofrer stagger ou abrir um menu durante a invocação com o patch ON → a sessão termina, o
    equipamento é restaurado e não há loop. O log mostra `ForceExit` ou `SkipChanneling: interrupted after skip`
    (spec, caso de borda "interruption by the player").
  - **5b.11** Magia de concentração (Flames) em Automatic com o patch ON → carga e canalização normais, sem efeito do
    patch. O log mostra `SkipChanneling: skipped ... reason=type=Concentration`.
- [X] T014 [US1] Rebuild both presets, release first and **debug last** (`cmake --build build/release`, then
  `cmake --build build/debug`), so MO2 keeps the DEBUG DLL. Expect 0 errors and no new warnings.
- [X] T015 [US1] **(in-game)** On the DEBUG build, run the diagnostic gate per [quickstart.md](quickstart.md) §1:
  Bound Sword, Fireball and Firebolt, patch OFF vs ON, recording magicka before the press and after the cast. Note
  whether the vanilla bar drains while charging or at release.

  Append a `**Gate result (<date>)**:` line under R2 in `specs/002-fix-skip-channeling-bound/research.md`, either
  `parity` or `under-charge by N`.

  **Done (2026-11-19)**: **under-charge**, by `cost − one frame` (Bound Sword spent 2.8 of 82.7, Candlelight 0.6 of
  18.6). Recorded under R2. It ran on the **release** build because the DEBUG DLL fails to load (SKSE reports
  "fatal error occurred while loading plugin"). The release build writes the same `[FLOW] SkipChanneling:` lines
  (`PCH.h` defines `DEBUG`). This is a deviation from Constitution Principle VI, pending the DEBUG load fix.
- [X] T016 [US1] In `src/Domain/MagicStatePump.cpp` and `src/Adapters/Outbound/MagicEquip.cpp`, **only if T015 =
  under-charge**, implement the R2 fallback (contract §2). If T015 = parity, mark this task `[X]` with the note "not
  needed (parity)".

  The fallback has four parts:

  - **(a)** Add `float forceChargeDrain{0.f};` to `PumpCastPhaseResult`, and `forceChargeDrainLeft` /
    `forceChargeDrainRight` to `PumpAutomaticResult`, in `include/Shared/PumpResults.h`.
  - **(b)** In `PumpCastPhase` (`src/Domain/MagicStatePump.cpp`), compute
    `remainder = cost * caster->castingTimer / spell->GetChargeTime()`.
    - If `GetPlayerMagicka(player) + 0.01f < remainder`, do **not** apply the skip: log
      `skipped ... reason=not affordable` and let the spell charge normally.
    - Otherwise emit the intent with `forceChargeDrain = remainder`.
  - **(c)** Merge the drain in `PumpAutomatic` and pass it through `SpellSystemController::OnFrame`.
  - **(d)** Change the adapter to `ForceChargeComplete(player, hand, float magickaToDrain)`. It calls
    `player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, magickaToDrain)` when
    `magickaToDrain > 0` and then zeroes the timer.

  Also update `.claude/CLAUDE.md` (T010 items (b) and (c)) to describe the remainder drain. Rebuild (release, then
  debug last) and repeat T015.

  **Done (2026-11-19), with two deviations from (b)–(d)**. Both are recorded under R2.

  - **Where the drain happens.** The owed amount is recorded at the skip in `HandMode::skipMagickaOwed`, as
    `cost × clamp(castingTimer − frame, 0, chargeTime) / chargeTime`, minus the frame the game still drains. It is
    returned by `OnSpellFired` as `SpellFiredResult::magickaToDrain`, and drained by the new
    `MagicAction::DrainMagicka` from `SpellSystemController::NotifyAnimEvent`. That happens **after the spell
    fires**, not inside `ForceChargeComplete` during the charge. Draining during the charge is what made the game
    cancel the cast (R1).
  - **Affordability.** The "not affordable" pre-check is replaced by clamping the drain at 0.
  - **Dual cast.** Each hand owes half of `cost × GetDualCastCostMultiplier`, and the first fire event pays both.
  - **Build order.** Debug was built first and **release last**, so MO2 holds the loadable release DLL. The stale
    debug PDB was removed from MO2.
  - **Re-run of T015 (2026-11-19): parity.** Bound Sword 82.7 of 82.7, Candlelight 18.6 of 18.6, confirmed by the
    user. Fireball, Firebolt and dual cast are still to be measured, under T017 (5b.8) and T018 / T025.
- [ ] T017 [US1] **(in-game)** On the DEBUG build, validate [quickstart.md](quickstart.md):
  - **§2**: 20 activations covering Automatic right, Automatic left and Press + auto-attack.
  - **§3**: the summon interruption arrives after `OnSpellFired`.
  - **§4**: a forced failure ends with `interrupted after skip ... -> finish (no retry)`.

  Tick TEST_MATRIX 5b.1–5b.3 and 5b.8 in `docs/TEST_MATRIX.md`.

**Checkpoint**: bound weapons work with Skip Channeling. MVP is complete.

---

## Phase 4: User Story 2 - Other spells keep their current Skip Channeling behavior (Priority: P2)

**Goal**: no regression for non-bound fire-and-forget spells, dual cast, mixed slots, the patch OFF, or the
spec's bound edge cases (FR-005–FR-007).

**Independent Test**: on the DEBUG build, TEST_MATRIX 5.4, 5.5 and 1.3 pass with the patch ON; 3.7–3.9 and 5.1 are
unchanged with the patch OFF; 5b.9 and 5b.10 pass.

### Implementation for User Story 2

- [ ] T018 [US2] **(in-game)** On the DEBUG build, validate [quickstart.md](quickstart.md) §5 and §6 (depends on
  task T017).
  - **§5 interactions**: dual bound (5b.4), mixed slot (5b.5), Bound Bow (5b.6), Skip Equip Animations ON (5b.7).
  - **§5 edge cases**: bound weapon already summoned (5b.9); block, stagger or menu during the summon (5b.10).
  - **§5 dual-cast pairing** (FR-012): for 5b.4 and 1.3, the log shows a single `applied dual L+R` line and a single
    dual cast occurs.
  - **§6 regression with the patch ON**: 5.4, 5.5, 1.3, and concentration not affected (5b.11).
  - **§6 regression with the patch OFF**: 3.7, 3.8, 3.9, 5.1.

  Tick the boxes in `docs/TEST_MATRIX.md`. Report to the user whether 5b.7 shortens the weapon draw (accepted per
  the spec edge case), and which exit path 5b.10 took (`ForceExit` vs `interrupted after skip`).

**Checkpoint**: US1 and US2 validated.

---

## Phase 5: User Story 3 - The patch description matches what it does (Priority: P3)

**Goal**: an accurate tooltip and design note (FR-008).

**Independent Test**: the hover text in the Patches tab matches [contracts/skip-channeling.md](contracts/skip-channeling.md)
§5, with the distributed strings file present.

### Implementation for User Story 3

- [X] T019 [P] [US3] In `src/UI/MENU.cpp`, replace the default text of
  `IntegratedMagic::Strings::Get("Tooltip_SkipChanneling", ...)` (lines ~1406–1409) with:
  `"When enabled, Fire-and-Forget spells cast automatically (Automatic mode,\nor Press with auto-attack) skip the
  hand charging animation and fire instantly.\nBound weapons are included; their summon animation is kept.\n
  Concentration spells are not affected."`, split into adjacent string literals as in the current code.
- [X] T020 [P] [US3] In `D:\ModdingSkyrim\MO2\mods\Integrated Magic\SKSE\Plugins\IntegratedMagic_Strings.txt`
  (outside the repo, not versioned), replace the `Tooltip_SkipChanneling=` line with the same text as T019. Keep the
  file's `Key=Value` format and its `\n` escapes. Read the file first and preserve its encoding.
- [X] T021 [P] [US3] In `.claude/tasks/P1-skip-channeling-patch.md`, mark the note as superseded and correct it
  (FR-008, research R9). The note describes an approach that was never implemented.
  - **(a)** Add a banner right below the title (in Portuguese, matching the file):
    `> **Superseded (2026-11-19):** este design (CastSpellImmediate) nunca foi implementado. O patch real completa a
    carga zerando castingTimer — ver specs/002-fix-skip-channeling-bound/ e
    specs/002-fix-skip-channeling-bound/contracts/skip-channeling.md.`
  - **(b)** Update the "Escopo de aplicação" table:
    - Automatic and Press with auto-attack apply.
    - `Bound` is included; only the charging animation is skipped and the summon is kept (decision 2026-11-19).
    - Concentration, Power and Shout do not apply.
    - Hold does not apply.
    - Classification uses `DetectSpellType` (`Cast` or `Bound`).
  - **(c)** Before the "Arquivos a modificar" section, add a line stating that the sections "Arquivos a modificar",
    "Implementação" 1–8, "Comportamento esperado" and the expected log lines (`[Action] CastHandImmediate`,
    `[State] OnImmediateCastComplete`) are **historical, not implemented**.
  - **(d)** Keep the "Considerações importantes" and "Teste" sections, marking `castStopsToSkip` as "não existe mais
    no código".
- [ ] T022 [US3] **(in-game)** On the DEBUG build, validate [quickstart.md](quickstart.md) §7: hover Skip Channeling
  in the Patches tab; the text matches contract §5. Depends on T019, T020 and a rebuild with debug last (T024).

**Checkpoint**: all three user stories validated.

---

## Phase 6: Polish & cross-cutting concerns

- [X] T023 [P] Run clang-format on the touched line ranges of `src/Domain/MagicStatePump.cpp` and the other touched
  files, using `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe
  --style=file --lines=<range>`. The files are `include/Domain/State.h`, `include/Shared/PumpResults.h`,
  `include/Adapters/Outbound/MagicEquip.h`, `src/Adapters/Outbound/MagicEquip.cpp`, `src/Domain/MagicStatePump.cpp`,
  `src/Domain/MagicStateSlot.cpp`, `src/Application/SpellSystemController.cpp` and `src/UI/MENU.cpp`. Verify with
  `--dry-run` that no pre-existing lines changed.
- [X] T024 Final build of both presets into `build/release` and `build/debug`, release first and **debug last**,
  with 0 errors and no new warnings (merge gate 1).
  - `build/release/IntegratedMagic.dll` is the distribution artifact.
  - MO2 keeps the DEBUG DLL for the remaining in-game tasks (T022, T025).
  - Confirm both DLLs contain `SkipChanneling: applied` and the new tooltip text.
- [ ] T025 **(in-game)** On the DEBUG build, run the dual-cast regression and record it in `docs/TEST_MATRIX.md`
  (Constitution IV): TEST_MATRIX 1.1, 1.3 and 1.5, plus baseline 5.1 (merge gate 3). Tick them in
  `docs/TEST_MATRIX.md`.
- [X] T026 Pre-commit review of `specs/002-fix-skip-channeling-bound/spec.md` and all touched files.
  - Set `**Status**` in `specs/002-fix-skip-channeling-bound/spec.md` to `Implemented` (noting any pending in-game
    tasks).
  - Run `git grep -n "DamageActorValue\|castingTimer = " -- src/Domain` and confirm it returns nothing.
  - Review `git diff` for every touched file, including `.claude/CLAUDE.md` from T010.
  - Remind the user that T020 changed a file outside the repo.

---

## Dependencies & execution order

### Phase dependencies

- **Setup (T001)**: needed before any in-game task. Code tasks can proceed in parallel with it.
- **Foundational (T002–T011)**: block all stories. T010 (CLAUDE.md) lands in the same change as the code it
  documents.
- **US1 (T012–T017)**: after Foundational. T015 (gate) blocks T016, which blocks T017.
- **US2 (T018)**: needs US1 validated (T017), because the cost handling must be final.
- **US3 (T019–T022)**: code and doc tasks T019–T021 can start right after Foundational. T022 needs the final build
  (T024).
- **Polish (T023–T026)**: after the stories. T024 must come after T019.

### Build order rule (Constitution VI)

Every build task (T011, T014, T016 rebuild, T024) builds `release` first and `debug` **last**, so the DLL in MO2
during every **(in-game)** task is the DEBUG build. The release DLL stays in `build/release` for distribution.

### Same-file sequencing (cannot be parallel)

- `src/Domain/MagicStatePump.cpp`: T005 → T006 → T008 (part) → T009 → T012 → (T016 if needed)
- `include/Shared/PumpResults.h`: T003 → (T016 if needed)
- `include/Adapters/Outbound/MagicEquip.h` / `src/Adapters/Outbound/MagicEquip.cpp`: T004 → (T016 if needed)
- `src/Application/SpellSystemController.cpp`: T007 → (T016 if needed)
- `.claude/CLAUDE.md`: T010 → (T016 if needed)
- `docs/TEST_MATRIX.md`: T013 → T017 → T018 → T025 (checkbox updates)

### Parallel opportunities

- T002, T003, T004 and T010 touch different files and can start together.
- T007 can run alongside T005/T006 once T003 and T004 are done.
- T013 (TEST_MATRIX) and T019–T021 (tooltip and docs) can run anytime after Foundational.
- T023 in Polish.

---

## Parallel example: Foundational + US3 docs

```text
# Start together:
Task: "T002 HandMode::chargeSkipped in include/Domain/State.h"
Task: "T003 intents in include/Shared/PumpResults.h"
Task: "T004 ForceChargeComplete in include/Adapters/Outbound/MagicEquip.h + src/Adapters/Outbound/MagicEquip.cpp"
Task: "T010 Document chargeSkipped / intent / adapter in .claude/CLAUDE.md"
# Then, sequential in src/Domain/MagicStatePump.cpp: T005 → T006 → T008 → T009
# Meanwhile: T007 (SpellSystemController.cpp), T013 (TEST_MATRIX), T019 (MENU.cpp), T020 (strings file), T021 (P1 note)
```

---

## Implementation strategy

### MVP first (User Story 1)

1. T001 setup, then T002–T011 foundational (including CLAUDE.md), then T012–T014.
2. **Gate (T015)**: confirm the magicka cost model in game on the DEBUG build. Apply T016 only if needed.
3. T017: validate the bound weapon flows. This alone fixes the reported loop.

### Incremental delivery

1. US1: the bound weapon loop is fixed, cost parity is verified, and CLAUDE.md is updated (T010). Commit; this
   satisfies merge gate 4 on its own.
2. US2: regression sweep (dual, mixed, Bound Bow, bound edge cases, patch OFF). Commit any TEST_MATRIX ticks.
3. US3: tooltip, strings file and design note. Commit; ship the strings file with the release package.
4. Polish: format, final builds (debug last), dual-cast regression, review.

---

## Notes

- Tasks marked **(in-game)** cannot be done by an agent; they need the user to run Skyrim with the DEBUG DLL and its
  log (Constitution VI).
- No INI keys, hook targets or Address Library IDs change.
- `include/PCH.h`'s unconditional `#define DEBUG` keeps `MAGIC_DEBUG_LOG` active in release builds as well. That
  concern is out of scope here and does not replace validating on the DEBUG build.
