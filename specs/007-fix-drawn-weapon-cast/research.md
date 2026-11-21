# Research: Fix slot casts failing with a weapon or shield in the left hand after drawing weapons

All findings come from the code on `fix/drawn-weapon-cast` (`main` at `f540210`), unless marked as a user test. The
root cause is not known yet. R1 is the gate that decides it, and R3–R7 hold one candidate cause each, with its log
signature and its fix.

## R1. Reproduction gate (FR-007)

**Decision**: the first task reproduces the report on the current code, before any change. Its log picks which of
R3–R7 is real. Only the confirmed causes are fixed. If nothing fails, the bug is closed as already fixed (spec FR-007),
and only R2 and R9 ship.

**Rationale**:
- The current code already logs almost everything needed. `include/PCH.h` defines `DEBUG` unconditionally, so the
  release DLL writes every `MAGIC_DEBUG_LOG` line (see R10).
  - every animation event, as `[SpellSystem] AnimEvent: <tag>` (`SpellSystemController::NotifyAnimEvent`), including
    `blockStart`, attack, equip and draw/sheathe events,
  - foreign-equip decisions, as `[EquipSink] ... -> ForceExitNoRestore` (`EquipEventAdapter.cpp`),
  - `ShouldForceInterrupt` reasons (`player sheathing/sheathed`, `spell mismatch`),
  - the start retries (`StartRequested for ... → UP`), the 1 s bound (`cast did not start in`) and interruptions,
  - the weapon state at activation (`EnsureActiveWithSnapshot: ACTIVATING ... weaponState=`).
- The release DLL in MO2 now is the spec 006 build. It matches `build\release\IntegratedMagic.dll` and the code of
  `main` (`f540210`), so the reproduction needs no new build.
- What the current log does not show is what each hand holds and what the game's animations believe it holds. R2 adds
  that. R4 (H2) cannot be confirmed without it.

**Protocol** (details in [quickstart.md](quickstart.md) §1):
- **Setups**:
  - **A**: two different one-handed weapons.
  - **A'**: two copies of the same weapon (see R8).
  - **B**: a sword in the right hand and a shield in the left.
- **Slots**: a left-hand, a right-hand and a dual slot, all in Automatic.
- **Patches**: Skip Equip Animation and Skip Equip Animation on Return OFF, then both ON.
- **Sequences**:
  - weapons drawn, 10 activations in a row;
  - the reporter's sequence: sheathe → cast → draw → cast ×5.

**Alternatives considered**:
- Fixing all five candidates blindly. Rejected: R4 and R6 change restore and equip-event behavior that works today,
  and three of the five may not be involved.
- Adding R2's logging before the first reproduction: **accepted for tasks**, so a single run gives the full log.
  - R2 only adds log lines, so a build with it behaves exactly like `main`, and FR-007's "current build" still holds.
  - A log the user already took with the installed DLL also counts.

## R2. Diagnostic log of the hands (FR-008), always shipped

**Decision**: one shared helper describes the game's view of both hands in one line. It is logged at three points.

- **Helper**: `include/Shared/HandDiagnostics.h`, `src/Shared/HandDiagnostics.cpp`, `DescribeHandsForLog(player)`.
  For each hand it reports:
  - what the hand holds, from the equipped object: `spell`, `1h`, `2h`, `bow`, `xbow`, `staff`, `shield`, `torch`,
    `empty`, with the form ID;
  - the animation hand type, from the graph variables `iLeftHandType` / `iRightHandType`;
  - the graph flags `IsEquipping`, `IsUnequipping`, `bEquipOk`, `IsBlocking` and `IsAttacking`;
  - the magic caster's state and current spell.
  It also reports the weapon state. A variable the graph does not have prints `n/a`, so a missing name never fails.
- **Where it is logged** (format in [contracts/diagnostic-log.md](contracts/diagnostic-log.md)):
  1. **Slot activation**: `MagicState::EnsureActiveWithSnapshot`, after the snapshot and before any equip.
  2. **Every fresh plugin press** of a hand: `SpellSystemController::DispatchAttackPress`, and the deferred restarts
     through it. Held repeats are not logged, because `PumpAutoAttack` resends them every frame.
  3. **Restore**: `SpellSystemController::ExecuteRestoreSnapshotPlan`, before and after the equips are issued.
