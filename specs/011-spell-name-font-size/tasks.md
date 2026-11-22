---

description: "Task list for spec 011: a configurable font size for the spell names"
---

# Tasks: A configurable font size for the spell names

**Input**: design documents in `specs/011-spell-name-font-size/`: [plan.md](plan.md), [spec.md](spec.md),
[research.md](research.md), [data-model.md](data-model.md), [contracts/spell-name-size.md](contracts/spell-name-size.md),
[quickstart.md](quickstart.md).

**Tests**: no automated harness exists (Constitution VI). Validation is by build checks, plus in-game runs by the user,
marked **(in-game, user)** and run through the quickstart.

**Organization**:
- **Foundational** (Phase 2): the setting and the font loader. Every story needs them.
- **US1** (change the size): the control, and the names drawn with the name font.
- **US2** (sharp and in place): the full-screen clip, and the HUD reserve measured with the name font.
- **US3** (the popup): the popup reserve measured with the name font.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: the user story a task belongs to (US1, US2, US3, see spec.md)

## Conventions for every code task

- **Style**: identifiers, comments and log messages are in English. `.clang-format` applies (120 columns, 4-space
  indent). The touched files use LF line endings; keep them. `styles.ini` starts with a UTF-8 BOM; keep it.
- **Debug logs**: `MAGIC_DEBUG_LOG` (compiled in by the `debug` preset only, spec 010). A value used only by a debug
  log gets `[[maybe_unused]]`. Warnings that must reach players use `spdlog::warn`.
- **Scope**: no change in `src/Domain`, `src/Input`, `src/Persistence`, `MagicConfig` or `IntegratedMagic.ini`
  (FR-012). No change in `extern/` or the vcpkg ImGui.
- **Pair rule** (CLAUDE.md, SKSE menu pair rule): every `ImGui::PushFont` / `PopFont` and `PushClipRect*` /
  `PopClipRect` is an unconditional pair with no `return` between them.
- **Build**: the scratchpad script
  `C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\build.ps1
  -Preset <debug|release> -LogFile <path>`. If it is gone, run `cmake --preset <p>` then `cmake --build build/<p>`.
  - **What gets installed**: each build copies its DLL to `D:\ModdingSkyrim\MO2\mods\IntegratedMagic\SKSE\Plugins`
    (the debug build its PDB too), so the build made **last** is the one the game loads. Build release first and
    debug last before an in-game run.
  - **Warnings**: counted from the build log (`warning C`). Both presets must show 0 (merge gate 1).
- **clang-format**: `D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe -i`
  on each changed `.h` / `.cpp`.

---

## Phase 1: Setup

- [X] T001 Record the baseline in the implementation log of `specs/011-spell-name-font-size/tasks.md`:
  - `git status` shows only `specs/011-spell-name-font-size/` untracked, on `feat/spell-name-font-size`;
  - `grep -rn "PushFont\|SetWindowFontScale\|FontGlobalScale" src include` finds nothing (no code depends on a font
    pointer or a font scale today);
  - `grep -rn "GetTextLineHeight" src include` lists the 3 name sites of research R4 (`HudTextUtil.h`,
    `Slotdrawer.cpp`, `Popupdrawer.cpp`) and any other site, which stays on the base font.
- [ ] T002 **(in-game, user)** Before any code change, with the build installed now (`6805b8b`, the same HUD code as
  `main`): take the "before" screenshot of quickstart §1 step 1 (names on, position Bottom, names of 1 or 2 lines).
  Keep it for T017.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the setting and the font loader. No visible change yet: with no control, the scale stays 1.0 and no
name font is built.

- [X] T003 In `include/Config/StyleConfig.h`, add next to `spellNamePadding` (data-model §1):
  - the field `float spellNameScale = 1.f;`;
  - in namespace `IntegratedMagic`: `inline constexpr float kSpellNameScaleMin = 0.5f;`, `kSpellNameScaleMax = 2.0f`,
    `kSpellNameScaleStep = 0.05f`;
  - `inline float NormalizeSpellNameScale(float s)`: rounds to the nearest multiple of 0.05
    (`std::round(s / kSpellNameScaleStep) * kSpellNameScaleStep`), then clamps to [0.5, 2.0]. A NaN returns 1.0.
  - Include `<cmath>` and `<algorithm>` if the header does not already.
