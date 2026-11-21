---

description: "Task list for spec 008: slot and wheel hotkeys must not trigger Magic Menu actions"
---

# Tasks: Slot and wheel hotkeys must not trigger Magic Menu actions

**Input**: design documents in `specs/008-fix-magic-menu-hotkey-leak/`: [plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md), [contracts/magic-menu-hold.md](contracts/magic-menu-hold.md),
[quickstart.md](quickstart.md).

**Tests**: no automated harness exists (Constitution VI). Validation is in game, through the quickstart and the tasks
marked **(in-game, user)**. They run on the **release** DLL, as approved by the user (spec Clarifications).

**Organization**:
- **Phase 2** builds the whole mechanism, because all three stories use the same hold, consume and hand-over rules.
- **Each story phase** checks its slice of the contract against the code, then validates it in game.
- **Build**: one build (T008) serves every in-game task.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1, US2, US3, see spec.md)

## Conventions for every code task

- Identifiers, comments and log messages are in English. `.clang-format` applies (120 columns, 4-space indent).
- **Logging**: every decision logs with `MAGIC_DEBUG_LOG` and the prefix `[Input] MenuHold:` (contract §5), for presses
  and releases only, never for held repeats.
- **Layers**: the new rules live in `include/Input/` / `src/Input/`. Only `src/Application/InputController.cpp` calls
  the outbound adapter (`IntegratedMagic::detail::EnqueueRetainedEvent`), as it already does for gameplay replays
  (Constitution I).
- **Out of scope**: no change in `src/Domain`, `include/Domain`, `src/UI`, `include/UI`, `src/Config`, `include/Config`,
  `src/Persistence` or the INI (spec FR-012).
- **Gameplay untouched**:
  - no change to `ShouldFilterAndSave`, `FollowPressFate`, `TickFilterWindows`, `RecomputeSlotEdges`,
    `ExclusiveTracker.cpp` or `kComboToleranceSec`;
  - in `FilterEvents`, the only change is T007.
