---

description: "Task list for spec 007: fix slot casts failing with a weapon or shield in the left hand after drawing weapons"
---

# Tasks: Fix slot casts failing with a weapon or shield in the left hand after drawing weapons

**Input**: design documents in `specs/007-fix-drawn-weapon-cast/`: [plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md), [contracts/diagnostic-log.md](contracts/diagnostic-log.md),
[contracts/hand-switch-wait.md](contracts/hand-switch-wait.md), [quickstart.md](quickstart.md).

**Tests**: no automated harness exists (Constitution VI). Validation is in game, through the quickstart and the
TEST_MATRIX tasks marked **(in-game, user)**. They run on the **release** DLL, as approved by the user (spec
Clarifications).

**Organization**: the root cause is decided by a reproduction gate (T009) before any fix. Phase 3 holds one fix per
candidate cause, and each is marked **(only if H<n> is confirmed)**. Skip a task whose cause T009 did not confirm,
and record the skip in the implementation log.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1, US2, see spec.md)

## Conventions for every code task

- Identifiers, comments and log messages are in English. `.clang-format` applies (120 columns, 4-space indent).
- **Logging**: every new decision logs with `MAGIC_DEBUG_LOG` and a bracketed prefix (Constitution VI).
- **Layers**: `Domain/` only reads game state, through `Shared/` helpers. Game writes go through `Adapters/Outbound/`,
  called from `Application/` (Constitution I).
- **New `HandMode` fields**: reset by every value reset (`hm = {}`, `_left = {}`) and by `FinishHand`
  (Constitution II).
- **Out of scope**: no change in `src/Config`, `include/Config`, `src/Input`, `include/Input`, `src/UI`,
  `src/Persistence` or the INI (spec FR-010).
