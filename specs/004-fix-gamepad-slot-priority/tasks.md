---

description: "Task list for fixing gamepad slots 5+ losing input priority to the game's default actions"
---

# Tasks: Fix gamepad slots 5+ losing input priority to the game's default actions

**Input**: Design documents from `specs/004-fix-gamepad-slot-priority/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md),
[contracts/input-ownership.md](contracts/input-ownership.md), [quickstart.md](quickstart.md)

**Tests**: there is no automated test harness (Constitution VI). Validation is the in-game TEST_MATRIX cases from
[quickstart.md](quickstart.md). Tasks marked **(in-game)** are done by the user.

**Organization**:
- **US1**: carries all the code and the **press-order test**.
  - The natural "second layer" layout (slots 5–8 = `face + RT + LB`) needs all three fixes M1, M2 and M3 (research
    R1).
  - The user confirmed in game that the press order is the main trigger (M2; spec Background item 7).
- **US2 and US3**: they validate the same code for buttons used on their own and for the keyboard.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1, US2 or US3 from spec.md
- Paths are relative to the repository root.

## Build procedure (referenced by the build tasks)

Run it in a Visual Studio 2026 developer shell. `Launch-VsDevShell.ps1` comes from
`D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools`, run with `-Arch amd64 -HostArch amd64`.

1. `cmake --preset debug` and `cmake --build build/debug`
2. `cmake --preset release` and `cmake --build build/release`. Release goes **last**, so MO2 keeps the release DLL.
3. Delete `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins\IntegratedMagic.pdb`.
4. Confirm that the MO2 DLL hash equals `build\release\IntegratedMagic.dll`.
5. **Expected**: 0 errors and no new compiler warnings. The `vcpkg builtin-baseline` warning already exists.

---

## Phase 1: Setup

- [X] T001 Ask the user to approve the Principle VI deviation for this spec: release-DLL validation that relies on the
  existing `#define DEBUG` (plan Complexity Tracking, rows 2 and 3).
  - Record the answer and date in `specs/004-fix-gamepad-slot-priority/plan.md`, replacing "**Pending the user's
    approval for this spec.**".
  - In-game tasks T015–T017 and T020 must not start before this is recorded.
  - **Done (2026-11-20)**: approved by the user. It is recorded in plan.md (Constitution Check VI and Complexity
    Tracking) and in the spec Clarifications.
- [X] T002 **(in-game, optional baseline)** On the **current** build, set up the 8-slot gamepad layout from
  [quickstart.md](quickstart.md) Prerequisites and run quickstart §7. Note whether the character jumps or attacks.
  - **Done by the user (2026-11-20)** on default slot 2 `R2 + Triangle`:
    - Triangle slightly before R2 → **jump**, and the spell fails,
    - R2 first → casts, with R2's attack masked by the spell equip.
  - Recorded in spec Background item 7 and research R1 (M2).
- [X] T003 [P] Add TEST_MATRIX cases 6.15–6.23 to `docs/TEST_MATRIX.md` after 6.14, in Portuguese, under a sub-heading
  `Gamepad × slots compartilhando botões (spec specs/004-fix-gamepad-slot-priority/):`
  - **6.15** 8 slots, slots 5–8 = `face + RT + LB`. Segurar `RT + LB` e apertar o botão de face por último → o slot
    5–8 conjura, sem pulo/ataque/menu do jogo
  - **6.16** Ordem (slots 1–8, acorde rápido): botão de face um pouco antes do `RT` (ex.: Triângulo/`Y` antes do
    R2/`RT`) e também `RT` primeiro → conjura nas duas ordens, sem pulo/ação do botão de face e sem ataque da mão
    direita
  - **6.17** Botões sozinhos (`RT`, `Y`, `A`, `B`) sem completar combo → a ação normal do jogo acontece, sem atraso
    perceptível
  - **6.18** Sobreposição: segurar `RT + LB`, `Y` por último → o slot 6 dispara e o slot 2 não; o log mostra
    `SUPPRESSED by slot=5`
  - **6.19** Combos idênticos (slot 8 = `B + RT` como o slot 3) → aviso `HotkeyCache: slots 2 and 7 have the same gp
    combo` ao carregar; o slot 3 dispara
  - **6.20** `requireExclusiveHotkey` ON + sobreposição → o slot 6 dispara e o slot 2 é recusado com `extra=gp:8`
  - **6.21** Teclado: slot 1 `Shift + 1`, slot 5 `Shift + 5` → cada combo ativa o próprio slot, e `Shift` não chega ao
    jogo
  - **6.22** Modo Hold: segurar o combo do slot 6 (`Y + RT + LB`) por ~2 s → nenhuma ação do jogo durante o hold; ao
    soltar, nenhum botão fica preso
  - **6.23** Slot 2 em Press sem auto-ataque ativo: apertar `RT` sozinho → conjura manualmente; o log mostra
    `handed to game` para `RT`

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: new state, pure helpers, the effective-code mapping and the resets that US1 builds on.

