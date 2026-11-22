# Research: A configurable font size for the spell names

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Date**: 2026-11-22

Source: `feat/spell-name-font-size` at `6805b8b`, and the ImGui 1.89.1 sources vcpkg builds
(`build/release/vcpkg_installed/vcpkg/blds/imgui/src/v1.89.1-92f3f9fec0.clean/`).

## R1. How sharp text at another size is made in ImGui 1.89.1

**Findings**:
- **The atlas**: ImGui 1.89.1 rasterizes every font into one texture (the font atlas) at a fixed pixel size, when the
  atlas is built. `ImGui::SetWindowFontScale` and `ImDrawList::AddText(font, size, ...)` only stretch those glyphs:
  above the built size they blur, and below it they alias.
- **The plugin today**: `FontLoader::LoadFontsFromConfig` (`src/UI/FontLoader.cpp`) adds one font at `[Font]` `Size`
  (28 px), plus merged character sets, once, from `HudController::InitializeGraphics` in `D3DInitHook`. The DX11
  backend builds and uploads the atlas on the first `ImGui_ImplDX11_NewFrame`.
- **Rebuilding**: the backend recreates its device objects, the font texture included, in `ImGui_ImplDX11_NewFrame`
  whenever `pFontSampler` is null (`imgui_impl_dx11.cpp:594`). `ImGui_ImplDX11_InvalidateDeviceObjects()` (public)
  releases them and clears the atlas `TexID`. So a rebuild is: change the atlas, invalidate, and let the next
  `ImGui_ImplDX11_NewFrame` upload it.
- **When the atlas may change**: only outside `NewFrame` … `Render` (the atlas is locked in between). The Present
  thunk (`DXGIPresentHook.cpp:164`) calls `ImGui::SetCurrentContext` and then `ImGui_ImplDX11_NewFrame`, so the
  point between the two is safe.
- **Missing glyphs**: the atlas builder skips code points the font does not have (`imgui_draw.cpp:2403`). The shipped
  `FuturaStd-Book.otf` is 27 KB, so it has no CJK glyphs, and the CJK ranges add nothing with it.

**Decision**: a second font, the **name font**, in the same atlas, rasterized at `[Font] Size × spell name scale`,
with the same character sets as the base font. The atlas is rebuilt when the wanted name font size changes.
- **At 100%**, and while "Show spell names" is off, no name font is added and the names use the base font. That keeps
  the default look identical (FR-003, SC-004) and costs no memory.
- **The base font** is re-added first, with the same parameters, so it stays the atlas default (`io.FontDefault`
  unset = first font) and every other text keeps its look (FR-002, FR-012).

**Alternatives considered**:
- **Scale the base font** (`SetWindowFontScale`, `AddText` with a size): no rebuild, but blurry above 100% (fails
  FR-007). Rejected.
- **Pre-load a 200% font and scale it down**: one build, but every size below 200% is a downscale (aliasing), 100% no
  longer matches today, and the memory is always spent. Rejected.
- **Upgrade to ImGui 1.92 (dynamic font sizes)**: sharp text at any size with no rebuild, but it changes the font API
  and the DX11 backend contract for the whole HUD, and the vcpkg baseline. Out of proportion for one setting.
  Rejected.

## R2. When and where the rebuild runs

**Decision**:
- **Where**: `FontLoader::RebuildIfNeeded()`, called from the Present thunk through
  `Application::HudController::PrepareFonts()`, right after `ImGui::SetCurrentContext` and before
  `ImGui_ImplDX11_NewFrame` (R1).
- **The trigger**: the wanted name font size, in pixels, which is 0 for "no name font" (scale 100%, or names off).
  When it differs from the built one and has stayed the same for `kFontRebuildSettleSec` (0.25 s), the atlas is
  rebuilt. The settle time keeps a burst of clicks on the size control (5% each) to one rebuild, and stays well inside
  FR-005 (1 s).
- **Live preview**: the menu writes `StyleConfig::spellNameScale` directly, like every other HUD setting, so the HUD,
  which stays visible while the SKSE menu is open, shows the new size about 0.25 s after the last click. "Apply
  changes" saves it, like the others. This answers the spec's open point (Assumptions, "Live preview").
