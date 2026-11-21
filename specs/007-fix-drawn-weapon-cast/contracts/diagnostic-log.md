# Contract: diagnostic log of the hands (spec FR-008, research R2)

These lines are the interface a future bug report is read through, so their format is stable. They are written with
`MAGIC_DEBUG_LOG`, into `%LOCALAPPDATA%\Skyrim Special Edition\SKSE\IntegratedMagic.log`. That holds on the release
build too, while `include/PCH.h` defines `DEBUG`.

## §1. The hands description

`DescribeHandsForLog(player)` returns:

```text
ws=<weapon state> L=<item> anim=<n|n/a> eq=<IsEquipping>/<IsUnequipping> ok=<bEquipOk> blk=<IsBlocking> atk=<IsAttacking> cast=<caster state>:<spell id> | R=<same fields>
```

| Field | Values |
|---|---|
| `ws` | `RE::WEAPON_STATE` as a number: 0 sheathed, 1 want to draw, 2 drawing, 3 drawn, 4 want to sheathe, 5 sheathing |
| `<item>` | `spell:<id>`, `1h:<id>`, `2h:<id>`, `bow:<id>`, `xbow:<id>`, `staff:<id>`, `shield:<id>`, `torch:<id>`, `empty` (IDs as `0x%08X`) |
| `anim` | the graph variable `iLeftHandType` / `iRightHandType`, or `n/a` |
| `eq`, `ok`, `blk`, `atk` | `0`, `1`, or `n/a` when the graph has no such variable |
| `cast` | the hand's `MagicCaster::State` as a number, and `currentSpell` (`0x00000000` for none) |

Rules:
- A missing graph variable prints `n/a`. It is never an error, and never skips the line.
- No null player: the caller logs `player=null` instead.
- Read-only: the helper never changes game state (Constitution I).

## §2. Where the description is logged

| Prefix and text | When |
|---|---|
| `[State] Activation hands: <description> slot=<n>` | `EnsureActiveWithSnapshot`, after `CaptureSnapshot`, on a new session only |
| `[SpellSystem] Press hands: hand=<L\|R> <description>` | `DispatchAttackPress`, on every fresh plugin press of a hand. This covers the first press and every restart, but not held repeats |
| `[SpellSystem] Restore hands (before): <description>` | `ExecuteRestoreSnapshotPlan`, before the first equip call |
| `[SpellSystem] Restore hands (after): <description>` | `ExecuteRestoreSnapshotPlan`, after the last equip call. Queued equips may not show yet |

## §3. Dropped synthetic input

`FireAttackDirect` (`src/Adapters/Outbound/SyntheticInput.cpp`) logs a press or release (`value` 1.0 with `heldSecs`
0, or `value` 0.0) that it does not deliver:

```text
[Action] synthetic attack dropped: hand=<L|R> value=<v> reason=<no handler|disabled|cannot process>
```

Held repeats (`value` 1.0, `heldSecs` > 0) are not logged, even when dropped.

## §4. Volume

The lines are written a few times per activation: one at activation, one per press or restart, two per restore. No
line is written every frame (Constitution frame budget).