- [X] T004 [P] Add the new state from [data-model.md](data-model.md).
  - In `include/Shared/InputConstants.h`, add after `kFilterReplayDelaySec`:
    - `inline constexpr float kMaxContestedHoldSec = 0.5f;` with the comment `// Max hold for a button while a slot
      containing it is still pending (spec 004, R3b).`
    - `inline constexpr float kMaxChordHoldSec = 0.15f;` with the comment `// Max hold while a chord is in progress:
      2+ buttons of a combo held (spec 004, R3c).`
  - In `include/Input/ExclusiveStore.h`:
    - Add `enum class PressFate : std::uint8_t { None = 0, HeldBack, Consumed, Passed };` next to `PendingSrc`, with
      the comment `// Fate of a button's current press; its held repeats and release follow it (spec 004, R11).`
    - Add these to `struct ExclusiveStore`, each with a one-line comment:
      - `std::array<bool, kInputMaxSlots> suppressedUntilRelease{};`: lost an overlap resolution; cannot activate
        until its combo is released (R4).
      - `std::array<float, kInputMaxSlots> heldBackSecs{};`: time since the current filter window opened (R3b/R3c
        caps).
      - `std::array<bool, kInputMaxSlots> contestedLogged{};`: the "window extended" line was logged for this window.
      - `std::array<PressFate, kMaxCode> kbFate{};` and `std::array<PressFate, kMaxCode> gpFate{};`: press fate per
        keyboard/mouse code and per gamepad index (R11).
      - `std::array<int, kMaxCode> kbOwner{};` and `std::array<int, kMaxCode> gpOwner{};`: the owner slot of a
        `HeldBack` press. Reset them to `-1` in T007, since `{}` gives `0`.
- [X] T005 [P] In `include/Input/HotkeyMatcher.h`, add pure helpers. No `RE::` types; add `#include <format>` and
  `#include <string>`.
  - `[[nodiscard]] inline int ComboKeyCount(const std::array<int, 3>& c)`: the count of entries `!= -1`.
  - `template <class DownArr> [[nodiscard]] int ComboHeldCount(const std::array<int, 3>& c, const DownArr& down)`:
    the count of bound entries in range whose `down[code]` is true.
  - `[[nodiscard]] inline bool ComboContainsAll(const std::array<int, 3>& sub, const std::array<int, 3>& sup)`:
    every bound key of `sub` is in `sup`.
  - `[[nodiscard]] inline bool ComboStrictSubset(a, b)`: `ComboKeyCount(a) > 0 && ComboKeyCount(a) <
    ComboKeyCount(b) && ComboContainsAll(a, b)`.
  - `[[nodiscard]] inline bool ComboSameKeys(a, b)`: `ComboKeyCount(a) > 0 && ComboKeyCount(a) == ComboKeyCount(b)
    && ComboContainsAll(a, b)`.
  - **Move `DescribeHeldCode`**: move `DescribeHeldCode(int code, bool gamepad)` from the anonymous namespace of
    `src/Input/ExclusiveTracker.cpp` into this header as `[[nodiscard]] inline std::string DescribeHeldCode(...)`,
    with the same body. Delete the old copy in T007, which edits the same `.cpp`.
- [X] T006 [P] Add the effective-code mapping (data-model "Effective code").
  - Declare `[[nodiscard]] int EffectiveComboCode(RE::INPUT_DEVICE dev, std::uint32_t rawIdCode);` in
    `include/Input/InputFilter.h`.
  - Define it in `src/Input/InputFilter.cpp`, in `namespace Input::detail` **outside** the anonymous namespace, after
    `GamepadIdToIndex`. Gamepad returns `GamepadIdToIndex(static_cast<int>(rawIdCode))`, mouse returns
    `kMouseButtonBase + static_cast<int>(rawIdCode)`, and anything else returns `static_cast<int>(rawIdCode)`.