- **Silent drops**: `SyntheticInput.cpp::FireAttackDirect` returns silently when the attack handler is disabled or
  refuses the event. It now logs `[Action] synthetic attack dropped: hand=<L|R> value=<v> reason=<disabled|cannot
  process>`, once per press or release, not for held repeats.

**Rationale**:
- H2 (R4) is only visible as a mismatch between what a hand holds and what the animations believe.
- The silent drop is a direct H1 signature (R3) that the log cannot show today.
- A future report can then be diagnosed from the log alone (spec FR-008).

**Constitution**:
- **I**: the helper only reads game state, as `CasterUtil` and `InventoryUtil` do. `Domain/` calls it only to log.
- **VI**: every new line has a bracketed prefix.
- **Frame budget**: the three points run a few times per activation, not every frame.

**Alternatives considered**:
- Logging every dispatch. Rejected: about 60 lines a second per held hand.
- Only logging the equipped objects. Rejected: H2 is about the animation state, not the equipment.

## R3. H1: the cast press reaches the game before it switched the hand to magic

> **Outcome (T009)**: D1 was not built. The reproduction found a related timing instead, fixed by D6. See R11.

**Facts (code)**:
- **Drawn path**: with weapons drawn, `wasHandsDown` is false and `needsSkipEquipVars` is false, so
  `DispatchSlotEvents` does not raise the hands and sets no instant-equip variable. The exception is Skip Equip
  Animation ON, through `EquipSpellInHand`.
- **Same-frame press**: `EquipSpellInHand` swaps the hand's item for the spell. Then `OnEquipComplete` runs in the
  same loop and `DispatchAttackPress` queues the press. `FlushSyntheticInput` sends it to the game's attack/block
  handler at the next input poll, about one frame after the equip.
- **What the press means**: the handler turns it into the left hand's current action. With a shield, or one weapon
  with another in the right, that is block. With two weapons, it is a left swing.
- **Block** raises `blockStart`, and `NotifyAnimEvent` runs `ForceExit` (Hold/Automatic): the slot ends at once.
- **Swing**: the caster never reaches its charging state. The start retries every 0.15 s, and the spec 005 bound ends
  the session after 1 s.
- **Fits the report**: spells in both hands, or a spell already in the left hand, need no switch to magic and always
  work. Sheathed activations equip while the weapons are away, so the draw goes straight into magic.
- **Does not explain on its own**: why the first cast after a draw works. See R4.

**Log signature**:
- `AnimEvent: blockStart` or an attack event right after `DispatchAttack Left 1.0 (equip)`,
- then `ForceExit`, or `cast did not start in ... -> ForceExit`,
- R2's `synthetic attack dropped` lines, or an animation hand type that is not magic at the press.

**Decision if confirmed (D1)**: delay the first plugin press of a switching hand until the game shows the hand as
magic. A switching hand is one whose snapshot item was not a spell, in an activation with weapons drawn.
- **New wait**: `HandMode::waitingHandSwitch` and `handSwitchWaitSecs`. Contract in
  [contracts/hand-switch-wait.md](contracts/hand-switch-wait.md), states in [data-model.md](data-model.md).
- **`OnEquipComplete`**: does not request the start for a waiting hand.
- **`PumpCastPhase`**: each frame, it ends the wait when the hand is ready or after `kMaxHandSwitchWaitSecs`, then
  calls `RequestAutoAttackStart` and returns `startAttack`. The 1 s bound starts there, because
  `RequestAutoAttackStart` resets `startFailSecs`.
- **Ready signal**: R1 picks the earliest reliable one, in this order:
  1. the graph hand type is magic,
  2. the hand's `M?h_Equipped_Event` animation event,
  3. the caster holds the slot spell.
  The domain reads it through the R2 helper (read-only, Constitution I).
- **Dual cast**: the wait ends for both hands in the same frame, once both are ready or the timeout passes, so both go
  DOWN together (Constitution IV).