- **Build**: the scratchpad script
  `C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\build.ps1
  -Preset <debug|release> -LogFile <path>`. If it is gone, run `cmake --preset <p>` then `cmake --build build/<p>`.
  The post-build step copies the DLL to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`. `CMakeLists.txt`
  globs `src/` with `CONFIGURE_DEPENDS`, so the new `.cpp` needs no CMake change.

---

## Phase 1: Setup

- [X] T001 In `include/Shared/InputConstants.h`, after `kPressBothAtSameTimeWindowSec`, add
  `inline constexpr float kMagicMenuHoldSec = 0.3f;` with the comment "Window of a held-back press in the Magic Menu
  only: a slot or wheel hotkey key waits this long, or until released, before it reaches the menu (spec 008, R3)".

---

## Phase 2: Foundational (the Magic Menu hold)

**Purpose**: the whole mechanism of [contracts/magic-menu-hold.md](contracts/magic-menu-hold.md), wired into the input
frame, plus the TEST_MATRIX cases.

**⚠️ CRITICAL**: no story validation can start before T008 builds.

- [X] T002 Create `include/Input/MagicMenuHold.h` (data-model):
  - **Includes**: `<array>`, `<cstdint>`, `<deque>`, `<optional>`, `<vector>`, `Config/InputConstants.h`,
    `Input/HotkeyCacheStore.h`, `Input/KeyStateStore.h`, `Input/SlotEdgeStore.h`, `PCH.h`, `Shared/ReplayTypes.h`.
  - **`namespace Input`**:
    - `enum class MenuFate : std::uint8_t { None = 0, Held, Handing, Passed, Consumed };`, commented with the data-model
      table: fate of a code's latest press in the Magic Menu;
    - `struct MenuHeldEvent { RetainedEvent ev{}; int code{-1}; bool gp{false}; bool isPress{false}; float ageSecs{0.f}; };`;
    - `struct MagicMenuHoldStore`, with `std::vector<MenuHeldEvent> queue;` ("held back, arrival order"),
      `std::deque<MenuHeldEvent> outbox;` ("handed to the menu, one sent per frame"),
      `std::array<MenuFate, kMaxCode> kbFate{};` and `std::array<MenuFate, kMaxCode> gpFate{};`;
    - `[[nodiscard]] inline MenuFate& MenuFateOf(MagicMenuHoldStore& h, bool gp, int code) noexcept`, returning a
      `static` dummy reset to `None` for an out-of-range code, as `FateOf` in `include/Input/ExclusiveStore.h` does.
  - **`namespace Input::detail`**:
    - `inline constexpr int kMenuHoldWheelOwner = -2;`. Owner of a combo: a slot index `>= 0`, this value for the
      wheel, or `-1` for none;
    - declarations, each with a one-line comment pointing to its contract section:
      - `void FilterMagicMenuEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots, const HotkeyCacheStore& cache, MagicMenuHoldStore& hold);` (§2)
      - `void TickMagicMenuHold(float dt, const KeyStateStore& keys, const SlotEdgeStore& slots, const HotkeyCacheStore& cache, MagicMenuHoldStore& hold);` (§3)
      - `[[nodiscard]] std::optional<RetainedEvent> PopMagicMenuOutbox(MagicMenuHoldStore& hold);` (§3, send)
      - `void ClearMagicMenuHold(MagicMenuHoldStore& hold, const char* reason);` (§4)
- [X] T003 [P] Add `[[nodiscard]] bool IsMagicMenuOpen();` to `include/Input/InputFilter.h`, next to
  `IsInputBlockedByMenus`, and define it in `src/Input/InputFilter.cpp`:
  `RE::UI::GetSingleton()`, then `IsMenuOpen` of a `static const RE::BSFixedString` `"MagicMenu"`, with false when
  there is no UI.
- [X] T004 Create `src/Input/MagicMenuHold.cpp` implementing the contract (depends on T001, T002). Includes:
  `Input/MagicMenuHold.h`, `Input/HotkeyMatcher.h`, `Input/InputFilter.h` (`EffectiveComboCode`), `PCH.h`.
  - **Anonymous helpers**:
    - `IsKeyHeld(keys, gp, code)`: `(gp ? keys.gpDown : keys.kbDown)[code]`, false out of range;
    - `ForEachHoldCombo(cache, slots, gp, fn)`: calls `fn(owner, combo)` for every active slot
      (`0 .. slots.ActiveSlots()-1`, combo `gp ? hk.gp : hk.kb`), then for `kMenuHoldWheelOwner` with
      `gp ? cache.hud.gp : cache.hud.kb`;
    - `IsHoldCode(cache, slots, gp, code)`: any visited combo `ComboContains` the code (research R2);
    - `FullyHeldOwnerWith(cache, slots, keys, gp, code)`: the first owner whose combo contains the code and is
      `ComboDown` on `gp ? keys.gpDown : keys.kbDown`, else `-1`;
    - `OwnerLabel(owner)`: `"slot=<n>"` or `"wheel"`;
    - `RefreshFate(hold, gp, code, fallback)`: sets the code's fate to `Held` if any `queue` entry has it, else
      `Handing` if any `outbox` entry has it, else `fallback`.
  - **`FilterMagicMenuEvents`**: walk the list as `FilterEvents` does, unlinking removed events.
    - **Button events only**. A mouse event whose raw id `IsMouseWheelId` passes. `code = EffectiveComboCode(dev, raw)`,
      and a code outside `0 .. kMaxCode-1` passes. `gp = dev == kGamepad`, `isRelease = value <= 0`,
      `isFresh = !isRelease && heldSecs <= 0`.
    - **Fresh press**:
      - not `IsHoldCode`: passes, and the fate is untouched;
      - `FullyHeldOwnerWith` gives an owner `!= -1`: fate `Consumed`, log `<label> consumed by <owner>`, removed;
      - otherwise: push `{ev, code, gp, true, 0.f}` to `queue`, fate `Held`, log `<label> held (queue=<n>)`, removed.
    - **Not fresh**, by `MenuFateOf`:
      - `Held`: a release goes to `queue` as `isPress=false`, and a repeat is dropped. Removed either way;
      - `Handing`: a release goes to `outbox`, and a repeat is dropped. Removed either way;
      - `Consumed`: removed. A release sets the fate to `None` and logs `consumed by` with no owner: `<label> release consumed`;
      - `None` / `Passed`: passes, and a release sets the fate to `None`.
    - `<label>` is `DescribeHeldCode(code, gp)`. The `RetainedEvent` is
      `{dev, raw, btn->QUserEvent(), btn->Value(), btn->HeldDuration()}`.
  - **`TickMagicMenuHold`**:
    1. **Consume**: for `gp` in {false, true}, `ForEachHoldCombo`. For every combo `ComboDown` now, erase from `queue`
       and `outbox` every entry with the same `gp` whose code the combo contains. Log `<label> consumed by <owner>` for
       each erased entry, then set each erased code's fate to `Consumed`, or `None` if its last erased entry was a
       release.
    2. **Age**: `ageSecs += dt` for every `queue` press (`dt` clamped at 0).
    3. **Hand over**, FIFO: while `queue` is not empty, the front is due when it is a release, or
       `ageSecs >= kMagicMenuHoldSec`, or `!IsKeyHeld(front)`.
       - **A due press** logs `<label> handed to menu after <s>s (window|released)`, with `window` when the age
         reached the window.
       - **Every due entry** moves to `outbox`, and `RefreshFate(..., Handing)` runs for its code.
       - The loop stops at the first entry that is not due.
  - **`PopMagicMenuOutbox`**: empty → `std::nullopt`. Otherwise pop the front, then
    `RefreshFate(..., front.isPress ? Passed : None)` for its code, and return its `ev`.
  - **`ClearMagicMenuHold`**: `n = queue.size() + outbox.size()`. If `n > 0`, log
    `<n> queued event(s) dropped (<reason>)`. Then clear both containers and fill both fate arrays with `None`.
- [X] T005 Wire the hold into the input frame (depends on T003, T004):
  - **`include/Application/InputController.h`**:
    - `#include "Input/MagicMenuHold.h"`;
    - private members `bool m_prevInMagicMenu{false};` and `Input::MagicMenuHoldStore m_menuHold{};`.
  - **`src/Application/InputController.cpp`, `ProcessAndFilter`**:
    - inside `if (Input::detail::ConsumeHotkeyReloadRequest()) {...}`, add
      `Input::detail::ClearMagicMenuHold(m_menuHold, "reset");`;
    - after `const bool blocked = ...;`, add `const bool inMagicMenu = Input::detail::IsMagicMenuOpen();`, then
      `if (m_prevInMagicMenu && !inMagicMenu) Input::detail::ClearMagicMenuHold(m_menuHold, "Magic Menu closed");` and
      `m_prevInMagicMenu = inMagicMenu;`;
    - right after `Input::detail::FilterMouseForPopup(a_evns);`:
      ```cpp
      // Magic Menu: hotkey keys are held back, consumed by a completed hotkey, or replayed to the menu (spec 008).
      if (inMagicMenu && !wantCaptureBefore) {
          Input::detail::FilterMagicMenuEvents(a_evns, m_keys, m_slots, m_hotkeys, m_menuHold);
          Input::detail::TickMagicMenuHold(dt, m_keys, m_slots, m_hotkeys, m_menuHold);
          if (const auto ev = Input::detail::PopMagicMenuOutbox(m_menuHold))
              IntegratedMagic::detail::EnqueueRetainedEvent(ev->dev, ev->rawIdCode, ev->userEvent, ev->value,
                                                            ev->heldSecs);
      } else if (inMagicMenu) {
          Input::detail::ClearMagicMenuHold(m_menuHold, "reset");
      }
      ```
  - **`OnConfigChanged`** and **`ResetInputState`**: add `Input::detail::ClearMagicMenuHold(m_menuHold, "reset");`.
  - **Not touched**: the replay of gameplay events (`DrainOneDeferredReplayEvent` + `GateHandEvent`) and the
    `if (!blocked) FilterEvents(...)` call.