- [X] T004 In `src/Config/StyleConfig.cpp`:
  - **Load**, after `spellNamePadding = GetFloat(ini, "HUD", "SpellNamePadding", ...)`:
    `spellNameScale = NormalizeSpellNameScale(GetFloat(ini, "HUD", "SpellNameScale", spellNameScale));`. A missing
    key keeps 1.0 ([contract §1](contracts/spell-name-size.md)).
  - **Save**, after `setFloat("HUD", "SpellNamePadding", ...)`: `setFloat("HUD", "SpellNameScale", spellNameScale);`.
- [X] T005 In `include/UI/FontLoader.h`, declare in namespace `FontLoader`, next to `LoadFontsFromConfig()`:
  - `void RebuildIfNeeded();` (once per frame, before `ImGui_ImplDX11_NewFrame`);
  - `ImFont* SpellNameFont();` (the name font, or `nullptr` = the default font for `ImGui::PushFont`).
  - A comment states the rule of [contract §3](contracts/spell-name-size.md): the atlas changes only there, never
    between `NewFrame` and `Render`.
- [X] T006 In `src/UI/FontLoader.cpp`, restructure the loading (research R1, R3, R8; data-model §2):
  - **State** (anonymous namespace): `std::vector<unsigned char> g_fontData`, `float g_baseSizePx`,
    `float g_builtNamePx = 0`, `float g_pendingNamePx = -1`, `std::chrono::steady_clock::time_point g_pendingSince`,
    `ImFont* g_nameFont = nullptr`, and `constexpr float kFontRebuildSettleSec = 0.25f`,
    `constexpr int kMaxAtlasSide = 16384`.
  - **`WantedNamePx()`**: `showSpellNamesInHud && spellNameScale != 1.f` ?
    `std::round(g_baseSizePx * spellNameScale * 2.f) / 2.f` : `0.f` (the scale is already normalized, so 1.0 is exact).
  - **`AddFamily(float px)`**: adds the font at `px` from `g_fontData` with `AddFontFromMemoryTTF(g_fontData.data(),
    size, px, &cfg, GetGlyphRangesDefault())`, `cfg.FontDataOwnedByAtlas = false`, then one merged
    `AddFontFromMemoryTTF` per enabled range (`MergeMode = true`, `FontDataOwnedByAtlas = false`), in today's order
    (Polish, Cyrillic, Japanese, Chinese, Korean, Greek). Returns the first `ImFont*`. With `g_fontData` empty, it adds
    `AddFontDefault(&cfg)` with `cfg.SizePixels = px` (R8).
  - **`BuildAtlas(float namePx)`**: `io.Fonts->Clear()`, `AddFamily(g_baseSizePx)` (always first, so it stays the
    default font), then `g_nameFont = namePx > 0 ? AddFamily(namePx) : nullptr`, `io.Fonts->Build()`, and
    `ImGui_ImplDX11_InvalidateDeviceObjects()` (include `<imgui_impl_dx11.h>`) so the next
    `ImGui_ImplDX11_NewFrame` uploads it. Sets `g_builtNamePx = namePx`.
  - **`LoadFontsFromConfig()`**: reads the font file once into `g_fontData` (`std::ifstream`, binary), when the path
    exists; else leaves it empty and logs the existing "font not found, using default" line. Sets `g_baseSizePx` to
    `fc.size`, or 13 for the fallback. Then `BuildAtlas(WantedNamePx())`, with no settle time (data-model §2, Init).
  - Remove the old `AddFontFromFileTTF` calls.
- [X] T007 In `src/UI/FontLoader.cpp`, the guard and the logs (research R6, R9; [contract §5](contracts/spell-name-size.md)):
  - **Guard**, in `BuildAtlas`: if `Build()` returns false, or `io.Fonts->TexWidth` or `TexHeight` exceeds
    `kMaxAtlasSide`, and `namePx > 0`: `spdlog::warn` the matching line of contract §5, rebuild with the base font
    alone, and keep `g_builtNamePx = namePx` so the same size is not retried (data-model §2).
  - **Debug log** after each build: `MAGIC_DEBUG_LOG("[HUD] FontLoader: atlas built (base={} px, names={}, scale={})
    {}x{} in {} ms", ...)`, with `names` = `"<px> px"` or `"none"` and the time measured with `steady_clock` around
    the build. Values used only there get `[[maybe_unused]]`.
