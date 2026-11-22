# Implementation Plan: A configurable font size for the spell names

**Branch**: `feat/spell-name-font-size` | **Date**: 2026-11-22 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/011-spell-name-font-size/spec.md`

## Summary

The spell names in the HUD and the assignment popup use the plugin's single font, prepared once at 28 px. A player
asked for bigger names, and today the size can only be changed for all the plugin's text, by editing `styles.ini` and
restarting.

**Approach**:
1. **The setting** (R7): `StyleConfig::spellNameScale`, 50% to 200% in 5% steps, saved as `[HUD]` `SpellNameScale`
   in `styles.ini`. A "Size (%)" control sits under "Padding" in the SKSE menu's HUD section.
2. **Sharp text** (R1): a second font in the ImGui atlas, the name font, rasterized at the name size with the same
   character sets. At 100%, or with names off, there is no name font, so the default look is unchanged.
3. **The rebuild** (R2, R3): when the wanted size changes and stays for 0.25 s, the atlas is rebuilt in the Present
   thunk, before `ImGui_ImplDX11_NewFrame`, from a font buffer read once at init. No file I/O after init.
4. **Drawing** (R4, R5): `DrawSpellLabel` and the name reserve computations push the name font. Names are drawn with
   a full-screen clip, so their window never cuts them off.
5. **Safety** (R6, R8): an atlas too large for a DX11 texture falls back to the base font with a warning. The
   fallback font gets the name size too.
6. **Ship with it** (R9): CLAUDE.md, TEST_MATRIX 10.23–10.27 and in-game validation on the debug build.

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive- /Zc:preprocessor`, CMake presets
with Ninja.

**Primary Dependencies**:
- **ImGui 1.89.1** (vcpkg, `imgui_impl_dx11` and `imgui_impl_win32`), for the HUD context. Its font atlas is static
  per build (R1).
- **SKSE Menu Framework** (`ImGuiMCP::`), for the settings page, which draws in the framework's own ImGui context.
- **SimpleIni**, through `StyleConfig`, for `styles.ini`.

**Storage**: `styles.ini`, one new key, `[HUD]` `SpellNameScale` (default 1.0). No change to `IntegratedMagic.ini`,
`SaveSpellDB` or `SpellSettingsDB`.

**Testing**: no automated harness. Both presets build with 0 warnings, then in-game validation following
[quickstart.md](quickstart.md), with screenshots for SC-002 and SC-004.

**Target Platform**: Windows x64, Skyrim SE / AE.

**Project Type**: SKSE plugin (`IntegratedMagic.dll`).

**Performance Goals**:
- **Each frame**: one size compare and a clock read in `RebuildIfNeeded`, and one `PushFont` / `PopFont` pair per name
  drawn.
- **A rebuild**: only after a change; a few milliseconds with the shipped font (Latin sets). The debug log records its
  duration.
- **Memory**: the name font with the shipped font is a few hundred glyphs at most 56 px. User CJK fonts are bounded by
  the guard (R6).

**Constraints**:
- **The default look** (FR-003, SC-004): at 100% no font is added, and every measurement uses the base font.
- **Other text** (FR-002, FR-012): the base font is re-added with the same parameters and stays the atlas default.
- **Per-frame rules** (Constitution, Technical Constraints): no file I/O and no blocking work each frame (R3).
- **The atlas** may change only outside `NewFrame` … `Render` (R1).

**Scale/Scope**: about 8 files. `StyleConfig` (h/cpp), `FontLoader` (h/cpp), `HudController` (h/cpp),
`DXGIPresentHook.cpp`, `HudTextUtil.h`, `Slotdrawer.cpp`, `Popupdrawer.cpp`, `MENU.cpp`, the strings file and the
docs.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| I. Layered Architecture | PASS | The hook calls `Application::HudController::PrepareFonts()`, which calls `FontLoader` (`UI/`). `UI/` reads `StyleConfig`, as every HUD look setting does today. The Ports cover gameplay config, so no port is added (R7). No `MagicState` access, no game mutation. |
| II. Main-Thread State Machine | PASS | No `MagicState` or `KeyStateStore` change. The menu and the Present thunk share `StyleConfig` on the render thread, as the other HUD settings do (R2). |
| III. Never Strand the Player | PASS | No session, input or restore change. A rebuild during an active slot only redraws text, and names are hidden during a session today. |
| IV. Dual-Cast Sync | N/A | `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt` and `SyntheticInput` are not touched. |
| V. Opt-In & Backward Compatibility | PASS | A new key with a safe default (1.0 = today's look). A missing or out-of-range value is handled (R7). No key renamed or removed; `[Font]` `Size` keeps its meaning. |
| VI. Observable & Verified In-Game | PASS | Debug log per atlas build, a warning on the fallback (R9). TEST_MATRIX 10.23–10.27 are added and validated in game on the debug build (quickstart). |
| Technical Constraints (per-frame work) | PASS | The font file is read once at init; a rebuild reads memory only and runs only after a settled change (R3). |
| Merge gates | PLANNED | 0 warnings on both presets, clang-format, the TEST_MATRIX cases, CLAUDE.md updated. |

**Post-design re-check (after Phase 1)**: PASS. The contracts add one INI key, one control, one call in the Present
thunk and two log lines. Nothing in them reaches the domain or input layers.

## Project Structure

### Documentation (this feature)

```text
specs/011-spell-name-font-size/
├── spec.md
├── plan.md              # This file
├── research.md          # R1–R9
├── data-model.md        # The setting and the font state
├── quickstart.md        # Build and in-game validation
├── contracts/
│   └── spell-name-size.md   # INI key, menu control, font rules, logs
├── checklists/
│   └── requirements.md
└── tasks.md             # /speckit-tasks
```

### Source Code (repository root)

```text
include/Config/StyleConfig.h          # spellNameScale + kSpellNameScaleMin/Max/Step
src/Config/StyleConfig.cpp            # Load (clamp, round) and Save of [HUD] SpellNameScale
include/UI/FontLoader.h               # RebuildIfNeeded(), SpellNameFont()
src/UI/FontLoader.cpp                 # font buffer, BuildAtlas(), settle timer, size guard, logs
include/Application/HudController.h   # PrepareFonts()
src/Application/HudController.cpp     # PrepareFonts() -> FontLoader::RebuildIfNeeded()
src/Adapters/Inbound/DXGIPresentHook.cpp  # PrepareFonts() before ImGui_ImplDX11_NewFrame
include/UI/HudTextUtil.h              # DrawSpellLabel: name font + full-screen clip
src/UI/Slotdrawer.cpp                 # name reserve measured with the name font
src/UI/Popupdrawer.cpp                # popup name reserve measured with the name font
src/UI/MENU.cpp                       # "Size (%)" control under "Padding"
SKSE/Plugins/IntegratedMagic_Strings.txt  # HUD_SpellNameSize
SKSE/Plugins/IntegratedMagics/styles.ini  # SpellNameScale = 1.0 (shipped default, documents the key)
.claude/CLAUDE.md                     # HUD / UI: spell name size
docs/TEST_MATRIX.md                   # 10.23–10.27
```

**Structure Decision**: the existing layers. The font work stays in `UI/FontLoader`, reached from the Present hook
through `Application::HudController`, the path `InitializeGraphics` already uses.

## Complexity Tracking

No violation to justify.