- [X] T006 [P] In `docs/TEST_MATRIX.md`, section 10, after 10.10, add the spec 008 cases (research R11), in Portuguese
  and in the file's style:
  - **Intro line**: "Magic Menu: atalhos de slot e da wheel sem acionar funções do menu (spec
    `specs/008-fix-magic-menu-hotkey-leak/`):"
  - **10.11**: "Spell em hover, hotkey de teclado de uma tecla e de duas teclas (as duas em até 0,3 s), 10× cada → a
    spell vai para o slot e o menu não reage a nenhuma das teclas; log `MenuHold: ... consumed by slot=`"
  - **10.12**: "Gamepad, layout padrão (`RT` + botão), 10 atribuições, metade com `RT` primeiro e metade com o botão
    primeiro → atribui e nenhum dos dois botões aciona função do menu; shout/power em hover vai para a voz do slot"
  - **10.13**: "Hotkey da wheel no Magic Menu, teclado e gamepad, abrir e fechar 10× cada → a wheel alterna e o menu não
    reage; log `consumed by wheel`"
  - **10.14**: "Botão de hotkey sozinho no Magic Menu (ex.: `RT` sozinho, `Shift` sozinho): toque rápido 10× → o menu
    executa a ação normal uma vez, na soltura; segurado ~1 s → reage em até 0,3 s e continua enquanto segurado"
  - **10.15**: "Botões fora de qualquer hotkey no Magic Menu (navegação, categorias) → sem atraso"
  - **10.16**: "Fechar o Magic Menu com uma tecla de hotkey segurada, e fechar até 0,3 s depois de apertar uma, 5× cada
    → de volta ao jogo, movimento, ataque e slots funcionam no primeiro aperto"