- **Timeout**: `kMaxHandSwitchWaitSecs` = 0.5 s. It is longer than the game's hand-switch animation with Skip Equip
  Animation OFF, and R1 measures that. A missing signal therefore costs at most 0.5 s and then behaves as today
  (Constitution III).

**Alternatives considered**:
- Sheathing and redrawing to cast. Forbidden by spec FR-003.
- Ignoring `blockStart` during the start. Rejected: it only covers the shield, the game would still block, and a
  real block by the player must still end the session (spec 005).
- A fixed delay with no ready signal. Kept only as the last fallback.

## R4. H2: the restore while drawn leaves the hand's animation state stale

**Facts (code)**:
- **Restore while drawn**: `ExitAllNow` restores at once when the activation started drawn (`wasHandsDown` false).
  `RestoreOneHand` re-equips the weapon or shield with the queue flag (`EquipObject(..., queueEquip=true, ...)`), with
  no sheathe and no draw. The game applies it later, possibly while the cast's release animation is still playing.
- **The report's pattern**: a draw by the player resets things, and only the plugin's restore while drawn precedes
  the failing casts. Only this candidate explains it directly.

**Log signature** (needs R2): at the next activation, a hand holds a shield or weapon while its animation hand type is
still magic, or the reverse. Or `IsEquipping` / `bEquipOk` stay stuck between activations.

**Decision if confirmed (D2)**: make the restore while drawn leave a consistent hand. R1's log chooses between two
candidates, in this order:
1. **Unqueued equip**: re-equip the hand item without the queue flag when the weapons are drawn, so the game applies
   it before the next frame.
2. **Ready gate**: at the next activation, D1's wait also covers a hand whose animation state disagrees with its item.
   It times out after 0.5 s.

If neither works in game, the finding is recorded and the spec escalates to the user before anything else is tried.
A sheathe and redraw is not an option (FR-003).