- [X] T008 In `src/UI/FontLoader.cpp`, implement `RebuildIfNeeded()` and `SpellNameFont()` (data-model §2, States):
  - **Before graphics init**, when `ImGui::GetCurrentContext()` is null: return.
  - `wanted == g_builtNamePx` → `g_pendingNamePx = -1`, return.
  - `wanted != g_pendingNamePx` → `g_pendingNamePx = wanted`, `g_pendingSince = now`, return.
  - `now - g_pendingSince >= kFontRebuildSettleSec` → `BuildAtlas(wanted)`, `g_pendingNamePx = -1`.
  - `SpellNameFont()` returns `g_nameFont`.
- [X] T009 [P] Add `void PrepareFonts();` to `include/Application/HudController.h`, next to `InitializeGraphics()`.
  In `src/Application/HudController.cpp` it calls `FontLoader::RebuildIfNeeded()`.
- [X] T010 In `src/Adapters/Inbound/DXGIPresentHook.cpp`, in `Impl::thunk`, call
  `Application::HudController::Get().PrepareFonts();` right after `ImGui::SetCurrentContext(...)` and before
  `ImGui_ImplDX11_NewFrame()`. A comment says why there ([contract §3](contracts/spell-name-size.md)).
- [X] T011 Build `release`, then `debug`. 0 warnings on both. Record the result in the implementation log.

**Checkpoint**: the HUD draws as before. The debug log has `atlas built (base=28 px, names=none, scale=1)` once at
start.

---

## Phase 3: User Story 1 - The player makes the spell names bigger or smaller (Priority: P1) 🎯 MVP

**Goal**: a "Size (%)" control changes the HUD names live, and the size is saved.

**Independent Test**: quickstart §1 and §2.

- [X] T012 [US1] In `include/UI/HudTextUtil.h`, `DrawSpellLabel`: after the existing early returns (empty text, no
  draw list), `ImGui::PushFont(FontLoader::SpellNameFont());` and `ImGui::PopFont();` at the end of the function, so
  the wrap, the line height and `AddText` use the name font (research R4). Include `"UI/FontLoader.h"`. No `return`
  between the push and the pop; turn `if (lines.empty()) return;` into a branch around the drawing.
- [X] T013 [P] [US1] In `src/UI/MENU.cpp`, inside `if (st.showSpellNamesInHud)`, after the "Padding" control
  ([contract §2](contracts/spell-name-size.md), research R7):
  - `ImGuiMCP::SetNextItemWidth(150.f);`
  - `int pct = static_cast<int>(std::lround(st.spellNameScale * 100.f));`
  - `if (ImGuiMCP::InputInt(S::Get("HUD_SpellNameSize", "Size (%)##spellnamesize").c_str(), &pct, 5, 25))` →
    `st.spellNameScale = IntegratedMagic::NormalizeSpellNameScale(static_cast<float>(pct) / 100.f); dirty = true;`
  - "rounded to a multiple of 5, clamped to [50, 200]" holds because `NormalizeSpellNameScale` rounds to 0.05 and
    clamps to [0.5, 2.0].
- [X] T014 [P] [US1] In `SKSE/Plugins/IntegratedMagic_Strings.txt`, add `HUD_SpellNameSize=Size (%)##spellnamesize`
  right after `HUD_SpellNamePadding=...` (FR-011).
- [X] T015 [P] [US1] In `SKSE/Plugins/IntegratedMagics/styles.ini`, `[HUD]`, add `SpellNameScale = 1.0` right after
  `SpellNamePadding = 10.0` ([contract §1](contracts/spell-name-size.md)). Keep the BOM and LF endings.
- [X] T016 [US1] Build `release`, then `debug`. 0 warnings. Check that the release DLL has no `atlas built` string
  and has the two `FontLoader:` warning strings (`findstr /c:"atlas built" build\release\IntegratedMagic.dll` finds
  nothing). Record it.
- [ ] T017 [US1] **(in-game, user)** Quickstart §1 step 2 (compare with T002) and §2 (live change, burst of clicks,
  typed values, 10 changes, Apply, 3 restarts). Read `IntegratedMagic.log`: one `atlas built` line per settled change.
  Record the result.

**Checkpoint**: US1 works on its own. Long names may still be cut by the HUD window at large sizes (US2).

---