- [X] T007 Remove the dead wheel filter (research R8). Runs after T003, which edits the same `InputFilter.cpp`:
  - in `src/Input/InputFilter.cpp`, `FilterEvents`, change `remove = ShouldFilterAndSave(...) ||
    ShouldFilterHudToggle(dev, code, cache);` to `remove = ShouldFilterAndSave(...);`;
  - delete the declaration in `include/Input/HudToggle.h` and the definition in `src/Input/Hudtoggle.cpp`;
  - drop `#include "Input/HudToggle.h"` from `InputFilter.cpp` if nothing else from it is used there. Keep
    `IsHudToggleCombo` and `UpdateHudToggleState`.
- [X] T008 Build debug, then release **last**, so MO2 holds the release DLL (research R12). Depends on T001–T007.
  - Expect 0 errors and 0 new compiler warnings in both presets (merge gate 1).
  - Check that the MO2 DLL hash matches `build\release\IntegratedMagic.dll`.
  - Delete any `IntegratedMagic.pdb` left in the MO2 plugin folder.

**Checkpoint**: the Magic Menu hold is built; story validation can start.

---

## Phase 3: User Story 1 - Quick bind without side effects in the Magic Menu (Priority: P1) 🎯 MVP

**Goal**: hover + slot hotkey assigns the entry, and the Magic Menu does not react to any of the hotkey's keys.

**Independent Test**: [quickstart.md](quickstart.md) §1 (SC-001).

- [X] T009 [US1] Desk-check `src/Input/MagicMenuHold.cpp` against contract §2–§4 for slot hotkeys. Trace each case and
  record the result in the implementation log:
  - **single-key hotkey**: the fresh press is consumed at once;
  - **2-key keyboard hotkey within 0.3 s**: the first key is held, then the second is consumed at once, and the first
    is consumed by the tick in the same frame;
  - **gamepad `RT` + face, in both orders**: as above;
  - **release of a consumed key**: dropped;
  - **quick bind and consumption agree**: both read the same `KeyStateStore` in the same frame. `ProcessButtonEvents`
    runs before `FilterMagicMenuEvents`, and `HudController::OnFrame` runs after `ProcessAndFilter`.
  - If a case fails, fix `src/Input/MagicMenuHold.cpp` and rebuild as in T008.