- [X] T007 In `src/Input/ExclusiveTracker.cpp`, delete the local `DescribeHeldCode` (now in `HotkeyMatcher.h`), add
  the fate helper, and reset the new fields. Depends on T004, T005, T006 and `#include "Input/InputFilter.h"`.
  - **Fate helpers** in the anonymous namespace (research R11):
    - `PressFate& FateOf(ExclusiveStore& excl, RE::INPUT_DEVICE dev, int code)`: returns `gpFate[code]` for gamepad,
      `kbFate[code]` otherwise.
    - `int& OwnerOf(...)`: the same for the owner arrays.
  - **Resolve helper** in the anonymous namespace: `void ResolveFates(const std::vector<RetainedEvent>& events,
    PressFate fate, ExclusiveStore& excl)`.
    - For each distinct effective code in `events` (`EffectiveComboCode(ev.dev, ev.rawIdCode)`), set the fate to
      `fate` and the owner to `-1`.
    - If the **last** event of that code in `events` is a release (`ev.value <= 0.f`), set the fate to `None`
      instead.
  - **Resets**:
    - `ResetExclusiveState` and `ClearEdgeStateOnly`: per slot, set `excl.suppressedUntilRelease[s] = false;`,
      `excl.heldBackSecs[s] = 0.f;` and `excl.contestedLogged[s] = false;`.
    - Once per call, fill `kbFate`/`gpFate` with `PressFate::None` and `kbOwner`/`gpOwner` with `-1`.
  - **`DiscardExclusivePending`**: before `retained[s].clear()`, call `ResolveFates(retained[s],
    PressFate::Consumed, excl)`. The game never saw those presses, so their held repeats and releases must not reach
    it alone. Next to `excl.filterWindowActive[s] = false;`, also reset `heldBackSecs[s]` and `contestedLogged[s]`.

**Checkpoint**: builds with no behavior change.

---

## Phase 3: User Story 1 - Gamepad combos cast regardless of slot number or press order (Priority: P1) 🎯 MVP

**Goal**:
- shared buttons are evaluated against every candidate slot,
- they are consumed by whichever slot activates,
- they are held while a candidate is pending or a chord is in progress, so a face button pressed slightly before the
  modifier no longer leaks, including in 3-button chords,
- each press has a fate that its held repeats and release follow,
- overlapping combos resolve to the longest.

FR-001, FR-002, FR-004, FR-007, FR-009, FR-010.

**Independent Test**: [quickstart.md](quickstart.md) §1, §2 (press order) and §4.

- [X] T008 [US1] Rewrite `ShouldFilterAndSave` in `src/Input/InputFilter.cpp` (research R2, R11, contract §1).
  Depends on T004–T007. Keep the signature.
  - **Candidates**:
    - Code: `code = (dev == kMouse) ? kMouseButtonBase + convertedCode : convertedCode`. `isGp = (dev == kGamepad)`.
    - Collect every `slot < slots.ActiveSlots()` where `ComboContains(isGp ? hk.gp : hk.kb, code)`. Keyboard and
      mouse use `kb`. Store them in a local `std::array<int, kInputMaxSlots>` plus a count.
    - With no candidate, return `false`.
  - **Event kind**:
    - `isRelease = value <= 0.f`,
    - `isFresh = value > 0.f && heldSecs <= 0.f`,
    - otherwise a held repeat.
  - **Phase 0, fate gate** for held repeats and releases (R11). Use `fate = isGp ? excl.gpFate[code] :
    excl.kbFate[code]`.
    - **Release**: for any candidate with `excl.deactivatedThisPress[c]`, clear the flag. This keeps the old phase-3
      bookkeeping.
    - `Consumed` → return `true`. On a release, set the fate to `None`.
    - `Passed` or `None` → return `false`. On a release, set the fate to `None`.
    - `HeldBack` → push the event into `retained[owner]` (`owner = isGp ? gpOwner[code] : kbOwner[code]`) and return
      `true`. The fate stays until it is resolved.
  - **Fresh press only**, from here on:
    - **Phase 1**: if any candidate has `slots.slotDown[c]` or `ComboDown(combo_c, isGp ? keys.gpDown :
      keys.kbDown)`, set the fate to `Consumed` and return `true`. **Do not use `slotWasAccepted`** (finding H3).
    - **Phase 2**: for each candidate, if `ReplayMatchesEvent(c, dev, rawIdCode, userEvent, value, replay)`, call
      `ResetReplayState(c, replay)`, set the fate to `Passed`, and return `false`.
    - **Phase 4, owner**: pick the owner with the highest `ComboHeldCount(combo, down)`. Ties go to the highest
      `ComboKeyCount(combo)`, then the lowest slot.
      - If `!excl.filterWindowActive[owner]`, set it `true`, `filterWindowTimer[owner] = kFilterReplayDelaySec`,
        `heldBackSecs[owner] = 0.f` and `contestedLogged[owner] = false`.
      - Push the event into `retained[owner]`, and set the fate to `HeldBack` and the owner code to `owner`.
      - Log `MAGIC_DEBUG_LOG("[Input] HoldBack: {} held for slot={} (candidates={})", DescribeHeldCode(code, isGp), owner, count);`
      - Return `true`.