## Phase 4: User Story 2 - Large names stay sharp and in place (Priority: P1)

**Goal**: at every size, names show in full and keep their position.

**Independent Test**: quickstart §3.

- [X] T018 [US2] In `include/UI/HudTextUtil.h`, `DrawSpellLabel`: wrap the drawing loop in `dl->PushClipRectFullScreen();`
  / `dl->PopClipRect();`, inside the font push of T012 (research R5).
- [X] T019 [P] [US2] In `src/UI/Slotdrawer.cpp`, in the `if (st.showSpellNamesInHud)` block that computes
  `textHeightReserve` and the `switch (st.spellNamePosition)` (around line 992): `ImGui::PushFont(FontLoader::SpellNameFont());`
  before `textHeightReserve`, `ImGui::PopFont();` after the `switch`, so `GetTextLineHeight` and `CalcMaxLabelWidth`
  measure with the name font (research R4). The formulas do not change. Include `"UI/FontLoader.h"`.
- [ ] T020 [US2] **(in-game, user)** Quickstart §3: 200% vs 100% screenshots (SC-002), the 6 positions at 200%
  (SC-003), and 50%. Record the result.

**Checkpoint**: US1 and US2 work together.

---

## Phase 5: User Story 3 - The popup names follow the same size (Priority: P2)

**Goal**: the assignment popup's names use the name size and fit.

**Independent Test**: quickstart §4 step 1.

- [X] T021 [P] [US3] In `src/UI/Popupdrawer.cpp`, around `textReserve` (line 331): `ImGui::PushFont(FontLoader::SpellNameFont());`
  before it and `ImGui::PopFont();` right after, so `GetTextLineHeight() * 1.6f` uses the name font. Tooltips, mode
  labels and buttons keep the base font (FR-002). Include `"UI/FontLoader.h"`.
- [ ] T022 [US3] **(in-game, user)** Quickstart §4: the popup at 200% and assigning by click, names off/on at 150%,
  a slot activated during a change, the out-of-range INI value, and optionally the Cyrillic case. Record the result.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T023 [P] In `.claude/CLAUDE.md`:
  - **HUD / UI**: a "Spell name size" paragraph (FR-013): `[HUD]` `SpellNameScale` (0.5–2.0, steps of 0.05,
    `NormalizeSpellNameScale`); the name font in the same atlas, only when the scale is not 1.0 and names are on; the
    rebuild point (`FontLoader::RebuildIfNeeded` from the Present thunk before `ImGui_ImplDX11_NewFrame`, 0.25 s
    settle, no file I/O after init); the size guard; `ImGui::PushFont(FontLoader::SpellNameFont())` for every name
    measurement and draw; the full-screen clip in `DrawSpellLabel`; the logs. Link `specs/011-spell-name-font-size/`.
  - **Common tasks → files**: a row "Spell name size, blurry or cut-off names" → `src/UI/FontLoader.cpp`,
    `include/UI/HudTextUtil.h`, `src/UI/Slotdrawer.cpp`, `src/UI/Popupdrawer.cpp`, `src/UI/MENU.cpp`; check the
    `FontLoader: atlas built` lines first.
- [X] T024 [P] In `docs/TEST_MATRIX.md` section 10, after 10.22, add a group line "Tamanho do nome das magias (spec
  `specs/011-spell-name-font-size/`):" and the cases 10.23–10.27 of quickstart §5, in Portuguese, unticked, each with
  its expected result.
- [X] T025 Run clang-format on every changed `.h` / `.cpp`, rebuild `release` then `debug` with 0 warnings, and check
  `git diff --stat` touches only the files in plan.md "Source Code".
- [ ] T026 Update `specs/011-spell-name-font-size/spec.md` **Status** (Implemented, with what is still open), tick the
  TEST_MATRIX cases the user validated (T017, T020, T022), and complete the implementation log below.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T002 must happen before any code change is installed.
- **Foundational (Phase 2)**: T003 → T004; T005 → T006 → T007 → T008 (same file); T009 after T005; T010 after T009;
  T011 after all. Blocks every story.
- **US1 (Phase 3)**: after Phase 2.
- **US2 (Phase 4)**: T018 after T012 (same file). T019 after Phase 2.
- **US3 (Phase 5)**: T021 after Phase 2.
- **Polish (Phase 6)**: after the stories it documents.

### User Story Dependencies