**Alternatives considered**: restoring through a sheathe and redraw (the reporter's workaround). Rejected by FR-003.

## R5. H3: the "wrong spell in the hand" check fires on a stale caster

**Facts (code)**:
- `ShouldForceInterrupt` runs every frame of a session (`PumpAutomatic`). It ends the session when the hand's caster
  `currentSpell` is a spell other than the slot's (`CasterSpellMismatch`).
- A caster still holding the previous session's spell, before the new equip reaches it, would trip it.
- It needs a slot spell that differs from the hand's last spell, so it cannot explain a failure with the same slot
  repeated. That makes it the least likely candidate.

**Log signature**: `ShouldForceInterrupt: TRUE - Left spell mismatch` right after activation.

**Decision if confirmed (D3)**: skip the mismatch check while the hand is in its start phase (`StartRequested`, or
D1's wait). A real mismatch is still caught once the cast starts, and the 1 s bound covers the start. No new state.

## R6. H4: a late equip event of the plugin's own restore is read as a foreign equip

**Facts (code)**:
- During a session, outside slot setup, `MagicEquipSink` ends the session with `ForceExitNoRestore` in two cases:
  - a shield is equipped while the slot has a left spell;
  - a weapon lands in a hand the slot uses.
- The restore's equips are queued. If one is still pending when the player activates the next slot, its event
  arrives during the new session and looks foreign. The session then ends without restoring, and the cast is lost.
- **Report**: the report does not mention lost gear. That makes this candidate less likely, but its symptom is exactly
  "the cast fails".

**Log signature**: `[EquipSink] shield ... conflicts -> ForceExitNoRestore` or `weapon/misc ... conflicts`, with the
form ID of the session's own snapshot item.

**Decision if confirmed (D4)**: the sink ignores a weapon or shield equip whose form is the active session's own
snapshot item for that hand.
- It is never foreign: the restore would put the same item back anyway.
- `GetActiveSlotContents` gains the snapshot form IDs.

## R7. H5: the weapon state passes through "sheathing" during the drawn swap

**Facts (code)**:
- `ShouldForceInterrupt` ends a session started drawn when the weapon state is `kWantToSheathe`, `kSheathing` or
  `kSheathed`. Only a left-only activation started sheathed is exempt.
- If swapping a weapon or shield for a spell while drawn makes the game put the old item away first, the state passes
  through `kWantToSheathe` / `kSheathing`, and the plugin reads that as the player sheathing.
- It fits the setup pattern: a spell-to-spell swap puts nothing away.

**Log signature**: `ShouldForceInterrupt: TRUE - player sheathing/sheathed weaponState=<4|5|0>` (want to sheathe,
sheathing, sheathed) within the first second of a drawn activation.

**Decision if confirmed (D5)**: while any hand of a drawn activation is in its start phase (D1's wait or
`StartRequested`), the sheathing rule does not apply.
- A player who really sheathes during the start is still covered by the 1 s bound.
- After the start, the rule applies again.
- No new state.

## R8. Two copies of the same weapon (restore check)

**Facts (code)**:
- `RestoreOneHand` picks the item instance by `ResolveLiveExtra`, then falls back to `FindAnyInstanceExtraForBase`,
  the first instance of that base.
- With two copies of the same sword, the fallback can pick the instance worn in the right hand and move it to the left
  hand.
- The right hand's restore (`restoreRightAfterLeftOnly`) then sees the same base still "there" (the equip is queued),
  so it does nothing. The right hand could end up empty.
- This is not the reported symptom, but setup A' is a common way to "dual-wield two swords".

**Decision**: R1 includes setup A' and checks that both hands get their sword back.
- **If one is lost**: the fix is in scope (spec FR-002): pick the instance that is not worn in the other hand.
- **If not**: TEST_MATRIX 7.1 gains the A' variant as a regression guard.

## R9. TEST_MATRIX cases (FR-009)

**Decision**: new cases in section 7 (Snapshot / Restore), in Portuguese and in the file's style:
- **7.11–7.13**: weapons drawn, 10 activations in a row: setups A, A' and B, each with a left-hand, a right-hand and a
  dual slot (Automatic).
- **7.14**: the reporter's sequence, sheathe → cast → draw → cast ×5, with setups A and B.
- **7.15**: right after the gear comes back, one attack and one block (setup B) work on the first press.
- **7.16**: Hold and Press, 5 uses each, setups A and B, weapons drawn.

**Regression**: 1.5, 1.6, 7.1, 7.2, 7.6, 7.7, 7.10, 8.9–8.19 and section 1 (dual cast), as Constitution IV requires
for changes to `PumpCastPhase`.

## R10. Validation build (Constitution VI)

**Facts**:
- The DEBUG DLL faults during start-up, inside `Hooks::Install_Hooks`. It is a DEBUG-only defect, unrelated to this
  spec. See the project memory note on DLL load failures.
- `include/PCH.h` defines `DEBUG` unconditionally, so the release DLL writes the same `MAGIC_DEBUG_LOG` lines.

**Decision**:
- Reproduction (R1) and validation run on the release DLL, approved by the user for this spec (2026-11-20, spec
  Clarifications). That is the Complexity Tracking row in the plan.
- The DEBUG crash stays out of scope.

## R11. Reproduction outcome and the fix (T008/T009, 2026-11-21)

**Runs** (release DLL with R2's logging, third person; `tasks.md` implementation log):
- **Run 1**: a sword and a shield, patches ON, 12 activations (left-hand, right-hand and dual slots, the sequence).
  All cast.
- **Run 2**: a sword (right) and an axe (left, `0x00013790`).
  - **Right-hand slot**: patches ON and OFF, drawn and sheathed, including during the draw. All cast.
  - **Left-hand slot, patches ON**: all cast, including during the draw.
  - **Left-hand slot, patches OFF**: activated after the draw animation, it cast. Activated during it, it failed (4 of
    4), which is the user's report.

**What the failing activations show** (patches OFF, left hand, `IsEquipping=1` at activation):
1. The player draws (`BeginWeaponDraw`). The full draw animation lasts about 1.42 s, up to `WeapEquip_Out`.
2. The slot is pressed 0.5–0.9 s into it, so `Activation hands:` shows `eq=1/0`, with `ws=2` or `ws=3`.
3. Equipping the spell restarts the draw: `InterruptCast` and two `BeginWeaponDraw` events, then `weaponDraw` after
   about 0.57 s.
4. The plugin keeps pressing: the first press is spurious-interrupted, then it restarts every 0.15 s. Every
   `Press hands:` line shows `anim=9` and `eq=1/0`.
5. `cast did not start in 1.0Xs -> ForceExit` arrives before the restarted animation ends.
6. The restore puts the axe back, which plays the full draw animation once more. That is the "restarted animation"
   the user sees.

**What a working activation shows** (same setup, after the draw, `eq=0/0` at activation):
- **Short re-draw**: equipping the spell starts a short left-hand re-draw, about 0.5 s.
- **Cast start**: `BeginCastLeft` comes about 17 ms after its `WeapEquip_Out`, 0.56 s after activation, inside the
  1 s bound.
- **The rule**: a left-hand cast begins only once the equip animation ends.
- **The right hand** has no such rule. Right-hand slots activated mid-draw began their cast while `IsEquipping` was
  still 1.

**Hypotheses**:

| Hypothesis | Outcome |
|---|---|
| **H1** (press before the hand switched) | Present on every drawn activation, but already recovered by the spurious restart: the second press shows `anim=9`. Not the failure. The failure is a related timing: the press is fine, but the left cast waits for the game's equip animation, which the 1 s bound does not allow for |
| **H2** (stale state after a restore) | Not seen. After each restore, the next activation shows the item's own `anim` value and casts |
| **H3**, **H4**, **H5** | Not seen. No spell-mismatch exit, no foreign-equip exit. The two `sheathing/sheathed` exits at the end of run 2 follow a `BeginWeaponSheathe` 0.13–0.23 s after the cast began. The plugin did not request it, and the gear came back. Most likely the player's own sheathe; to be confirmed with the user |
| **R8** (two copies) | Not tested. Setup A' was not run |

**Decision (D6, replaces D1)**:
- **Pause**: while a drawn activation's hand is in `StartRequested` and the game reports `IsEquipping`, the frame time
  goes to `HandMode::equipAnimGraceSecs` instead of `startFailSecs`.
- **Cap**: the pause lasts at most `kMaxEquipAnimGraceSecs` = 2.0 s per attempt, against a measured full draw of
  1.42 s. After that the bound counts again, so a stuck graph variable ends the session within 3 s (Constitution III).
- **Presses**: they are unchanged. The existing restart loop already starts the cast on the frame after the
  animation ends, as the working activation shows.
- **Dual cast**: `IsEquipping` is one variable for the whole player, so both hands pause and resume together
  (Constitution IV).
- **Scope**: drawn activations only (`!wasHandsDown`). The sheathed path is unchanged (FR-004).
- **Logs**: `[FLOW] EquipAnimGrace: hand=<L|R> equip animation playing -> retry bound paused` and
  `... retry bound resumes after <s>s (animation done|cap reached)`.
- **Resets**: in the same places as `startFailSecs` (`RequestAutoAttackStart`, `FinishHand`, a cast past the spurious
  window), and in every `HandMode` value reset.
- **Read**: the domain reads the variable through `IsEquipAnimationPlaying` in `Shared/HandDiagnostics`.

**Alternatives considered**:
- **D1 as designed** (no press until the hand shows as magic): rejected. The hand shows `anim=9` within one frame of
  the equip, long before the cast can begin, so it would not have helped.
- **D1 gated on `IsEquipping`** (no press until the animation ends): it would also work. It was rejected because the
  presses during the animation are harmless (the working activation sends them too), and this way the existing,
  validated press loop stays as it is.
- **Raising `kMaxCastStartSecs`** for every activation: rejected. It would lengthen spec 005's bound for real
  failures too, such as TEST_MATRIX 8.18 (jump), where nothing animates.
- **Turning on `InstantEquipAnim` for drawn activations**: rejected. It forces the Skip Equip Animation behavior on
  players who turned it off (Constitution V).