- [X] T009 [US1] In `src/Input/ExclusiveTracker.cpp`, add the consume helper in the anonymous namespace (research
  R3a, contract §2). Depends on T006, T007 and `#include "Input/InputFilter.h"`.
  - Signature: `void ConsumeHeldBackForCombo(int winner, const SlotHotkeys& hk, bool kb, bool gp, int n,
    ExclusiveStore& excl, RetainedArr& retained, DeferredVec& deferred)`.
  - **Matching event**: an event `ev` belongs to the winner when `evGp = (ev.dev == kGamepad)`, `(evGp ? gp : kb)` is
    true, and `ComboContains(evGp ? hk.gp : hk.kb, EffectiveComboCode(ev.dev, ev.rawIdCode))`.
  - **Held-back events**: for every `t < n`, first collect the matching events of `retained[t]` and call
    `ResolveFates(matching, PressFate::Consumed, excl)` (T007). Then `std::erase_if(retained[t], ...)` them, logging
    each one: `MAGIC_DEBUG_LOG("[Input] HoldBack: {} consumed by slot={}", DescribeHeldCode(code, evGp), winner)`.
    - If `retained[t]` becomes empty while `excl.filterWindowActive[t]`, close the window. Set it `false`, with
      `filterWindowTimer`, `heldBackSecs` and `contestedLogged` reset.
  - **Queued replays**: also `std::erase_if(deferred, ...)` the `DeferredReplayEvent`s whose `.ev` matches, with the
    same log line.
- [X] T010 [US1] Restructure `RecomputeSlotEdges` in `src/Input/ExclusiveTracker.cpp` into three passes (research R4,
  data-model "Slot activation"). Depends on T005, T007 and T009.
  - **Pass 1**: per slot, compute `kbNow`, `gpNow`, `rawNow` and `prevAcc` as today. Store them with `accNow` in
    local `std::array<bool, kInputMaxSlots>` arrays.
    - If `excl.suppressedUntilRelease[s] && rawNow`: set `excl.prevRawKbDown[s] = kbNow`, `excl.prevRawGpDown[s] =
      gpNow`, call `DiscardExclusivePending(s, ...)`, set `accNow = false` and skip the acceptance logic.
    - If `excl.suppressedUntilRelease[s] && !rawNow`: clear the flag and continue normally.
    - Otherwise compute `accNow` exactly as today (the `ComputeAcceptedExclusive` branch or the single-key branch).
  - **Pass 2, overlap**: for each slot `a` rising (`acc[a] && !prev[a]`), check every `b != a`. `a` loses when, on a
    device where `a`'s combo is down (`kb[a]` or `gp[a]`) and `b`'s combo is also down on it:
    - `ComboStrictSubset(combo_a, combo_b)` holds, and `b` is rising or `excl.pendingSrc[b] != PendingSrc::None`, or
    - `ComboSameKeys(combo_a, combo_b)` holds, `b` is rising and `b < a`.

    A loser gets `acc[a] = false`, `excl.suppressedUntilRelease[a] = true` and `DiscardExclusivePending(a, ...)`. Log
    `MAGIC_DEBUG_LOG("[Input] RecomputeSlotEdges: slot={} SUPPRESSED by slot={} ({})", a, b, same ? "identical combo" : "longer combo")`.
    Stop at the first `b` that beats `a`.
  - **Pass 3**: apply edges exactly as the existing code does (EDGE log, `slotDown` store, pressed/released masks,
    `slotWasAccepted` rules), using the arrays. For each slot whose edge is `PRESSED`, call
    `ConsumeHeldBackForCombo(slot, cache.slots[s], kb[s], gp[s], n, excl, retained, deferred)`.