- [X] T010 [US1] **(in-game, user)** [quickstart.md](quickstart.md) §1 on the T008 build. Check `skse64.log` first:
  the `IntegratedMagic` line must load. Then send `IntegratedMagic.log`.

### Overlapping hotkeys (found in T010, spec FR-013, research R13)

- [X] T019 [US1] Create `include/Input/QuickBindResolver.h` and `src/Input/QuickBindResolver.cpp`
  (contract §6, data-model `QuickBindState`):
  - **`QuickBindState`**: `wasDown`, `pending` (`std::uint64_t`) and `pendingSecs`
    (`std::array<float, kInputMaxSlots>`);
  - **`ResolveQuickBind(st, active, dt, slotCount, cache, keys, slots)`**, per contract §6:
    - pending slots first;
    - then rising slots, which are suppressed by a held longer combo or a lower identical one, go pending when a
      longer combo exists, or are assigned;
    - inactive frames drop pending slots and only track `wasDown`;
  - **`ResetQuickBind`**;
  - logs with the `[Input] QuickBind:` prefix.
- [X] T020 [US1] Wire it (depends on T019):
  - **`include/Application/InputController.h`**: `#include "Input/QuickBindResolver.h"`, member
    `Input::QuickBindState m_quickBind{};`, and `std::uint64_t ResolveQuickBindSlots(bool active, int slotCount);`;
  - **`src/Application/InputController.cpp`**:
    - `ResolveQuickBindSlots` calls `ResolveQuickBind` with `m_lastDt`;
    - `ResetQuickBind` runs in the hotkey-reload block, `OnConfigChanged` and `ResetInputState`;
  - **`src/Application/HudController.cpp`**: replace the `s_wasDown` rising-edge block with
    `ResolveQuickBindSlots(inMagicMenu && !nowPopupOpen, n)`, called every frame, then `ExecuteHotkeyAssignment` on
    the returned mask.
- [X] T021 [US1] Build as in T008, with 0 errors and 0 new warnings, and the hash check. Then clang-format the new files
  and the changed ranges.
- [X] T022 [US1] **(in-game, user)** [quickstart.md](quickstart.md) §1 step 5 (SC-007) on the T021 build, plus a
  short re-run of §1–§2 to confirm nothing else changed. Then tick TEST_MATRIX 10.17.

**Checkpoint**: quick bind no longer triggers menu functions (MVP).

---

## Phase 4: User Story 2 - The wheel toggle does not trigger Magic Menu actions (Priority: P1)

**Goal**: the wheel hotkey opens and closes the wheel, and the Magic Menu does not react to it.

**Independent Test**: [quickstart.md](quickstart.md) §2 (SC-002).

- [X] T011 [US2] Desk-check the wheel path:
  - `ForEachHoldCombo` visits `cache.hud` for both device families;
  - `UpdateHudToggleState` (`src/Input/Hudtoggle.cpp`) still sees the rising edge, since key state is recorded before
    any filter;
  - with the wheel open, `FilterMouseForPopup` runs before `FilterMagicMenuEvents`, so the wheel's own controls are
    unchanged;
  - `ShouldFilterHudToggle` has no reference left: `grep -r ShouldFilterHudToggle src include` is empty.

  Record the result in the implementation log.
- [X] T012 [US2] **(in-game, user)** [quickstart.md](quickstart.md) §2 on the T008 build.

---

## Phase 5: User Story 3 - Normal Magic Menu use and gameplay stay as they are (Priority: P2)

**Goal**:
- **Used alone**: a hotkey button pressed alone still does its menu action, within 0.3 s, or at its release.
- **Other buttons**: no delay.
- **Gameplay**: unchanged.

**Independent Test**: [quickstart.md](quickstart.md) §3–§5 (SC-003 to SC-006).