- **Threads**: the SKSE menu and the Present thunk both run on the render thread, as every other `StyleConfig` setting
  already assumes. No new synchronization.

**Alternatives considered**: rebuild only on "Apply changes" (no preview, unlike the rest of the HUD section);
rebuild on every change with no settle time (a rebuild per click, and per keystroke when typing).

## R3. The per-frame rules (Constitution, Technical Constraints)

**Findings**: per-frame code must not do file I/O, blocking waits or unbounded allocations. Today
`AddFontFromFileTTF` reads the font file once per added font (up to 7 times, with the merged sets), at init only.

**Decision**:
- **No file I/O in a rebuild**: `LoadFontsFromConfig` reads the font file once into a buffer the loader keeps, at
  init (where it reads it today). Every font, at init and in a rebuild, is added with `AddFontFromMemoryTTF` and
  `ImFontConfig::FontDataOwnedByAtlas = false`, so `io.Fonts->Clear()` does not free the buffer.
- **Not per frame**: the check in `RebuildIfNeeded` is two float compares and a clock read. The rebuild itself runs
  only after a change (R2): a few milliseconds for the Latin sets, more with large CJK sets in a user's own font
  (R6). The debug log records its duration.
- **Bounded memory**: the name font is at most 2× the base size (FR-004), and the atlas texture size is checked (R6).

## R4. Drawing and measuring with the name font

**Findings**: `HUD::DrawSpellLabel` (`include/UI/HudTextUtil.h`) wraps with `ImGui::CalcTextSize`, steps lines with
`ImGui::GetTextLineHeight()` and draws with `ImDrawList::AddText(pos, col, text)`, all using the current font. The
space reserved for names also uses the current font: `Slotdrawer.cpp:993` (`GetTextLineHeight() * 2 + 8 + padding`)
and `CalcMaxLabelWidth`, and `Popupdrawer.cpp:331` (`GetTextLineHeight() * 1.6 + padding`).

**Decision**:
- `FontLoader::SpellNameFont()` returns the name font, or `nullptr` when there is none. `ImGui::PushFont(nullptr)`
  pushes the default font, so callers push it unconditionally and always pop.
- `ImGui::PushFont(FontLoader::SpellNameFont())` / `PopFont()` wraps:
  - the body of `DrawSpellLabel` (measure, wrap and draw);
  - the reserve computations in `Slotdrawer.cpp` (line height and `CalcMaxLabelWidth`) and `Popupdrawer.cpp`.
- **Nothing else** is drawn with it: tooltips, the mode labels and the popup's buttons keep the base font (FR-002).
- **Pair rule**: the push and pop are an unconditional pair with no early `return` between them. In `DrawSpellLabel`
  the push comes after the existing early returns.
- **Positions and padding** (FR-009): unchanged. The anchor is still slot radius + padding (+ the hotkey icon reserve),
  so a larger font moves the text block outward only by its own height or width.

## R5. Names cut off by the window edges

**Findings**: the HUD and the popup are ImGui windows sized from the reserve (`SetNextWindowSize`, `Slotdrawer.cpp:1117`,
`Popupdrawer.cpp:347`), and a window's draw list is clipped to the window. The vertical reserve is 2 lines, and
`DrawSpellLabel` wraps at the slot diameter, so a 2-spell name (`Flames | Healing`) can need 3 lines even at 100%.
The third line is clipped today.

**Decision**:
- `DrawSpellLabel` draws inside `dl->PushClipRectFullScreen()` / `dl->PopClipRect()`, so a name is never clipped by
  its window (FR-008, SC-003).
- **The reserve** keeps its formula, with the name font's line height: 2 lines, or 1.6 in the popup. It is not sized
  from the assigned names, because then the HUD would shift every time a longer name is assigned (the HUD anchor uses
  the reserve, `ComputeHudCenter`).
- **Effect at 100%**: identical where a name fits in the window; a line that was clipped before now shows. SC-004 is
  checked with names of 1 or 2 lines (quickstart §2).