- [X] T011 [US1] Contested hold and chord in progress in `TickFilterWindows` (research R3b, R3c, contract §2).
  Depends on T004–T007.
  - **Signature**: in `src/Input/ExclusiveTracker.cpp` and `include/Input/ExclusiveTracker.h`, add `const
    HotkeyCacheStore& cache, const KeyStateStore& keys` after `SlotEdgeStore& slots`. Update the single call in
    `RecomputeSlotEdges`.
  - **Per active window**: `excl.heldBackSecs[s] += dt;` then `filterWindowTimer[s] -= dt;`. Keep the window if the
    timer is still `> 0.f`.
  - **Contested check (R3b)**: on expiry, while `heldBackSecs[s] < kMaxContestedHoldSec`, look for a slot `t < n`
    with `excl.pendingSrc[t] != PendingSrc::None` whose combo (device of the event) contains
    `EffectiveComboCode(ev.dev, ev.rawIdCode)` for some `ev` in `retained[s]`. The reason is `pending`.
  - **Chord check (R3c)**: otherwise, while `heldBackSecs[s] < kMaxChordHoldSec`, look for a slot `t < n` whose combo
    (device of the event) contains such a code and has `ComboHeldCount(combo, down) >= 2 && ComboHeldCount(combo,
    down) < ComboKeyCount(combo)`, where `down` is `keys.kbDown` or `keys.gpDown`. The reason is `chord`.
  - **If either check finds a slot**: keep the window active with the timer at `0.f`, so it is rechecked every frame.
    If `!contestedLogged[s]`, log `MAGIC_DEBUG_LOG("[Input] HoldBack: slot={} window extended (slot={} {})", s, t, reason)`
    and set the flag.
  - **Release**: otherwise call `ResolveFates(retained[s], PressFate::Passed, excl)`. Then, for each `ev`, log
    `MAGIC_DEBUG_LOG("[Input] HoldBack: {} handed to game (slot={})", DescribeHeldCode(code, evGp), s)` and
    `QueueDeferredReplayEvent(s, ev, deferred)`. Clear `retained[s]` and close the window (reset `heldBackSecs` and
    `contestedLogged`).
- [X] T012 [US1] In `src/Input/ExclusiveTracker.cpp` `ClearExclusivePending`, change the non-`Success` branch so the
  contested rule decides the release. Depends on T011.
  - **Current**: it closes the window and queues the retained events for replay.
  - **New**: if `excl.filterWindowActive[s]`, only set `excl.filterWindowTimer[s] = 0.f`, so `TickFilterWindows`
    decides on the next frame. Do **not** clear `retained[s]` in that case.
  - Keep `ClearDeferredReplayEventsForSlot`, `ResetReplayState` and the sim-window reset.
  - Move the trailing `retained[s].clear();` into the `Success` branch, preceded by `ResolveFates(retained[s],
    PressFate::Consumed, excl)`. In the non-`Success` branch, clear only when no window is active, resolving to
    `Passed`, because nothing is replayed there.
  - Update the existing `queue replay` log to say "deferred to window".
  - This adds about one frame (~16 ms) to the release of a cancelled press only (research R3d, accepted).
- [X] T013 [P] [US1] In `src/Input/HotkeyMatcher.cpp` `LoadHotkeyCache_FromConfig`, warn about identical combos (research
  R5). Depends on T005.
  - After the per-slot loop, for `i < j < m`:
    - if `ComboSameKeys(cache.slots[i].kb, cache.slots[j].kb)`, log
      `MAGIC_DEBUG_LOG("[Input] HotkeyCache: slots {} and {} have the same kb combo; slot {} fires", i, j, i);`,
    - do the same for `gp`.
- [X] T014 [US1] Build per the **Build procedure**. Depends on T004–T013.
- [X] T015 [US1] **(in-game)** Validate [quickstart.md](quickstart.md) §1, §2 and §4 (SC-001, SC-002, SC-006,
  SC-007, SC-008 Hold). Depends on T001 and T014.
  - **Expected**:
    - slots 5–8 cast 20 out of 20 times, with no game action,
    - **press order (§2)**: slots 1–8 cast in both orders, for every press the log counts as a chord. With
      Triangle/`Y` slightly before R2/`RT` there is no jump, and with `RT` first no right-hand attack reaches the
      game,
    - the log shows `window extended (... pending|chord|complete)` → `consumed by slot=...`, and no `handed to game` line
      during activations,
    - slow chords (§2 step 4, or any press the log marks as slow) behave as the documented limit and are reported
      separately,
    - the Hold-mode step (§2 step 5) shows no game action during the hold and nothing stuck afterwards,
    - the overlap and identical-combo cases behave as described.
  - **Afterwards**: tick 6.15, 6.16, 6.18, 6.19, 6.20 and 6.22 in `docs/TEST_MATRIX.md`.

