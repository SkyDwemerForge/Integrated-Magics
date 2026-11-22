# Data Model: A configurable font size for the spell names

**Spec**: [spec.md](spec.md) | **Research**: [research.md](research.md)

## 1. The setting: `StyleConfig::spellNameScale`

| Field | Type | Default | Range | Stored as |
|---|---|---|---|---|
| `spellNameScale` | `float` | `1.0` | 0.5 to 2.0, in steps of 0.05 | `styles.ini` `[HUD]` `SpellNameScale` |

- **Where**: `include/Config/StyleConfig.h`, next to `spellNamePosition` and `spellNamePadding`.
- **Constants** (same header): `kSpellNameScaleMin = 0.5f`, `kSpellNameScaleMax = 2.0f`, `kSpellNameScaleStep =
  0.05f`, and a `NormalizeSpellNameScale(float)` helper that rounds to the step and clamps. Load and the menu both use
  it.
- **Validation**:
  - **Load**: a missing key → 1.0. Any read value → `NormalizeSpellNameScale`. A non-numeric value → what `GetFloat`
    returns for it today (the default).
  - **Menu**: the control edits a whole percentage, `pct = round(scale × 100)`. After an edit it is rounded to a
    multiple of 5 and clamped to [50, 200], then `scale = pct / 100`.
- **Meaning**: relative to the base font size, `[Font]` `Size` (28 by default), or 13 for the built-in fallback font.
- **Applies to**: spell names in the HUD and the assignment popup, while `showSpellNamesInHud` is true.
- **Saved**: by "Apply changes" (`StyleConfig::Save`), like every `[HUD]` key. It is live in memory as soon as it is
  edited (preview).

## 2. Font loader state (`src/UI/FontLoader.cpp`, internal)

| State | Meaning |
|---|---|
| `fontData` (`std::vector<unsigned char>`) | The font file, read once at init. Empty when the file is missing (fallback font). |
| `baseSizePx` (`float`) | `[Font]` `Size`, or 13 for the fallback font. Fixed after init. |
| `builtNamePx` (`float`) | The name font size in the current atlas. 0 = no name font. |
| `pendingNamePx` (`float`) / `pendingSince` (`steady_clock::time_point`) | The last wanted size that differs from `builtNamePx`, and since when it has been wanted. |
| `nameFont` (`ImFont*`) | The name font in the current atlas, or `nullptr`. Invalid after any `io.Fonts->Clear()`, so it is reset by every build. |

### Wanted size

```text
wantedNamePx = (showSpellNamesInHud && spellNameScale != 1.0) ? round_to_0.5(baseSizePx × spellNameScale) : 0
```

The comparison with 1.0 uses the normalized value, so 1.0 is exact.

### States

```text
Built(builtNamePx)
  ├─ wanted == builtNamePx                     → stay; pending cleared
  ├─ wanted != builtNamePx, new value          → pendingNamePx = wanted, pendingSince = now
  ├─ wanted == pendingNamePx, now − pendingSince < 0.25 s → stay (settling)
  └─ wanted == pendingNamePx, ≥ 0.25 s         → BuildAtlas(wanted)
        ├─ fits (both sides ≤ 16384)           → Built(wanted), nameFont set
        └─ too large / Build() failed          → BuildAtlas(0), warning → Built(0) with
                                                 builtNamePx = wanted, so it is not retried until the size changes
```

- **Init**: `LoadFontsFromConfig` reads the file, computes the wanted size from the loaded `StyleConfig` and builds
  once, with no settle time, so a saved size is there from the first frame (SC-005).
- **A failed size** is remembered as built, so the loader does not retry it every 0.25 s. A different size, or 100%,
  builds again.

## 3. What is not stored

- No per-slot or per-save data. `SaveSpellDB` and `SpellSettingsDB` do not change.
- No change to `IntegratedMagic.ini` or `MagicConfig`.