**Alternatives considered**: size the reserve from the longest wrapped name (the HUD moves with each assignment);
wrap at a width that grows with the font (changes the look of Top/Bottom names at 100%).

## R6. Large character sets and the atlas size

**Findings**: a user can point `[Font] Path` at a font with CJK glyphs and turn on Chinese and Japanese. Those sets are
about 5,000 glyphs. At 56 px (200% of 28), the name font alone needs about 5,000 × 56 × 56 ≈ 16 M texels: a 4096 ×
4096 atlas, 64 MB as RGBA on the CPU and as much on the GPU, next to the base font. The DX11 limit per texture side is
16384 (`D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION`).

**Decision**:
- **Same sets as the base font** (FR-010): the name font merges the same sets, so the characters the base font shows
  also show in names.
- **The guard**: after `io.Fonts->Build()`, if `TexHeight` or `TexWidth` exceeds 16384, or `Build()` fails, the
  loader rebuilds the base font alone, logs a warning (`spdlog::warn`, every build), and the names use the base font
  until the size changes again. The HUD always keeps working.
- **CPU copy**: the backend keeps the atlas pixels after upload. `io.Fonts->ClearTexData()` is not called, because
  `InvalidateDeviceObjects` → `NewFrame` needs them if the device objects are recreated. That stays as today.

## R7. The setting, its storage and the control

**Decision**:
- **Field**: `StyleConfig::spellNameScale` (`float`, default `1.0`), next to `spellNamePosition` and
  `spellNamePadding` (`include/Config/StyleConfig.h:120`).
- **INI**: `styles.ini` `[HUD]` `SpellNameScale`, read with `GetFloat`, then clamped to [0.5, 2.0] and rounded to the
  nearest 0.05. A missing key means 1.0 (Constitution V). Saved by `StyleConfig::Save` with the other `[HUD]` keys.
- **Why `StyleConfig` and not a `Config/Ports` interface**: every HUD look setting, the spell name ones included,
  lives in `StyleConfig` and is read by `UI/` directly. The Ports cover gameplay config. A new port for one style
  value would split the spell name settings across two places.
- **Control**: in `DrawSettings` (`src/UI/MENU.cpp`), inside the `if (st.showSpellNamesInHud)` block, after "Padding":
  `ImGuiMCP::InputInt("Size (%)##spellnamesize", &pct, 5, 25)`, width 150. The value is rounded to a multiple of 5,
  clamped to [50, 200], written as `pct / 100`, and sets `dirty`.
  - **Why `InputInt`**: the steps are exact (5% and 25%), and typing a value works. The section already uses
    `InputInt` with steps (`MENU.cpp:606`).
  - **Hidden when names are off**: like "Position" and "Padding" (spec Edge Cases).
- **Label**: `HUD_SpellNameSize=Size (%)##spellnamesize` in `SKSE/Plugins/IntegratedMagic_Strings.txt`, after
  `HUD_SpellNamePadding`, and the same English default in `S::Get` (FR-011).

## R8. The fallback font

**Findings**: with no font file, `LoadFontsFromConfig` adds `AddFontDefault()` (ProggyClean, 13 px) and nothing else.

**Decision**: the name font is then `AddFontDefault` with `ImFontConfig::SizePixels = 13 × scale`. ProggyClean is a
bitmap-style font, so sizes that are not whole multiples look rougher, but it stays readable, and the game does not
fail (spec Edge Cases).

## R9. Logs, docs and tests

**Decision**:
- **Debug log** (`MAGIC_DEBUG_LOG`): `[HUD] FontLoader: atlas built (base=<px> px, names=<px|none>, scale=<s>) <w>x<h>
  in <ms> ms` after each build, init included. The existing "font not found" line stays.
- **Always-on**: `[HUD] FontLoader: spell name font too large (<w>x<h>), names use the base font` (`spdlog::warn`).
- **CLAUDE.md**: the HUD / UI section gets a "Spell name size" paragraph: the setting, the name font, the rebuild
  point, and the push/pop and full-screen clip rules.
- **TEST_MATRIX**: section 10 gains 10.23–10.27 (quickstart §5).