**Checkpoint**: the reported bug is fixed. MVP.

---

## Phase 4: User Story 2 - Buttons used on their own keep their normal game action (Priority: P2)

**Goal**: single buttons, abandoned combos and manual casting during a session still reach the game, with no added
delay (FR-003, FR-010).

**Independent Test**: [quickstart.md](quickstart.md) §3.

No code task: T008–T012 change the shared path. This phase validates that it does not regress normal gameplay.

- [X] T016 [US2] **(in-game)** Validate [quickstart.md](quickstart.md) §3 (SC-003, SC-008 Press), on the T014 build or
  later.
  - **Expected**:
    - single buttons perform their normal action 10 out of 10 times, with no perceivable extra delay,
    - an abandoned combo's button acts as a normal press,
    - the log shows `held for slot=...` → `handed to game`,
    - **casting manually during a session (§3 step 4)**: with slot 2 active in Press mode without auto-attack,
      pressing `RT` alone casts 10 out of 10 times, and `RT` is never left stuck.
  - **Afterwards**: tick 6.17 and 6.23.

---

## Phase 5: User Story 3 - Keyboard and mouse combos follow the same rule (Priority: P3)

**Goal**: shared keyboard keys behave the same (FR-005).

**Independent Test**: [quickstart.md](quickstart.md) §5.

No code task: the changed functions are device-agnostic (research R7).

- [X] T017 [US3] **(in-game)** Validate [quickstart.md](quickstart.md) §5 (SC-004).
  - **Expected**:
    - `Shift + 1` and `Shift + 5` each activate their own slot 10 out of 10 times,
    - `Shift` causes no game action,
    - the log shows `consumed by slot=...` for `kb:0x2a`.
  - **Afterwards**: tick 6.21, then restore the original bindings.

---

## Phase 6: Polish & cross-cutting concerns

- [X] T018 [P] Update `.claude/CLAUDE.md`, section `## Input system`.
  - **`ReplaySystem` paragraph**:
    - Held-back buttons are attributed to an owner candidate.
    - They are **consumed** when any slot containing them activates.
    - They are held while a candidate is pending (`kMaxContestedHoldSec` = 0.5 s), or while a chord is in progress:
      2+ of a combo's buttons held (`kMaxChordHoldSec` = 0.15 s).
    - Otherwise they are handed to the game.
    - **Press fate** (`kbFate` / `gpFate`): a button's held repeats and release follow its own press, consumed or
      passed. An active session does not block new presses (spec 004 FR-010).
  - **New paragraph "Overlapping combos"**: the longest combo completing at the same moment wins. Identical combos go
    to the lower slot. The loser is `suppressedUntilRelease`. The load warning is `HotkeyCache: slots ... same ...
    combo`.
  - **Log prefixes**: the `HoldBack:` lines and `SUPPRESSED`.
  - **Common tasks row "Hotkey not detected / double-firing"**: add "for shared buttons, check `HoldBack:` lines" and
    point to `specs/004-fix-gamepad-slot-priority/`.
- [X] T019 Run `clang-format --style=file --dry-run` (x64 binary at
  `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`) on every touched
  source file. Fix only violations in lines this feature changed, with `--lines`. Rebuild if anything changed.
- [X] T020 **(in-game)** Regression: [quickstart.md](quickstart.md) §6. TEST_MATRIX 6.1–6.14 and dual cast 1.1, 1.3,
  1.5 (SC-005), on the final build. Tick what passes, and record any failure with its log excerpt.
- [X] T021 Pre-commit review.
  - Set `**Status**` in `specs/004-fix-gamepad-slot-priority/spec.md`.
  - Run `git diff --stat -- src/Domain src/Application include/Domain include/Application src/Config include/Config`
    and confirm it is empty (scope, FR-006).
  - Review `git diff` of every file touched by this feature.
  - Remind the user that the staged distribution-package changes on this branch (`SKSE/Plugins/`, `CMakeLists.txt`,
    `src/UI/MENU.cpp`, the deleted task doc) are separate and should be committed apart from this fix.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup**: T001 blocks every in-game task (T015–T017, T020). T002 is optional and already done by the user. T003
  is needed before the first
  tick (T015).
