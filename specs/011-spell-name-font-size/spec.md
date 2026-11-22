# Feature Specification: A configurable font size for the spell names

**Feature Branch**: `feat/spell-name-font-size` (created from `fix/debug-build-define` at `6805b8b`, because in-game
validation needs the spec 010 debug build; moved onto `main` at `83598b7` after spec 010 merged as #79, with the same
tree)

**Created**: 2026-11-22

**Status**: Implemented, both builds with 0 warnings. In-game validation (quickstart §1–§4, TEST_MATRIX
10.23–10.27) is open; see the implementation log in `tasks.md`.

**Input**: User description: "Tamanho de fonte configurável para os nomes das magias
@.claude/tasks/feature-04-tamanho-fonte-nomes.md". The task file quotes one player report: "Is there a way to
increase the font size when you display the spell titles?"

## Background & Investigation Findings *(context)*

Source: a review of `fix/debug-build-define` at `6805b8b` (the same tree as `main` at `83598b7`).

1. **Where the names appear**: with "Show spell names" on, each slot shows the name of what it holds, in two places.
   - **The HUD**: next to each slot circle.
   - **The assignment popup**: next to each slot circle, in the Magic Menu.
   - **Same text in both**: the slot's spell, both spells joined by ` | `, or its shout, power or two-handed spell.
2. **The name settings today**: "Show spell names", "Position" and "Padding", in the SKSE menu's HUD section. They are
   saved to `styles.ini` (`[HUD]` `ShowSpellNames`, `SpellNamePosition`, `SpellNamePadding`) by "Apply changes". Each
   applies to both places.
3. **What sets the name size today**: one font, loaded once when the game's graphics start.
   - **The setting**: `styles.ini` `[Font]` `Size`, 28 by default. The shipped `styles.ini` has no `Size` key, so 28 is
     used.
   - **What else uses that font**: all of the plugin's own on-screen text, such as the popup's tooltips and labels.
   - **Where it can be changed**: only by editing `styles.ini` and restarting the game. The SKSE menu has no control
     for it.
   - **The font**: FuturaStd-Book, with optional extra character sets (Polish, Cyrillic, Japanese, Chinese, Korean,
     Greek).
4. **Why enlarging is not just a scale**: the font is prepared as images at one size. Drawing it larger stretches those
   images, so the letters get blurry. Sharp larger text needs the font prepared at the larger size.
5. **Space around the names**: the HUD reserves room for the names from the text height, for two lines. A name wider
   than its slot wraps onto more lines.

## Clarifications

None asked. The choices with a reasonable default are recorded in Assumptions.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The player makes the spell names bigger or smaller (Priority: P1)

The player finds the names hard to read, so they open the SKSE menu, go to the HUD section and change the spell name
size. The names in the HUD take the new size, and keep it the next time the game starts.

**Why this priority**: this is the request. Today the size can only be changed by editing a file, it changes all of the
plugin's text, and it needs a restart.

**Independent Test**: turn on "Show spell names", set a larger size, apply, look at the HUD, restart the game and look
again.

**Acceptance Scenarios**:

1. **Given** "Show spell names" is on, **When** the player sets a larger size and applies, **Then** the HUD names are
   drawn larger, without restarting the game.
2. **Given** the same, **When** the player sets a smaller size, **Then** the names are drawn smaller.
3. **Given** a size was applied, **When** the game restarts, **Then** the names have that size.
4. **Given** the player changes the size, **When** they look at the rest of the plugin's text (the popup's tooltips and
   labels), **Then** it keeps its size.
5. **Given** the player never touches the setting, **When** they update the plugin, **Then** the names look exactly as
   before.

---

### User Story 2 - Large names stay sharp and in place (Priority: P1)

At every size the setting allows, the names are crisp, are not cut off by the HUD's edges, and keep their position
relative to the slots.

**Why this priority**: a larger but blurry name, or one that is cut off, does not help a player who cannot read the
names.

**Independent Test**: set the largest size, look at the HUD with 1-slot names and long 2-spell names, in each name
position.

**Acceptance Scenarios**:

1. **Given** the largest size, **When** the names are drawn, **Then** the letters are as sharp as at the default size.
2. **Given** the largest size and a long name, **When** it wraps, **Then** every line shows in full, inside the HUD.
3. **Given** any size, **When** the player picks each name position (Top, Right, Bottom, Left, Toward Center, Away
   from Center), **Then** the names sit at that position and do not cover the slot circle or its hotkey icons.
4. **Given** an extra character set is on (for example Cyrillic), **When** the size changes, **Then** those
   characters still show.

---

### User Story 3 - The popup names follow the same size (Priority: P2)

The names in the Magic Menu's assignment popup use the same size as the HUD names.

**Why this priority**: the other name settings already apply to both places. It is P2 because the popup is used less
often than the HUD.

**Independent Test**: set a larger size, open the Magic Menu and the assignment popup, and look at the names.

**Acceptance Scenarios**:

1. **Given** a larger size, **When** the popup opens, **Then** its names are drawn at that size, sharp and inside the
   popup.
2. **Given** the same, **When** the player assigns spells in the popup, **Then** clicking and hovering work as before.

---

