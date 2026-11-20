# Contract: the player's attack/block buttons during plugin casts

This contract defines when the player's own attack/block buttons reach the game while the plugin drives a hand, what
their presses do, when a cast attempt gives up, and how an interrupted cast ends the slot. It is the reference for the
TEST_MATRIX cases (FR-010) and the `.claude/CLAUDE.md` Input system section.

## 1. Terms

- **Hand action**: the game action `"Left Attack/Block"` (left hand) or `"Right Attack/Block"` (right hand),
  identified by the event's user event. That covers the mouse buttons, gamepad `LT`/`RT`, keyboard binds and remaps.
- **Plugin-cast hand**: the session is active and the hand is `!finished && (autoActive || (holdActive &&
  wantAutoAttack))`. That is Automatic, Hold with auto-attack, and Press with auto-attack until its first cast ends.
- **Other hand**: a hand that is not plugin-cast while the other one is, for example a shield during a right-hand slot.
- **May cancel**: a hand is plugin-cast and the session is not in Press mode, the same rule as the game's `blockStart`.
- **Gated event**: an event of a hand action that would reach the game:
  - it passed spec 004's combo rules and the HUD toggle filter,
  - or it is a replay about to be injected.

## 2. Gate (FR-001, FR-002, FR-003, FR-006, FR-007, FR-011)

| Gated event | State | Outcome | Requirement |
|---|---|---|---|
| Fresh press of a plugin-cast hand | may cancel | Kept from the game; the slot is cancelled with a full gear restore | FR-001, Q1 |
| Fresh press of a plugin-cast hand | Press mode | Kept from the game; no effect | FR-001, Q1 |
| Fresh press of the other hand | may cancel | **Reaches the game** (block or attack at once); the slot is cancelled with a full gear restore | FR-011, Q3 |
| Fresh press of the other hand | Press mode | Reaches the game; the press itself does not cancel the slot. If it interrupts a cast in progress, §4 "Interrupted cast" applies | FR-011, FR-012, Q3 |
| Held repeat or release of a kept press | any, during or after the session | Kept from the game until the release | FR-003 |
| Held repeat or release, press not kept, the game sees it held | plugin-cast hand, before the plugin's first press | Reaches the game (it completes the player's own press) | FR-002 |
| Held repeat or release, press not kept, the game does not see it held | plugin-cast hand | Kept from the game | FR-001 |
| Any | no hand plugin-cast, nothing kept | Reaches the game (vanilla) | FR-006 |

- **A fresh press always starts over**: a press kept earlier whose release was missed never blocks it.
- **Combo presses are never gated as block or attack presses**: spec 004 decides them first. For example `RT` as the
  modifier, or `RT + X` to start another slot during a session.
- **The game's `blockStart` / `BashExit` rule is kept**: it still cancels Hold and Automatic sessions, for blocks that
  do not come from the gated buttons, such as a mod's own block key.

## 3. Taking held buttons at the plugin's first press (FR-002, FR-004)

When the plugin sends its first press for a hand (on equip, or on a restart), then for **each** hand whose action the
game sees held by the player:

1. a release of that action is sent to the game first, in the same frame, before the plugin's press,
2. the player's press is then kept from the game until it is released (§2 row 5),
3. in dual cast, both hands still go down in the same frame (Constitution IV).

Both hands are checked: a shield held up (the left action) keeps the game from casting a right-hand spell.

After the session, a taken button that is still held does nothing until the player presses it again.

## 4. Session ends (FR-005, FR-012)

- **Retry bound (Q2)**:
  - A plugin-cast hand whose cast attempt has not reached a stable cast within **1.0 s** ends the session through the
    normal forced exit: every synthetic input is released and the gear is restored. In dual cast, both hands end
    together.
  - What does not reset the 1.0 s: restarts, interruptions before the cast, and short (spurious) casts. A new attempt
    resets it, and so does a cast that lasts 0.2 s.
- **Interrupted cast**:
  - When the game interrupts a plugin cast that had started (longer than 0.2 s), that hand is finished (both hands in
    dual cast).
  - The session ends through the spell-fire finalize, within at most 0.7 s once the caster is idle, and the gear is
    restored.
  - The slot never stays active until the safety timeout or the hotkey release.

## 5. Unchanged

- **Require exclusive hotkey** (Q4): a held attack/block button is an extra key, so with the patch ON a slot cannot be
  activated while blocking (`REFUSED (not exclusive) extra=mouse:1` / `extra=gp:<n>`).
- Hotkey combos, spec 004 shared-button rules, replay, wheel handling, the HUD popup filter, capture mode and menu
  blocking.
- Manual casting with the button when the plugin does not drive the hand: no session, auto-attack off, or Press after
  its first auto-cast.
- No INI or setting changes.

## 6. Diagnostic lines (FR-009, Constitution VI)

`MAGIC_DEBUG_LOG` lines. The release build also writes them (`PCH.h` defines `DEBUG`). None is written for per-frame
held repeats.

```text
[Input] HandGate: <Left|Right> press during plugin cast -> cancel session
[Input] HandGate: <Left|Right> press during plugin cast -> ignored (Press mode)
[Input] HandGate: <Left|Right> other-hand press -> passes, cancel session
[Input] HandGate: <Left|Right> button taken at cast start -> released in game (held=<s>s)
[Input] HandGate: <Left|Right> button released -> gate cleared
[SpellSystem] HandGate cancel -> ForceExit
[FLOW] PumpAutomatic: hand=<L|R> cast did not start in <s>s -> ForceExit
[State] OnCasterInterrupt: hand=<Left|Right> real interrupt -> finish + finalize session
```