- **Foundational**: T004, T005 and T006 can run in parallel (different files). T007 depends on T004, T005 and T006,
  because the fate helpers use `EffectiveComboCode`.
- **US1**: T008 needs T004–T007. T009 needs T006 and T007. T010 needs T005, T007 and T009. T011 needs T004–T007.
  T012 needs T011. T013 [P] needs T005. T014 needs T004–T013. T015 needs T001 (done) and T014.
- **US2 and US3**: validation only, on the T014 build or later. Each needs T001.
- **Polish**: T018 can run any time after the design is stable. T019 and T020 run after the last code change. T021
  runs last.

### Same-file sequencing

- `src/Input/ExclusiveTracker.cpp`: T007 → T009 → T010 → T011 → T012
- `src/Input/InputFilter.cpp`: T006 → T008
- `include/Input/HotkeyMatcher.h`: T005
- `src/Input/HotkeyMatcher.cpp`: T013
- `include/Input/ExclusiveTracker.h`: T011 (signature: + cache, keys)
- `include/Input/InputFilter.h`: T006
- `include/Input/ExclusiveStore.h`, `include/Shared/InputConstants.h`: T004
- `docs/TEST_MATRIX.md`: T003, then ticks in T015, T016, T017 and T020
- `.claude/CLAUDE.md`: T018

---

## Parallel Example

```text
# Foundational, at the start:
Task: "T004 Add the constants, PressFate and the ExclusiveStore fields"
Task: "T005 Add combo helpers and move DescribeHeldCode into include/Input/HotkeyMatcher.h"
Task: "T006 Add EffectiveComboCode in include/Input/InputFilter.h / src/Input/InputFilter.cpp"
Task: "T003 Add TEST_MATRIX cases 6.15–6.23"

# US1, once T005 is done:
Task: "T013 Identical-combo warning in src/Input/HotkeyMatcher.cpp"
Task: "T018 Update .claude/CLAUDE.md"
```

---

## Implementation Strategy

### MVP (User Story 1)

1. T001 (approval), T003, then T004–T007 → T008–T013 → T014.
2. The user runs T015. If it passes, the reported bug is fixed.

### Incremental delivery

1. MVP as above.
2. T016 (US2) and T017 (US3) on the same build. Neither needs new code.
3. T018 → T019 → T020 → T021, then commit when the user asks. Keep the staged distribution package in its own
   commit.

---

## Notes

- **Commits**: only when the user asks. Pushes also need the user's go-ahead.
- **Scope**: no `Domain/`, `Application/` or `Config/` changes. If one seems necessary, stop and revisit the plan.
- **Logs**: every new log uses `MAGIC_DEBUG_LOG` with the `[Input]` prefix, and none fires per frame for held
  repeats (Constitution VI).
- **Staged files**: the user's staged package files must not be unstaged or mixed into this fix's commit.

## Implementation log (2026-11-20)

- **Code tasks done**: T003–T014, T018, T019, T021.
  - Builds, debug first and release last: 0 errors and 0 compiler warnings.
  - Only the existing `vcpkg builtin-baseline` warning appears, plus the `CMAKE_TOOLCHAIN_FILE` "not used" warning
    on reconfigure. That one comes from the staged `CMakeLists.txt` version bump, not from this feature.
  - The MO2 DLL hash matches `build\release\IntegratedMagic.dll`, and the debug PDB was removed.
  - Scope check: `git diff -- src/Domain src/Application include/Domain include/Application src/Config
    include/Config` is empty.
