# Contract: The spell name size

**Spec**: [../spec.md](../spec.md) | **Research**: [../research.md](../research.md) |
**Data model**: [../data-model.md](../data-model.md)

## 1. `styles.ini`

```ini
[HUD]
SpellNameScale = 1.0
```

- **Values**: a decimal number. Read values are rounded to the nearest 0.05 and clamped to [0.5, 2.0].
- **Missing**: 1.0, which is today's look.
- **Written**: by "Apply changes", with the other `[HUD]` keys.
- **Shipped file**: `SKSE/Plugins/IntegratedMagics/styles.ini` gets `SpellNameScale = 1.0` after `SpellNamePadding`,
  so the key is visible to players who edit the file.

## 2. The SKSE menu control

- **Where**: the HUD section, inside "Show spell names", after "Padding". Hidden while "Show spell names" is off.
- **Control**: `InputInt`, label `HUD_SpellNameSize` (default `Size (%)##spellnamesize`), width 150, step 5, fast step
  25.
- **Shows**: the scale as a whole percentage (100 = today).
- **On edit**: round to a multiple of 5, clamp to [50, 200], store `pct / 100` in `spellNameScale`, mark the page
  dirty ("Apply changes" becomes enabled).
- **Effect**: the HUD shows the new size about 0.25 s after the last edit, before "Apply changes". "Apply changes"
  saves it.

## 3. Fonts in the HUD context

| Font | Size | Character sets | Used for |
|---|---|---|---|
| Base font (atlas default, added first) | `[Font]` `Size` (or 13, fallback) | as today | all HUD and popup text except spell names |
| Name font (only when the scale is not 100% and names are on) | base × scale | the same as the base font | spell names in the HUD and the popup |

**Rules**:
- **Same base font**: every build adds the base font first with the same parameters, so no other text changes.
- **When the atlas changes**: only in `FontLoader::RebuildIfNeeded()`, called once per frame from the Present thunk
  through `HudController::PrepareFonts()`, after `ImGui::SetCurrentContext` and before `ImGui_ImplDX11_NewFrame`.
  Never between `NewFrame` and `Render`.
- **No file I/O after init**: fonts are added from the buffer read at init (`AddFontFromMemoryTTF`,
  `FontDataOwnedByAtlas = false`).
- **Upload**: a build calls `ImGui_ImplDX11_InvalidateDeviceObjects()`. The next `ImGui_ImplDX11_NewFrame` creates
  the device objects and uploads the atlas.
- **The size guard**: an atlas side above 16384 texels, or a failed `Build()`, makes the loader build the base font
  alone. Names then use the base font.

## 4. Drawing spell names

- **`FontLoader::SpellNameFont()`**: the name font, or `nullptr` (= the default font for `ImGui::PushFont`).
- **Push/pop pair**: `ImGui::PushFont(FontLoader::SpellNameFont())` and `ImGui::PopFont()` are an unconditional pair
  with no `return` between them, around:
  - the measuring, wrapping and drawing in `HUD::DrawSpellLabel` (after its early returns);
  - the name reserve in `SlotDrawer` (`textHeightReserve`, `CalcMaxLabelWidth`);
  - the name reserve in `PopupDrawer` (`textReserve`).
- **No clipping by the window**: `DrawSpellLabel` draws between `dl->PushClipRectFullScreen()` and
  `dl->PopClipRect()`.
- **Unchanged**: the wrap width (slot diameter), the anchor (slot radius + padding + the hotkey icon reserve), the
  six positions, the text color and shadow, and the "hidden during a session" rule.

## 5. Logs

| Build | Level | Line |
|---|---|---|
| debug | `MAGIC_DEBUG_LOG` | `[HUD] FontLoader: atlas built (base=<px> px, names=<px> px\|none, scale=<s>) <w>x<h> in <ms> ms` |
| all | `spdlog::warn` | `[HUD] FontLoader: spell name font too large (<w>x<h>), names use the base font` |
| all | `spdlog::warn` | `[HUD] FontLoader: atlas build failed (names=<px> px), names use the base font` |

The existing `[HUD] FontLoader::LoadFontsFromConfig: font not found, using default` stays (debug).