- **US1**: none beyond Phase 2.
- **US2**: needs US1's control to set large sizes in game (T013), and T012 in the same file.
- **US3**: needs T012 (the popup names are drawn by `DrawSpellLabel`) and the control (T013).

### Parallel Opportunities

- T003 and T005 (different files), then T009 next to T006–T008.
- T013, T014, T015 next to T012.
- T019 and T021 next to T018.
- T023 and T024.

## Parallel Example: User Story 1

```text
T012 include/UI/HudTextUtil.h       (name font in DrawSpellLabel)
T013 src/UI/MENU.cpp                (the control)
T014 SKSE/Plugins/IntegratedMagic_Strings.txt
T015 SKSE/Plugins/IntegratedMagics/styles.ini
```

## Implementation Strategy

### MVP First (User Story 1 Only)

Phases 1–3 give the requested setting. At 200% some long names can be cut by the HUD window until US2.

### Incremental Delivery

The code tasks are small, so the plan is to implement T003–T021 together, build once, and run **one** in-game
session for T017, T020 and T022 (quickstart §1–§4), to save game restarts. T002 still comes first.

## Notes

- **The "before" screenshot (T002)** has to be taken before the new build is installed. If it is skipped, SC-004 is
  checked against a build of `main` instead.
- **Base**: the branch started from `fix/debug-build-define`. It was moved onto `main` at `83598b7` after spec 010
  merged (implementation log, 2026-11-22: base moved to `main`).

## Implementation log

### 2026-11-22: implementation (T001, T003–T016, T018, T019, T021, T023–T025)

- **Baseline (T001)**: `git status` showed only `specs/011-spell-name-font-size/` untracked, on
  `feat/spell-name-font-size`. No `PushFont`, `SetWindowFontScale` or `FontGlobalScale` anywhere. `GetTextLineHeight`
  had exactly the 3 name sites (`HudTextUtil.h:43`, `Slotdrawer.cpp:994`, `Popupdrawer.cpp:331`).
- **"Before" build kept for T002**: the DLL and PDB installed before this work (debug build of `6805b8b`, SHA-256
  `b7990c32…`, the same HUD code as `main`) are copied to
  `C:\Users\igora\AppData\Local\Temp\claude\1894c108ad5d1ad3\8889715e-31f7-488b-8ded-3e6362d57a0b\scratchpad\before-011\`.
  The builds below replaced them in MO2, so T002 is taken by putting these two files back for one run.
- **Code**: as tasks.md, with these details:
  - `NormalizeSpellNameScale` returns 1.0 for NaN. `WantedNamePx` compares the scale with 1.0 within 0.001.
  - `DrawSpellLabel`: `if (lines.empty()) return;` became an `if (!lines.empty())` block, so the `PopFont` is always
    reached. The only other `return` after the push is inside the `flushWord` lambda.
  - The debug log prints the name size with `std::format("{} px", ...)`, so a half-pixel size (42.5) shows as is.
- **clang-format**: it also reflowed a hand-aligned XInput table and the include block in `DXGIPresentHook.cpp`
  (spec 009 code). That file was restored and only the 2 new lines reapplied, so the diff has no unrelated change.
- **Line endings**: the working tree is CRLF (index LF). `src/UI/FontLoader.cpp`, rewritten whole, was converted back
  to CRLF. `styles.ini` keeps its BOM.
- **Builds (T011, T016, T025)**: release then debug, twice (the second after the log format change). 0 warnings
  (`warning C`) and 0 errors on both. The debug DLL installed in MO2 matches `build\debug\IntegratedMagic.dll`
  (SHA-256 `1e4f6bcce5ce8300…`).
- **Strings in the DLLs**: `atlas built` release 0, debug 1. `spell name font too large`, `atlas build failed`,
  `Size (%)##spellnamesize` and `SpellNameScale` are in both.
- **Diff**: 15 files, all in plan.md "Source Code" plus the docs.
- **Open**: T002, T017, T020 and T022 (in game, user), then T026 (ticking the TEST_MATRIX cases).

### 2026-11-22: base moved to `main`

- **Why**: spec 010 merged as #79 (`83598b7`). `git diff 6805b8b origin/main` is empty, so the two trees are identical.
- **How**: `git reset origin/main` with the work still uncommitted. The working tree did not change, so the builds of
  the previous entry (and the DLL installed in MO2) still match the code.