- **Deviations from the task text**:
  - `FateOf` / `OwnerOf` live **inline in `include/Input/ExclusiveStore.h`** and take `bool gamepad`, not
    `RE::INPUT_DEVICE`. Both `InputFilter.cpp` and `ExclusiveTracker.cpp` need them, and they are pure.
  - `ConsumeHeldBackForCombo(winner, hk, n, excl, retained, deferred)` has **no kb/gp "combo down" flags**. It
    consumes, on every device, the codes in the winner's combos.
    - Reason: a multi-key combo accepted on release ("released after full combo → ACCEPTED") is no longer held at
      that moment, so the flags would have skipped the consumption.
  - **Held repeats of a `HeldBack` press are filtered and not stored**. Only the press and its release are kept.
    - Reason: replay drains one event per frame. With holds of up to 0.5 s, about 30 stored repeats could reach the
      game after the live release and leave the button stuck.
    - Recorded in research R11 and the data-model lifecycle.
  - **Helpers added to keep Sonar cognitive complexity in bounds**, with identical behavior:
    - `FollowPressFate` and `PickHoldBackOwner` in `InputFilter.cpp`,
    - `ComputeSlotAcceptance`, `ResolveOverlaps`, `BeatsOnDevice`, `FindHoldReason`, `ResolveFate(s)`,
      `CloseFilterWindow` and `ResetAllFates` in `ExclusiveTracker.cpp`.
  - `ResolveFates` runs in event order, so the last event of each code decides. A release makes the fate `None`.
  - Unknown gamepad codes (`EffectiveComboCode` = -1) are guarded everywhere. `ComboContains(combo, -1)` would
    otherwise match every combo with an unused position.
- **T019 incident**:
  - For files with a single changed range, the PowerShell splat of `--lines` was split into characters, so
    clang-format reformatted the whole file.
  - Two lines outside this feature were changed that way: the include grouping in `ExclusiveTracker.h` and
    `ConsumeHotkeyReloadRequest` in `HotkeyMatcher.cpp`. Both were **reverted by hand**.
  - The re-check with `@()` shows 0 violations on the changed lines.
- **Known limitation**, documented by spec Edge Case "sequential superset" and FR-009 "at the same moment":
  - Take a 3-button chord whose first two buttons form another slot's combo, for example slot 2 `Y + RT` inside
    slot 6 `Y + RT + LB`.
  - If the third button arrives after the shorter slot was accepted (about 50–67 ms after its last button at 60 fps
    with the 50 ms tolerance), the shorter slot fires, and the longer one may fire afterwards as a second press.
  - Avoiding it would delay every activation of the shorter slot. With Require exclusive hotkey ON, the extra
    button already cancels the shorter slot.
- **First in-game run**: working (user, 2026-11-20, 40 ms build).
- **Tuning after that run** (Clarification 2026-11-20, user: "Para combos de 2 botões ou mais deixe a tolerância em
  25ms"):
  - `kFilterReplayDelaySec` (40 ms) is split into `kComboToleranceSec` = **25 ms** and `kSingleKeyAcceptDelaySec`
    = 40 ms. The first is the filter window (`ShouldFilterAndSave` phase 4) and the multi-button pending wait
    (`ComputeAcceptedExclusive`). The second keeps the single-key wait (Require exclusive hotkey) as it was.
  - `kMaxChordHoldSec` is now `2 × kComboToleranceSec` = 50 ms (was 0.15 s): two gaps, as combos have at most 3
    buttons.
  - `FindHoldReason` also holds for a combo **completed in the frame the window ends** (`complete`), and its chord
    cap counts the frame that crosses it (`heldBackSecs − dt`). Without that, a 25 ms window (about 1.5 frames at
    60 fps) would let the first button leak when the second one is seen in the expiry frame.
  - Builds: debug, then release, 0 errors, 0 compiler warnings. The MO2 DLL hash matches `build\release`, and the
    PDB was removed. clang-format: 0 violations on the changed lines.
- **Second in-game run** (user, 25 ms build): every test passed except one slow chord.
  - In the log, Triangle (`gp:13`) was handed to the game 33 ms after its press, and R2 arrived 100 ms after Triangle.
  - The character jumped and slot 2 activated in the air, because Press both at the same time accepts the combo within
    0.2 s. This is the documented "Slow chord" limit.
- **Second tuning** (Clarification 2026-11-20, user: "Vamos com 50ms então"): `kComboToleranceSec` = **50 ms**, so
  `kMaxChordHoldSec` = 100 ms.
  - Builds: debug, then release, 0 errors and 0 compiler warnings. The MO2 DLL hash matches `build\release`, and the
    PDB was removed.
- **In-game tasks closed** (T015, T016, T017, T020): validated by the user on the 25 ms build. The user asked for the
  commit right after setting 50 ms; that build only widens the tolerance.
- **Side observation, out of scope**: in the same log, after landing, the Flames slot (Hold, auto-attack) kept
  restarting its synthetic attack (`StartRequested for 0.15s → UP`) until the combo was released, and never cast. To
  check separately (domain layer).