- [X] T013 [US3] Scope review (research R9). Record the result in the implementation log.
  - **Gameplay code**: `git diff main -- src/Input/ExclusiveTracker.cpp include/Input/ExclusiveStore.h
    src/Input/ReplaySystem.cpp` must be empty. `git diff main -- src/Input/InputFilter.cpp` must show only T003 and
    T007.
  - **Gating**: the new calls in `InputController::ProcessAndFilter` run only when `inMagicMenu`, and the 0.3 s constant
    is used only in `src/Input/MagicMenuHold.cpp`.
  - **Pass-through**: a non-hold code, mouse movement, thumbsticks and the mouse wheel pass with no delay (contract §1,
    FR-005).
- [ ] T014 [US3] **(in-game, user)** [quickstart.md](quickstart.md) §3, §4 and §5 on the T008 build. That covers the
  hotkey buttons used alone, no stuck key after closing the menu, and the TEST_MATRIX section 6 and 10.1–10.3
  regression. Then tick TEST_MATRIX 10.11–10.16 and what passed in section 6.

---

## Phase 6: Polish & cross-cutting concerns

- [X] T015 [P] In `.claude/CLAUDE.md`, input system section:
  - **A "Magic Menu hold (spec 008)" bullet block**, after the "Hand-button gate" block:
    - why: the Magic Menu blocks slot input, which also skipped the whole filter;
    - the rules: hold codes, the 0.3 s window or the release, FIFO hand-over, consumption on a fully held slot or
      wheel combo, one replay per frame;
    - the store `MagicMenuHoldStore` and its clears: menu close, game load, hotkey reload, capture;
    - the log lines `[Input] MenuHold: ... held / consumed by slot=<n>|wheel / handed to menu after <s>s
      (window|released) / queued event(s) dropped`.
  - **In "Common tasks"**, the row "Hotkey in the Magic Menu triggers a menu action (quick bind or wheel)" →
    `src/Input/MagicMenuHold.cpp`, `src/Application/InputController.cpp` (`ProcessAndFilter`). Say to check the
    `MenuHold:` lines first.
- [X] T016 Run clang-format in dry-run mode on the new files and on the changed line ranges of each changed `.cpp` / `.h`
  only, and fix any violation. The binary is
  `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe --dry-run`, with
  `--lines` passed through `@()` in PowerShell. If a fix changes code, rebuild as in T008.
- [X] T017 Final scope check:
  - `git diff --stat main -- src/Domain include/Domain src/UI include/UI src/Config include/Config src/Persistence SKSE`
    must be empty;
  - `git diff --stat main` must show only this spec's files, `docs/TEST_MATRIX.md`, `.claude/CLAUDE.md` and the
    source files of T001–T007 and T019–T020;
  - the MO2 DLL hash must still match `build\release\IntegratedMagic.dll`.
- [X] T018 Pre-commit review:
  - set `**Status**` in `specs/008-fix-magic-menu-hotkey-leak/spec.md`;
  - complete the implementation log with the build, the desk checks and the in-game results;
  - review `git diff`.

  Commit and push only when the user asks.

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1** (T001): first.
- **Phase 2**:
  - T002 after T001;
  - T003 and T006 at any time (different files);
  - T007 after T003 (same `InputFilter.cpp`);
  - T004 after T002;
  - T005 after T003 and T004;
  - T008 after T001–T007.
- **Phases 3–5**:
  - desk checks (T009, T011, T013) after T008;
  - in-game tasks (T010, T012, T014) on the T008 build, in any order. T014 ticks the TEST_MATRIX cases.
- **Phase 6**: T015 at any time after T005. Then T016 → T017 → T018, last.

### User story dependencies

- **US1 (P1)**: the MVP. It depends only on Phase 2.
- **US2 (P1)**: independent of US1. It uses the same mechanism with the wheel combo.
- **US3 (P2)**: validates the same build. It has no code of its own unless T013 finds a gap.

### Same-file sequencing

| File | Order |
|---|---|
| `include/Input/InputFilter.h` | T003 |
| `src/Input/InputFilter.cpp` | T003 → T007 |
| `include/Input/MagicMenuHold.h` | T002 → T004 |
| `src/Application/InputController.cpp` / `.h` | T005 |
| `docs/TEST_MATRIX.md` | T006 → T014 (ticks) |