### Edge Cases

- **"Show spell names" off**: the size control may stay visible or be hidden, but changing it has no visible effect
  and nothing else changes.
- **A value outside the allowed range in `styles.ini`** (edited by hand): it is brought into the range, and the game
  does not fail.
- **No size key in `styles.ini`** (every existing install): the default applies, which is today's size.
- **The font file is missing**: the plugin already falls back to a built-in font. The size setting applies to that
  font too, and the game does not fail.
- **Changing the size several times in a row** (for example 10 changes): each one shows, with no slowdown that lasts.
- **The size is changed while a slot is active**: the cast continues normally. Names are hidden during an active
  session today (`spellSystemActive`), and that stays.
- **Large sizes with many slots in a ring or grid**: neighboring names may overlap each other. That is accepted, as it
  is today with long names. Each name must still show in full.
- **The extra character sets at large sizes**: Chinese and Japanese have many characters, so preparing them at a large
  size needs much more memory. The game must not fail, and the HUD must keep working.
- **Other HUD text**: the hotkey icons, the cooldown ring and the mode controls do not change.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The SKSE menu's HUD section MUST offer a spell name size control next to the other spell name settings.
- **FR-002**: The size MUST apply to the spell names in the HUD and in the assignment popup, and to nothing else.
- **FR-003**: The default MUST reproduce today's name size exactly, so an install that never changes it looks the same.
- **FR-004**: The size MUST range from half to twice the default (50% to 200%), in steps of 5%.
- **FR-005**: A size the player applies MUST show without restarting the game, within 1 second.
- **FR-006**: The size MUST be saved with the other HUD settings ("Apply changes", `styles.ini`) and loaded at game
  start. A missing key MUST mean the default, and an out-of-range value MUST be brought into range (Constitution V).
- **FR-007**: The names MUST stay sharp at every allowed size, with no visible blur compared to the default size.
- **FR-008**: The space the HUD and the popup reserve for the names MUST follow the name size, so names are never cut
  off by their edges at any allowed size and position.
- **FR-009**: The name positions and the padding MUST keep their meaning at every size: the same side of the slot, the
  same gap from the circle and its hotkey icons.
- **FR-010**: The extra character sets turned on in `styles.ini` MUST still show at every size.
- **FR-011**: The control's label MUST come from `IntegratedMagic_Strings.txt`, like the other menu labels, with an
  English default.
- **FR-012**: No other setting, saved data, input or cast behavior changes. The existing `[Font]` `Size` key keeps its
  meaning for the rest of the plugin's text.
- **FR-013**: The setting MUST be documented where the HUD settings are (`.claude/CLAUDE.md`, HUD / UI), and
  `docs/TEST_MATRIX.md` section 10 MUST gain cases for it (Constitution VI).

### Key Entities

- **Spell name size**: one HUD setting, saved in `styles.ini` with the other spell name settings. It is relative to the
  base font size, so the default means today's look. It applies to the HUD and the popup.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A player changes the name size from the SKSE menu and sees the result in under 1 second, with no restart,
  in 10 tries out of 10.
- **SC-002**: At the largest size, the names are as sharp as at the default size, checked side by side on screenshots.
- **SC-003**: At the largest size, in each of the 6 name positions, a 2-spell name shows every line in full in the HUD
  and in the popup.
- **SC-004**: With the setting never changed, a screenshot of the HUD names matches one taken before the change, for
  names of 1 or 2 lines. A name line the HUD cut off before now shows (FR-008, research R5).
- **SC-005**: An applied size is still there after a game restart, 3 restarts out of 3.
- **SC-006**: The rest of the plugin's text keeps its size at every name size.
- **SC-007**: 10 size changes in a row leave the frame rate where it was before them.

## Assumptions

- **Unit**: a relative size, shown as a percentage (100% = today's size, 50% to 200%), so it keeps its meaning if
  `[Font]` `Size` is edited.
- **One size for both places**: the HUD and the popup share it, like "Position" and "Padding". A separate popup size
  is out of scope.
- **Scope**: the spell names only. A size control for all of the plugin's text, or for the hotkey icons, is out of
  scope.
- **Live preview**: the HUD stays on screen while the SKSE menu is open (that menu is not in the list of menus that
  hide it), so the player sees the change there. A change takes effect when applied, like the other HUD settings; the
  plan decides whether it also shows before "Apply changes".
- **Sharpness**: met by preparing the font at the name size, not by stretching it. The plan decides how, within the
  per-frame rules (Constitution, Technical Constraints: no file I/O and no blocking work every frame).
- **Validation**: in game on the debug build (spec 010), with screenshots for SC-002 and SC-004.
- **Files likely touched** (task file, corrected against the code): `include/Config/StyleConfig.h`,
  `src/Config/StyleConfig.cpp`, `src/UI/FontLoader.cpp`, `include/UI/HudTextUtil.h`, `src/UI/Slotdrawer.cpp`,
  `src/UI/Popupdrawer.cpp`, `src/UI/MENU.cpp` and `SKSE/Plugins/IntegratedMagic_Strings.txt`. The task file names
  `HudSettings` and `ConfigAdapter`, but the spell name settings live in `StyleConfig`.