- **Build**: the scratchpad script
  `C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\build.ps1
  -Preset <debug|release> -LogFile <path>`. If it is gone, run `cmake --preset <p>` then `cmake --build build/<p>`.
  The post-build step copies the DLL to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`.

---

## Phase 1: Setup

**Purpose**: keep any reproduction the user already ran. Optional, and it does not block anything.

- [ ] T001 **(in-game, user, optional)** If the user already ran [quickstart.md](quickstart.md) §1 on the DLL
  installed before this spec, record it in this file's implementation log:
  - the setup, the slot and the patch pass of each run,
  - how many activations cast, out of how many,
  - what each failure looked like: nothing, a block, a swing, or the spell equipped and never cast,
  - whether the gear came back,
  - about 20 log lines around each failure.

  That DLL is the spec 006 build, whose code is the same as `main` at `f540210`. Otherwise skip this task: T008
  reproduces on the T007 build.

---

## Phase 2: Foundational (diagnostics and reproduction gate)

**Purpose**: ship the diagnostic log (FR-008) and the TEST_MATRIX cases (FR-009), then find the root cause (FR-007).

**⚠️ CRITICAL**: no Phase 3 task starts before T009 records its decision.

- [X] T002 [P] Create `include/Shared/HandDiagnostics.h` and `src/Shared/HandDiagnostics.cpp` in namespace
  `IntegratedMagic` (research R2, [contract §1](contracts/diagnostic-log.md)).
  - **Read-only**: include `PCH.h` and `Shared/InventoryUtil.h`, and never change game state.
  - **`const char* DescribeItemKind(const RE::TESForm* form)`** returns, in this order:
    - `"empty"` for null;
    - `"spell"` for `RE::SpellItem`;
    - for `RE::TESObjectWEAP`: `"staff"` (`IsStaff()`), `"bow"` (`IsBow()`), `"xbow"` (`IsCrossbow()`), `"2h"`
      (`IsTwoHandedSword() || IsTwoHandedAxe()`), else `"1h"`;
    - `"shield"` for `RE::TESObjectARMO` with `IsShield()`;
    - `"torch"` for `RE::TESObjectLIGH`;
    - else `"other"`.
  - **`std::optional<std::int32_t> GetHandAnimType(RE::PlayerCharacter* player, bool leftHand)`** reads the graph
    variable `iLeftHandType` / `iRightHandType` with `GetGraphVariableInt`. It returns `std::nullopt` when the
    player is null or the call returns false.
  - **`std::string DescribeHandsForLog(RE::PlayerCharacter* player)`** builds the exact format of contract §1:
    - `ws=<n> L=<kind>:<0x%08X> anim=<n|n/a> eq=<IsEquipping>/<IsUnequipping> ok=<bEquipOk> blk=<IsBlocking>
      atk=<IsAttacking> cast=<state>:<0x%08X> | R=<same>`;
    - `ws` is `std::to_underlying(player->AsActorState()->GetWeaponState())`;
    - the item is `player->GetEquippedObject(leftHand)`, printed as `empty` with no ID when null;
    - the bools come from `GetGraphVariableBool` and print `0`, `1` or `n/a`;
    - the caster is `GetMagicCaster(player, kLeftHand / kRightHand)`: `std::to_underlying(caster->state.get())` and
      `caster->currentSpell ? caster->currentSpell->GetFormID() : 0`, or `cast=n/a` with no caster;
    - it returns `"player=null"` for a null player.
  - Keep the `RE::BSFixedString` variable names as `static const` locals, as `MagicEquip.cpp` does with `kInstantAnim`.
- [X] T003 In `src/Domain/MagicStateLifecycle.cpp`, `MagicState::EnsureActiveWithSnapshot`, after the existing
  `snapHasSpells` log line (new sessions only, the early `already active` return stays first), add:
  `MAGIC_DEBUG_LOG("[State] Activation hands: {} slot={}", DescribeHandsForLog(pc), slot);` (contract §2), including
  `Shared/HandDiagnostics.h`. Depends on T002.
- [X] T004 [P] In `src/Application/SpellSystemController.cpp` (contract §2; depends on T002):
  - **`DispatchAttackPress`**: first line,
    `MAGIC_DEBUG_LOG("[SpellSystem] Press hands: hand={} {}", hand == IntegratedMagic::Hand::Left ? "L" : "R",
    IntegratedMagic::DescribeHandsForLog(RE::PlayerCharacter::GetSingleton()));`.
  - **`ExecuteRestoreSnapshotPlan`**: after the `if (!player || !mgr || !plan.valid) return;` guard, log
    `[SpellSystem] Restore hands (before): {}`. As the last statement of the function, log
    `[SpellSystem] Restore hands (after): {}`.
- [X] T005 [P] In `src/Adapters/Outbound/SyntheticInput.cpp`, `FireAttackDirect` ([contract §3](contracts/diagnostic-log.md)):
  - Before each of its three silent `return`s, log
    `[Action] synthetic attack dropped: hand=<L|R> value=<v> reason=<no handler|disabled|cannot process>`:
    - `no handler`: no `PlayerControls`, or no `attackBlockHandler`;
    - `disabled`: `!IsInputEventHandlingEnabled()`;
    - `cannot process`: `CanProcess(ev)` is false.
  - Log only a press (`value` 1.0 with `heldSecs` 0) or a release (`value` 0.0), not held repeats. Use one local
    `const bool logDrop = (value == 0.0f) || (heldSecs <= 0.0f);`.
  - Restructure the `CanProcess` line so the drop can be logged. Keep the behavior otherwise identical.
- [X] T006 [P] In `docs/TEST_MATRIX.md`, section 7, after 7.10, add the spec 007 cases (research R9), in Portuguese
  and in the file's style.
  - **Intro line**: "Armas sacadas com arma/escudo na mão esquerda (spec `specs/007-fix-drawn-weapon-cast/`). Rodar
    com Skip Equip Animation e Skip Equip Animation on Return desligados e depois ligados. Slots em Automatic salvo
    indicação:"
  - **7.11**: "Duas espadas diferentes, armas sacadas: slot de mão esquerda, de mão direita e dual, 10× seguidas cada
    → todas conjuram; as duas espadas voltam às mãos certas, ainda sacadas"
  - **7.12**: "Duas cópias da mesma espada, armas sacadas: mesmo teste do 7.11 → todas conjuram; as duas espadas
    voltam e nenhuma mão fica vazia"
  - **7.13**: "Espada + escudo, armas sacadas: mesmo teste do 7.11 → todas conjuram; espada e escudo voltam, ainda
    sacados"
  - **7.14**: "Sequência do relato (7.11 e 7.13): embainhar → conjurar → sacar → conjurar 5× → as 6 conjurações
    funcionam, não só a primeira depois de sacar"
  - **7.15**: "Logo depois de o equipamento voltar (espada + escudo): um ataque e uma defesa funcionam no primeiro
    aperto, sem embainhar"
  - **7.16**: "Hold e Press com auto-ataque, duas espadas e espada + escudo, armas sacadas, 5 usos cada → conjuram
    como com magia nas duas mãos"
  - **Must be done before** T019, which ticks these cases.
- [X] T007 Build debug, then release **last**, so MO2 holds the release DLL (research R10). Depends on T002–T005.
  - Expect 0 errors and 0 new compiler warnings in both presets (merge gate 1).
  - Check that the MO2 DLL hash matches `build\release\IntegratedMagic.dll`.
  - Delete any `IntegratedMagic.pdb` left in the MO2 plugin folder.
  - This build only adds log lines, so it behaves exactly like `main` (research R1).
- [X] T008 **(in-game, user)** Reproduction gate: run [quickstart.md](quickstart.md) §1 on the T007 build.
  - **Cover**: setups A, A' and B; left-hand, right-hand and dual slots; both patch passes; 10 activations each, and
    the sheathe → cast → draw → cast ×5 sequence.
  - **First**, check `skse64.log`: the `IntegratedMagic` line must load.
  - **Collect**, for each run: the counts, how each failure looked, whether the gear came back (both swords for A'),
    and the `IntegratedMagic.log`.
- [X] T009 Classify T008's log (and T001's, if any) against the signature table of [quickstart.md](quickstart.md) §1
  and research R3–R8. Record a **decision** section in this file's implementation log:
  - **H1–H5 and R8**: confirmed, ruled out or not seen, each with the log lines that show it.
  - **If H1 is confirmed, the D1 ready signal**, chosen in research R3's order:
    - the graph hand type, giving the `anim=` value a hand shows when it holds a spell in the `Press hands:` lines
      (expected 9);
    - else the `MLh_Equipped_Event` / `MRh_Equipped_Event` animation event;
    - else the caster's `currentSpell`.
    Also record the measured time from activation to that signal, with the patches OFF and ON.
  - **If H2 is confirmed**: whether D2 is option 1 (unqueued equip) or option 2 (D1's ready gate).
  - **Outcome**:
    - if no setup failed, the outcome is **already fixed**: skip T010–T018 and run T019 on the T007 build;
    - if the cause is still unclear, stop and report to the user with the excerpts before writing any fix.

**Checkpoint**: the root cause is known, or the bug is closed as already fixed.

---

## Phase 3: User Story 1 - Slots cast every time with weapons drawn (Priority: P1) 🎯 MVP

**Goal**: with two weapons, or a sword and a shield, drawn, every activation of a left-hand, right-hand or dual slot
casts. The gear comes back drawn, ready to attack or block.

**Independent Test**: [quickstart.md](quickstart.md) §2 (SC-001 to SC-005).

### D6: equip-animation pause of the retry bound (T009 decision, research R11)

T009 replaced D1 with D6, so T010–T012 below were rewritten to D6. The original D1 text is kept in
[contracts/hand-switch-wait.md](contracts/hand-switch-wait.md) and [data-model.md](data-model.md).

- [X] T010 [US1] In `include/Domain/State.h` (data-model, D6):
  - **`HandMode`**: after `startFailSecs`, add `float equipAnimGraceSecs{0.f};` and
    `bool equipAnimGraceActive{false};`, with one comment ("Drawn activation: time spent waiting for the game's
    equip/draw animation, which does not count toward startFailSecs ... Reset with startFailSecs").
  - **`MagicState`**: next to `kMaxCastStartSecs`, add `static constexpr float kMaxEquipAnimGraceSecs = 2.0f;`.
- [X] T011 [US1] In `include/Shared/HandDiagnostics.h` / `src/Shared/HandDiagnostics.cpp`, add
  `bool IsEquipAnimationPlaying(RE::PlayerCharacter* player)`: the graph variable `IsEquipping`, false when it is
  missing or the player is null. Read-only.
- [X] T012 [US1] In `src/Domain/MagicStatePump.cpp`, `PumpCastPhase` (data-model D6 rules; depends on T010, T011):
  - where a plugin-cast hand in `StartRequested` adds `dt` to `startFailSecs`, add it to `equipAnimGraceSecs` instead
    when `!_session.wasHandsDown && equipAnimGraceSecs < kMaxEquipAnimGraceSecs && IsEquipAnimationPlaying(...)`;
  - log `[FLOW] EquipAnimGrace: hand=<L|R> equip animation playing -> retry bound paused` when the pause starts, and
    `... retry bound resumes after <s>s (animation done|cap reached)` when it ends;
  - clear both fields wherever `startFailSecs` is cleared: `RequestAutoAttackStart`, the spurious-window reset in
    `PumpCastPhase`, and `FinishHand` in `src/Domain/MagicStateSlot.cpp`.

### D1: hand-switch wait (superseded by D6; original text, not built)

- **Original T010** [US1] In `include/Domain/State.h` (data-model, D1):
  - **`HandMode`**: after `startFailSecs`, add, with a one-line comment each:
    - `bool waitingHandSwitch{false};` "Drawn activation: wait for the game to show this hand as magic before the
      first press (spec 007, R3)",
    - `float handSwitchWaitSecs{0.f};`.
  - **`MagicState`**: next to `kMaxCastStartSecs`, add `static constexpr float kMaxHandSwitchWaitSecs = 0.5f;` with
    the comment "A missing ready signal ends the wait after this (spec 007, Constitution III)".
  - **Declarations**: `void MarkHandSwitchWaits();` and
    `void PumpHandSwitchWait(float dt, PumpAutomaticResult& result);`.
- **Original T011** [US1] In `src/Domain/MagicStateSlot.cpp` (contract [§1, §2](contracts/hand-switch-wait.md);
  depends on T010):
  - **Implement `MarkHandSwitchWaits()`**. For each `Hand h`, the hand waits when all of these hold:
    - `!_session.wasHandsDown`,
    - `IsPluginCast(ModeFor(h))`,
    - the hand's snapshot item `_restore.snapshot.leftObj.base` / `rightObj.base` is not null, or `h` is the left
      hand and the right snapshot item is a `TESObjectWEAP` that is two-handed, a bow or a crossbow (it occupies both
      hands).
  - **When a hand waits**: set `waitingHandSwitch = true` and `handSwitchWaitSecs = 0.f`, and log
    `[FLOW] HandSwitch: hand=<L|R> waiting (item=<DescribeItemKind(base)>)`.
  - **Dual cast**: if `_session.isDualCasting` and either hand waits, both wait (contract §4).
  - **`OnSlotPressed`**: call it once, right after the `EnterHand(Left, ...)` / `_left = {}` block, before
    `return action;`.
  - **`OnEquipComplete`**, in the `tryStart` lambda: if `hm.waitingHandSwitch`, log
    `[State] OnEquipComplete: hand=<..> skip dispatch - waiting hand switch` and do not call
    `RequestAutoAttackStart`.
  - **`FinishHand`**: also set `hm.waitingHandSwitch = false;` and `hm.handSwitchWaitSecs = 0.f;`.
- **Original T012** [US1] In `src/Domain/MagicStatePump.cpp`, and in `Shared/HandDiagnostics` for the ready read (contract §2–§4;
  depends on T011 and T009's ready signal):
  - **Ready signal**: add to `HandDiagnostics.h` / `.cpp` the read T009 chose. For the graph hand type, that is
    `bool IsHandShownAsMagic(RE::PlayerCharacter* player, bool leftHand)`: `GetHandAnimType` equals the recorded
    magic value. For the animation event, add a `HandMode` flag set from `NotifyAnimEvent` instead, and update T010.
  - **`PumpHandSwitchWait`**: for each waiting hand, add `dt`, then compute `ready` or
    `timedOut = handSwitchWaitSecs >= kMaxHandSwitchWaitSecs`.
    - **Single hand**: when ready or timed out, clear the flag, log `[FLOW] HandSwitch: hand=<..> ready after
      {:.2f}s` or `... timed out after {:.2f}s -> pressing anyway`, and if `RequestAutoAttackStart(h, false)` returns
      true, set `result.startLeftAttack` / `startRightAttack`.
    - **Dual cast**: end both hands only when both are ready, or either timed out, in the same call (Constitution IV).
  - **`PumpAutomatic`**: call `PumpHandSwitchWait(dt, result)` after the retry-bound loop and before
    `const auto leftPhase = PumpCastPhase(Left, dt);`.
  - **`PumpCastPhase`**: right after the `if (!_session.active || hm.finished)` block, add
    `if (hm.waitingHandSwitch) return result;`, so no `startFailSecs` and no redispatch run during the wait.
  - **`NotifyAttackEnabled`**, in `tryRedispatch`: `if (hm.waitingHandSwitch) return;` first.
  - **`OnCasterInterrupt`**: after the `if (hm.finished) return result;` check, add
    `if (hm.waitingHandSwitch) return result;` (no press was sent, so there is nothing to release).

### D2: restore while drawn (only if H2 is confirmed)

- [X] T013 [US1] **Skipped (T009: H2 not seen).** Option 1, unqueued equip (research R4; T009 chose it):
  - **`include/Shared/RestoreSnapshotPlan.h`**: add `bool equipNow{false};`.
  - **`src/Domain/MagicStateLifecycle.cpp`, `BuildRestoreSnapshotPlan`**: set
    `plan.equipNow = (player->AsActorState()->GetWeaponState() == RE::WEAPON_STATE::kDrawn);`, and add `equipNow` to
    its existing log line.
  - **`include/Adapters/Outbound/RestoreEquip.h` / `src/Adapters/Outbound/RestoreEquip.cpp`**: `RestoreOneHand` gains
    a last parameter `bool equipNow = false`. Pass `queueEquip = !equipNow` and `applyNow = equipNow` to both
    `EquipObject` and `UnequipObject`.
  - **`src/Application/SpellSystemController.cpp`, `ExecuteRestoreSnapshotPlan`**: pass `plan.equipNow` to the three
    `RestoreOneHand` calls.
  - **Option 2** (D1's ready gate) needs no task here: extend T011's wait condition to "a hand whose `anim=` type
    disagrees with its item", as T009 recorded it.

### D3 / D5: start-phase exemptions (only if H3 or H5 is confirmed)

- [X] T014 [US1] **Skipped (T009: H3 and H5 not seen).** In `include/Domain/State.h`, add the private helper
  `[[nodiscard]] static bool InStartPhase(const HandMode& hm) noexcept`, which returns
  `!hm.finished && (hm.waitingHandSwitch || hm.autoCastPhase == AutoCastPhase::StartRequested)`. Drop
  `hm.waitingHandSwitch` from it if D1 did not ship.
  - **D3 (H3)**: in `src/Domain/MagicStateLifecycle.cpp`, `ShouldForceInterrupt`, skip the `CasterSpellMismatch` check
    of a hand while `InStartPhase(ModeFor(hand))`.
  - **D5 (H5)**: in the same function, skip the sheathing rule while `!_session.wasHandsDown` and either hand is
    `InStartPhase`. Log `[State] ShouldForceInterrupt: weaponState={} ignored during drawn start` only when the
    weapon state changes, using a function-local `static int s_lastIgnoredWs = -1;` reset to -1 when the rule is not
    skipped.

### D4: own snapshot items are not foreign (only if H4 is confirmed)

- [X] T015 [US1] **Skipped (T009: H4 not seen).** Expose the snapshot items and use them in the equip sink (research R6, data-model D4):
  - **`include/Domain/State.h`**: add `RE::FormID SnapshotLeftObjID() const noexcept` and `SnapshotRightObjID()`,
    returning `_restore.snapshot.valid ? _restore.snapshot.leftObj.formID : 0` (and `rightObj`).
  - **`include/Application/SpellSystemController.h`**: `ActiveSlotContents` gains `RE::FormID snapLeftObj{0};` and
    `RE::FormID snapRightObj{0};`.
  - **`src/Application/SpellSystemController.cpp`, `GetActiveSlotContents`**: fill them.
  - **`src/Adapters/Inbound/EquipEventAdapter.cpp`**: in the shield branch and the weapon/misc branch, before any
    `ScheduleForceExitNoRestore()`, if `formID == contents.snapLeftObj || formID == contents.snapRightObj`, log
    `[EquipSink] own snapshot item {:#010x} re-equipped -> ignored` and `return kContinue;`.

### R8: two copies of the same weapon (only if T008 lost a weapon with setup A')

- [X] T016 [US1] **Skipped (T009: no weapon was lost; setup A' still to run in T019).** In
  `src/Adapters/Outbound/RestoreEquip.cpp`, `RestoreOneHand`, when `want.base` is set and
  `ResolveLiveExtra` finds nothing:
  - do not take `FindAnyInstanceExtraForBase` if that list is the one worn in the other hand
    (`GetWornExtraForHand(player->GetEquippedEntryData(!leftHand), !leftHand)`);
  - pass `nullptr` instead, so the game picks a copy that is not worn;
  - log `[Restore] hand=<L|R> base={:#010x} instance worn in the other hand -> nullptr extra`.

### Build and validation

- [X] T017 [US1] Build as in T007 (debug, then release last), with 0 errors and 0 new warnings, and the hash check.
  Depends on every Phase 3 task that shipped.
- [X] T018 [US1] Self-review of the shipped fixes against the contracts (for D6: against data-model D6 and
  research R11, as the D1 contract was not built):
  - [hand-switch-wait.md](contracts/hand-switch-wait.md) §1–§5: dual pairing, no UP for a hand never pressed, the
    0.5 s timeout, and the resets in `FinishHand` and in every `HandMode` reset;
  - data-model reset rules;
  - Constitution II and IV.

  Record the outcome in the implementation log.
- [X] T019 [US1] **(in-game, user)** Validate [quickstart.md](quickstart.md) §2 (SC-001 to SC-005) on the T017 build,
  or on the T007 build when T009's outcome is "already fixed". Depends on T006.
  - **Expected**:
    - 10 out of 10 casts per slot and setup, with both patch passes;
    - 6 out of 6 in the sequence;
    - the gear back, drawn, within 1 s;
    - the first attack and block work;
    - no `TIMEOUT -> ForceExit`.
  - **Afterwards**: tick TEST_MATRIX 7.11–7.16, and record any failure with its log excerpt.

**Checkpoint**: the reported bug is fixed (MVP).

---

## Phase 4: User Story 2 - The paths that work today keep working (Priority: P2)

**Goal**: the sheathed path, the setups that already work and the spec 005 attack/block rules behave as before.

**Independent Test**: [quickstart.md](quickstart.md) §3 (SC-006).

- [X] T020 [US2] Review the scope of the change. Depends on T017.
  - **Sheathed path**: `git diff main -- src/Domain/MagicStateLifecycle.cpp src/Application/SpellSystemController.cpp`
    must not change `ExitAllNow`'s sheathe branch, or the `needsSkipEquipVars` block of `DispatchSlotEvents`.
  - **Gating**: every shipped fix is gated on a drawn activation (`!wasHandsDown`), a weapon state of `kDrawn`, or
    the session's own snapshot items.
  - **No change is expected.** If one is needed, fix it and record it.
- [ ] T021 [US2] **(in-game, user)** Regression: [quickstart.md](quickstart.md) §3 on the build of T019.
  - **Cases**:
    - sheathed casts with setups A and B;
    - TEST_MATRIX section 1, including 1.5 and 1.6 (mandatory when T012 or T014 shipped, Constitution IV);
    - 7.1, 7.2, 7.6, 7.7, 7.10;
    - 8.9–8.19;
    - spells in both hands, and a spell in the left hand with a sword in the right, drawn.
  - **Afterwards**: tick what passes, and record any failure with its log excerpt.

---

## Phase 5: Polish & cross-cutting concerns

- [X] T022 [P] In `.claude/CLAUDE.md` (done 2026-11-21 for D6: the `HandMode` fields, the equip-animation pause
  under the retry bound, the hand diagnostics lines, the corrected log path, the Common tasks row):
  - **Debug logging section**: add the spec 007 diagnostic lines (`[State] Activation hands:`,
    `[SpellSystem] Press hands:` / `Restore hands (before|after):`, `[Action] synthetic attack dropped:`). Add one
    sentence on the `DescribeHandsForLog` format, pointing to `specs/007-fix-drawn-weapon-cast/contracts/diagnostic-log.md`.
  - **If D1 shipped**:
    - add `waitingHandSwitch` / `handSwitchWaitSecs` to the `HandMode` field list;
    - add a "Hand-switch wait" bullet to the Automatic-mode StartRequested step, with the log lines
      `[FLOW] HandSwitch: ...`;
    - state that both dual-cast hands leave it together.
  - **If D4 shipped**: one bullet on the equip sink ignoring the session's own snapshot items.
  - **Common tasks**: add the row "Slot fails with weapons drawn and a weapon or shield in a hand" →
    `src/Domain/MagicStateSlot.cpp` (`MarkHandSwitchWaits`), `src/Domain/MagicStatePump.cpp` (`PumpHandSwitchWait`),
    `src/Adapters/Outbound/RestoreEquip.cpp`, `src/Adapters/Inbound/EquipEventAdapter.cpp`. Keep only the files that
    changed, and say to check the `Activation hands:` / `Press hands:` lines first.
- [X] T023 Run clang-format in dry-run mode on the changed line ranges of each changed `.cpp` / `.h` only, and fix any
  violation. The binary is
  `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe --dry-run`, with
  `--lines` passed through `@()` in PowerShell.
- [X] T024 Final build as in T007, then check the scope:
  - `git diff --stat -- src/Config include/Config src/Input include/Input src/UI src/Persistence SKSE` must be
    empty;
  - `git diff --stat` must show only this spec's files, `docs/TEST_MATRIX.md`, `.claude/CLAUDE.md`, and the source
    files of the tasks that shipped.
- [X] T025 Pre-commit review:
  - set `**Status**` in `specs/007-fix-drawn-weapon-cast/spec.md`;
  - complete the implementation log: the build, T009's decision, the skipped tasks, the in-game results;
  - review `git diff`.

  Do not edit `.claude/tasks/*`, which the user maintains; mention in the report that bug 05 / BUG-07 can be marked
  done. Commit and push only when the user asks.

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1** (T001): optional, any time before T009.
- **Phase 2**:
  - T002 first;
  - then T003, T004 and T005, in parallel (different files); T006 at any time;
  - then T007 → T008 → T009.
  - **T009 gates everything after it.**
- **Phase 3 (US1)**:
  - **D1**: T010 → T011 → T012.
  - **D2**: T013, which can run beside D1 (different files).
  - **D3 / D5**: T014 after T010, because they share `State.h`, and after T013, because they share
    `MagicStateLifecycle.cpp`.
  - **D4**: T015 after T010 and T014 (`State.h`) and after T013 (`SpellSystemController.cpp`).
  - **R8**: T016 after T013 (`RestoreEquip.cpp`).
  - **Then**: T017 → T018 → T019.
- **Phase 4 (US2)**: T020 after T017, T021 after T019.
- **Phase 5**: T022 at any time after T009. Then T023 → T024 → T025, last.

### User story dependencies

- **US1 (P1)**: depends on the Phase 2 gate. It is the MVP.
- **US2 (P2)**: validates the same build. It has no code of its own unless T020 finds a gap.

### Same-file sequencing

| File | Order |
|---|---|
| `include/Domain/State.h` | T010 → T014 → T015 |
| `src/Domain/MagicStateLifecycle.cpp` | T003 → T013 → T014 |
| `src/Application/SpellSystemController.cpp` | T004 → T013 → T015 |
| `src/Adapters/Outbound/RestoreEquip.cpp` | T013 → T016 |
| `include/Shared/HandDiagnostics.h` / `.cpp` | T002 → T012 |
| `docs/TEST_MATRIX.md` | T006 → T019 / T021 (ticks) |

---

## Parallel Example

```text
# Phase 2, after T002 (different files):
T003  src/Domain/MagicStateLifecycle.cpp        (activation log)
T004  src/Application/SpellSystemController.cpp (press / restore logs)
T005  src/Adapters/Outbound/SyntheticInput.cpp  (dropped-input log)
T006  docs/TEST_MATRIX.md                       (7.11–7.16)

# Phase 3, when both H1 and H2 are confirmed:
T010–T012  D1 (State.h, MagicStateSlot.cpp, MagicStatePump.cpp)
T013       D2 (RestoreSnapshotPlan.h, RestoreEquip.*, MagicStateLifecycle.cpp, SpellSystemController.cpp)
```

---

## Implementation Strategy

### MVP first (US1)

1. Phase 2: the diagnostics, the build, the reproduction gate (T002–T009).
2. Phase 3: only the fixes T009 confirmed, then T017–T019.
3. **Stop and validate**: drawn slots cast every time with weapons or a shield in the hand.

### Incremental delivery

1. US2 (T020–T021): the regression guard, on the same build.
2. Polish (T022–T025).

### "Already fixed" path

T009 finds no failure. Ship T002–T007 (diagnostics and test cases), run T019 and T021 on the T007 build, then do
T022 (logging part only) to T025.

## Notes

- Commit or push only when the user asks.
- If T009 cannot tell the causes apart, do not guess: report the excerpts and ask (research R1).

## Implementation log

### 2026-11-20: diagnostics build (T002–T007)

- **T001**: not run. No log of an earlier reproduction was provided, and the bug report in `.claude/tasks/` has none.
  T008 reproduces on the T007 build.
- **T002**: `include/Shared/HandDiagnostics.h`, `src/Shared/HandDiagnostics.cpp`. CMake picks them up through the
  `GLOB_RECURSE CONFIGURE_DEPENDS` of `cmake/CollectFiles.cmake`, so no CMake change.
- **T003–T005**: the log points of contract §2 and §3. `FireAttackDirect` keeps its behavior. Its fourth return
  (`ButtonEvent::Create` failing) stays silent, as the contract lists three.
- **T006**: TEST_MATRIX 7.11–7.16.
- **T007**:
  - debug and release both built with exit 0 and no compiler warnings. The only `warning` line is vcpkg's
    builtin-baseline notice, which every build prints;
  - the MO2 DLL `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins\IntegratedMagic.dll` matches
    `build\release\IntegratedMagic.dll` (SHA-256 `EB144DB6…82A4`), and the copied PDB was deleted;
  - **note**: `D:\ModdingSkyrim\MO2\mods\Integrated Magic\SKSE\Plugins` (with a space) holds another, older
    `IntegratedMagic.dll` (19/11, SHA-256 `B0F3BCD8…8EFB`) next to the user's INI. It was not touched. The new
    `Activation hands:` lines in the log show that the T007 build is the one loaded.
- **Formatting**: a clang-format dry run of the new files and the changed ranges reports nothing (T023 repeats it at
  the end).

### 2026-11-21: T008 run 1 (partial)

- **Build**: the T007 DLL, confirmed by the `Activation hands:` lines (log of 2026-11-21 00:21–00:25).
- **Setup**: B only (sword `0x00012EB7` right, shield `0x00012EB6` left), drawn and sheathed.
- **Settings**: Skip Equip Animation and Skip Equip Animation on Return **ON** (`InstantEquipAnim = true` lines),
  Skip Channeling **ON**. Spells: Candlelight `0x00043324` (charge 0.5 s) in the right-hand slot (index 0) and the
  left-hand slot (index 1), Courage `0x0004DEE8` in the dual slot (index 2).
- **Activations**: 12, all cast (`*_SpellFire_Event` each), and each restore put the sword and the shield back in their
  hands, still drawn (`ws=3`):
  - right-hand slot: 1 sheathed, then 5 drawn, including sheathe → cast → draw → cast ×4;
  - left-hand slot: 1 drawn, then sheathe → cast → draw → cast ×3;
  - dual slot: 1 drawn, both hands began and fired together.
- **Attack after the restore**: four `weaponSwing` events, no `blockStart` (block not tried).
- **Signatures**:
  - **H1, mechanism present but recovered**: every drawn activation's first press lands while the hand still shows
    the old item (`anim=1` sword, `anim=10` shield). About 64 ms later the game sends `InterruptCast` + `weaponDraw`,
    the existing `pre-cast spurious → UP (DOWN next frame)` path re-presses with `anim=9`, and the cast starts. No
    failure followed it in this run.
  - **H2**: not seen. After each restore, the next activation shows `anim=10` / `anim=1` again and casts.
  - **H3, H4, H5**: not seen. No `ShouldForceInterrupt: TRUE`, no `[EquipSink] ... conflicts`, `ws=3` throughout.
  - **Others**: no `synthetic attack dropped`, `cast did not start`, or `TIMEOUT`.
- **Not covered yet**: setups A and A' (a weapon in the left hand, whose early press can start a swing instead of
  being interrupted), the patches-OFF pass (a longer hand switch, so the early press lands further ahead), 10× per
  slot, first person, the block after a restore.

**Next**: T008 run 2, setup A with the patches ON, then setups A and B with the patches OFF.

### 2026-11-21: T008 run 2, T009 decision, D6 (T010–T018)

- **Run 2** (log of 2026-11-21 00:29–00:33): a sword `0x00012EB7` in the right hand and an axe `0x00013790` (`anim=3`)
  in the left. Candlelight in the right-hand slot (index 1) and, from 00:31:39, in the left-hand slot (index 0).
  - **Patches ON**: right-hand slot 1 sheathed and 4 drawn, all cast. Left-hand slot 2 during the draw, cast.
  - **Patches OFF**:
    - right-hand slot 1 sheathed and 4 drawn, one of them during the draw: all cast, the drawn ones in about 1.4 s;
    - left-hand slot activated after the draw animation (`eq=0/0`): cast (00:32:02);
    - left-hand slot activated **during** the draw animation (`eq=1/0`, 00:31:46, 00:31:54, 00:32:14, 00:32:24):
      **4 of 4 failed** with `cast did not start in 1.0Xs -> ForceExit`. The user saw the draw restart and no spell.
  - **Two `ShouldForceInterrupt: TRUE - player sheathing/sheathed` exits** (00:33:43, 00:33:48): a `BeginWeaponSheathe`
    the plugin did not request, 0.13–0.23 s after the cast began. The gear came back. To confirm with the user that
    it was their sheathe key.
- **T009 decision** (research R11):
  - **H1**: present on every drawn activation (the first press shows the old `anim`), but the existing spurious
    restart recovers it. It is not the failure.
  - **H2, H3, H4, H5**: not seen.
  - **R8**: not tested (setup A' not run).
  - **The failure**: with the patches OFF, a spell equipped in the left hand during the draw restarts the draw
    animation (about 1.42 s to `WeapEquip_Out`). The left cast begins only after it ends, and spec 005's 1 s bound
    ends the session first.
  - **Fix**: D6, which keeps the equip animation out of the bound, up to 2 s. D1 is superseded: the hand shows
    `anim=9` within a frame, so D1's ready signal would not have helped. D2–D5 and R8 are skipped (T013–T016).
- **Spec updated**: Background item 7, the 2026-11-21 Clarifications session, the "during the draw animation" edge
  case, and FR-006 (animation time does not count toward the 1 s, up to 2 s).
- **T010–T012**: `HandMode::equipAnimGraceSecs` / `equipAnimGraceActive`, `kMaxEquipAnimGraceSecs`,
  `IsEquipAnimationPlaying`, and the pause in `PumpCastPhase`, with the resets in `RequestAutoAttackStart`, the
  spurious-window reset and `FinishHand`.
- **T017**: debug and release built with exit 0 and no compiler warnings. The MO2 DLL matches `build\release`
  (SHA-256 `BA08E1B1…DE4B`), and the copied PDB was deleted. A clang-format dry run of the changed ranges is clean,
  after one line was joined.
- **T018 self-review**:
  - **Sheathed path**: unchanged, as the pause needs `!wasHandsDown` (FR-004).
  - **Dual cast**: `IsEquipping` is one variable for the player, so both hands pause and resume in the same frame,
    and no press changes (Constitution IV).
  - **Bounded**: at most 2.0 s of pause, plus 1.0 s (Constitution III).
  - **Resets**: wherever `startFailSecs` resets, and in every `HandMode` value reset (Constitution II).
  - **TEST_MATRIX 8.18** (jump, drawn): it can now take up to the equip animation time longer before `ForceExit`,
    about 1.5 s with the patches OFF instead of 1 s. To check in T021.

**Next**: T019, in game, by the user, on the T017 build.

### 2026-11-21: T019 validation, closing tasks (T020–T025)

- **T019** (log of 2026-11-21 01:04–01:05, T017 build): a sword in the right hand and an axe in the left.
  - **Activations**: 11, all of the left-hand slot during the draw animation: 1 with the patches ON, then 10 OFF
    (Candlelight ×9, Flames ×1).
  - **Result**: all 11 cast (`MLh_SpellFire_Event`). Each logged `EquipAnimGrace: hand=L ... retry bound paused`.
    The cast began on the frame after `WeapEquip_Out`, 1.4–1.5 s after activation. No `cast did not start`, no
    `TIMEOUT`, no forced exit.
  - **The `... resumes after` line**: it never appeared, because the cast begins as the animation ends.
    `quickstart.md` §2 now says so.
  - **The user's verdict**: "tudo está funcionando".
  - **TEST_MATRIX**: 7.17 ticked. 7.11–7.16 stay open, as their full counts (10× per slot and setup, both patch
    passes, setup A') were not run as written. The runs of the log cover them in part (run 1, run 2, T019).
- **T020**: `git diff main` of `MagicStateLifecycle.cpp` and `SpellSystemController.cpp` shows only the include and
  the log lines. `ExitAllNow`'s sheathe branch and `DispatchSlotEvents` are unchanged. D6 is gated on `!wasHandsDown`.
- **T021**: not run in full. Still open:
  - dual cast during the draw (Constitution IV),
  - TEST_MATRIX section 1,
  - 8.18 (jump, drawn),
  - the right hand after the fix.
  The user reported no problem in what they tried.
- **T022–T024**:
  - `.claude/CLAUDE.md` updated (T022);
  - clang-format clean on every changed range (T023);
  - nothing changed in `src/Config`, `include/Config`, `src/Input`, `include/Input`, `src/UI`, `src/Persistence` or
    `SKSE`;
  - the source has not changed since the T017 build (T024).
- **T025**:
  - spec `Status` set to Implemented;
  - bug 05 / BUG-07 in `.claude/tasks/` can be marked as resolved by the user, who maintains those files;
  - the two `sheathing/sheathed` exits of run 2 were not confirmed with the user. The plugin did not request that
    sheathe, and the gear came back.