---

## Parallel Example

```text
# Phase 2, after T001:
T002  include/Input/MagicMenuHold.h          (types and declarations)
T003  include/Input/InputFilter.h + .cpp     (IsMagicMenuOpen)
T006  docs/TEST_MATRIX.md                    (10.11–10.16)
# then T004 (after T002) and T007 (after T003) can run together

# After T008, the three desk checks can run together:
T009  slot hotkeys   T011  wheel   T013  scope
```

---

## Implementation Strategy

### MVP first (US1)

1. Phases 1–2: the constant, the hold, the wiring, the build (T001–T008).
2. US1: desk check, then in-game quick bind (T009–T010).
3. **Stop and validate**: quick bind with no menu reaction.

### Incremental delivery

1. US2 (T011–T012): the wheel, on the same build.
2. US3 (T013–T014): normal menu use, no stuck key, gameplay regression.
3. Polish (T015–T018).

## Notes

- Commit or push only when the user asks.
- If an in-game task shows the menu still reacting, check the `MenuHold:` lines for that key before changing the code:
  - `held` with no `consumed`: the hotkey did not complete within the frame check;
  - no line at all: the key is not a hold code, or the event never reached the filter.

## Implementation log

### 2026-11-21: the Magic Menu hold, build, desk checks (T001–T009, T011, T013, T015, T016)

- **T001–T005**: as written. Files: `include/Shared/InputConstants.h`, `include/Input/MagicMenuHold.h`,
  `src/Input/MagicMenuHold.cpp`, `include/Input/InputFilter.h` / `src/Input/InputFilter.cpp` (`IsMagicMenuOpen`),
  `include/Application/InputController.h` / `src/Application/InputController.cpp`.
- **T006**: TEST_MATRIX 10.11–10.16.
- **T007**: `ShouldFilterHudToggle` removed, and so is `IsHudToggleCombo`. That one is a deviation from the task text,
  which said to keep it: its only caller was `ShouldFilterHudToggle`, so it would have been dead too. `InputFilter.cpp`
  no longer includes `Input/HudToggle.h`, since nothing else from it is used there.
- **T008**:
  - debug and release built with exit 0 and no compiler warnings. The only `warning` line is vcpkg's
    builtin-baseline notice;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `FF85932F…C939`), and the copied PDB was
    deleted.
- **T009 desk check** (slot hotkeys, contract §2–§4), traced in `src/Input/MagicMenuHold.cpp`:
  - **single-key hotkey**: `ProcessButtonEvents` marks the key down before the filter, so `FullyHeldOwnerWith` finds
    the slot on the fresh press, and it is consumed at once. Its repeats and release are dropped (`Consumed`);
  - **`Shift + 2` within 0.3 s**: `Shift` is queued (`Held`), and its repeats are dropped. `2` arrives with the combo
    fully held, so it is consumed on its fresh press. The tick of the same frame erases `Shift` from the queue
    (`ConsumeForCombo`, fate `Consumed`), and its release is dropped later;
  - **gamepad `RT` + face, either order**: same path. The first button is queued, and the second is consumed at
    once and takes the first in the tick;
  - **quick bind agrees with consumption**: both read `KeyStateStore`, which `ProcessButtonEvents` wrote earlier in
    `ProcessAndFilter`. `HudController::OnFrame` runs after `ProcessAndFilter` in `PollInputDevicesHook`. Result: OK.
- **T011 desk check** (wheel):
  - `ForEachHoldCombo` visits `cache.hud.kb` / `gp` after the slots;
  - `UpdateHudToggleState` runs on the recorded key state, before any filter;
  - `FilterMouseForPopup` runs before `FilterMagicMenuEvents`, so the wheel's own controls are unchanged;
  - `git grep ShouldFilterHudToggle IsHudToggleCombo` is empty. Result: OK.
- **T013 scope review**:
  - `git diff main` of `ExclusiveTracker.cpp`, `ExclusiveStore.h` and the replay system is empty;
  - `InputFilter.cpp` shows only `IsMagicMenuOpen`, the removed include and the dead call;
  - the hold runs only under `inMagicMenu && !wantCaptureBefore`, and `kMagicMenuHoldSec` is used only in
    `MagicMenuHold.cpp`;
  - non-hold codes, mouse movement, thumbsticks and the wheel pass with no delay. Result: OK.
- **T015**: `.claude/CLAUDE.md` has the "Magic Menu hold" block under the input system and the Common tasks row.
- **T016**: clang-format dry run clean on the new files and every changed range.

**Next**: T010, T012 and T014, in game, by the user, on the T008 build.

### 2026-11-21: first in-game run, overlapping hotkeys (T019–T021)

- **User report**: everything works, except with overlapping hotkeys. Quick-binding with `R2 + L2 + X` also filled
  the `R2 + X` slot.
- **Log** (14:39:54.236 and 14:40:15.898): two `ComputeSpellAssignment` lines in the same frame (slots 3 and 7, then 0
  and 4). Every key was consumed (`MenuHold: ... consumed by slot=`), so nothing leaked to the menu, and the wheel
  toggled with `consumed by wheel` each time.
- **Cause**: `HudController` assigned every slot whose hotkey rose in the frame, with no overlap rule (research R13).
- **T019–T020**: `QuickBindResolver`. HudController now asks `InputController::ResolveQuickBindSlots` for the slots
  to assign. Spec FR-013 / SC-007, research R13, data-model, contract §6, quickstart §1 step 5, TEST_MATRIX 10.17 and
  `.claude/CLAUDE.md` were updated.
- **T021**:
  - debug and release built with exit 0 and no compiler warnings;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll` (SHA-256 `76D06DF3…8462`), and the PDB was deleted;
  - clang-format is clean on the new files and the changed ranges.
- **Self-check of the reported case**: `RT + LT + A` and `RT + A` both rise in one frame. Step 2 of
  `ResolveQuickBind` suppresses `RT + A` (`LongerHeld`) and assigns `RT + LT + A`. If `RT + A` completes a frame
  earlier, it goes pending (`LongerExists`), then is suppressed when the longer one becomes held.

**Next**: T022 (overlap test), with T010, T012 and T014 still open, all on the T021 build.

### 2026-11-21: validation and closing (T010, T012, T017, T018, T022)

- **T010 / T012**, first in-game run on the T008 build (log of 14:39–14:40):
  - gamepad quick binds: 6 assignments, each with its keys `consumed by slot=`;
  - the wheel toggled 14 times, 10 on gamepad (`LB + RB`) and 4 on keyboard (`H`), each with `consumed by wheel` and no
    menu reaction;
  - the user: "Quase perfeito", the only problem being the overlap of R13.
- **T022**, overlapping hotkeys on the T021 build: the user confirmed it works ("Funciona"). The log on disk still
  ends at 14:40:21, before the T021 build, so the `QuickBind:` lines of that run were not kept. The result is
  recorded as reported by the user.
- **TEST_MATRIX**:
  - ticked: 10.11–10.13 and 10.17;
  - still open: 10.14–10.16 (a hotkey button alone, buttons outside every hotkey, closing the menu with a key held)
    and the section 6 regression, as the log of the run shows only one `handed to menu` and no menu close with a key
    held. T014 stays open for them.
- **T017**:
  - nothing changed in `src/Domain`, `include/Domain`, `src/UI`, `include/UI`, `src/Config`, `include/Config`,
    `src/Persistence` or `SKSE`;
  - the changes are this spec, `docs/TEST_MATRIX.md`, `.claude/CLAUDE.md` and the source files of T001–T007 and
    T019–T020;
  - the MO2 DLL matches `build\release\IntegratedMagic.dll`.
- **T018**: spec `Status` set to Implemented. The user's own deletion of `.claude/tasks/bug-05-arma-esquerda-sacada.md`
  (bug closed by spec 007) goes in a separate commit.
