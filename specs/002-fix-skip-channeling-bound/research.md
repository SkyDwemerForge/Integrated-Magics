# Phase 0 Research: Fix Skip Channeling patch looping on bound weapon spells

Evidence comes from the code at `feat/patch-for-spell-channeling-skip` (after `b9bb354`) and from the user's
plugin log of 2026-11-19 15:14–15:16 (`Documents\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log`, 3,763
lines). That session contains three Bound Sword sessions (`0x000211EB`, Press mode with auto-attack): two with Skip
Channeling ON and one with it OFF. It also contains a Candlelight session (`0x00043324`) with Skip Channeling ON.

---

## R1. Root cause of the loop (confirmed from the log)

**Decision**: the loop is caused by the patch's **manual magicka drain**. It is not caused by the bound weapon
being equipped. Bound Sword is affected because it is expensive. Any fire-and-forget spell whose cost is close to the
player's current magicka can hit the same failure.

**Evidence: patch ON, Bound Sword (log lines 2623–2776)**

| Line | Event | Meaning |
|---|---|---|
| 2656 | `StartRequested → Casting (state=2)` | the caster is in `kUnk02` (charging); the patch drains `cost × castingTimer` and zeroes `castingTimer` in this same pump (no log line today) |
| 2658 | `OnCastInterrupted ... depleteEnergy=false`, `castingSecs=0.016` | the **game** cancels the cast on the next frame. No `MRh_SpellFire_Event` and no weapon equip: the summon never happened |
| 2660 | `post-cast spurious → UP (DOWN next frame)` | the interruption is inside the 0.2 s "spurious" window, so the plugin retries |
| 2663–2776 | 24 × `deferred restart`, 22 × `StartRequested for 0.17s → UP`, **1** `BeginCastRight` in total | on every retry the game **refuses to start the cast at all**: the caster never reaches `kUnk02` again |
| 2776 | `active slot pressed again` | the session ends only because the player pressed the slot again (Press mode). In Automatic mode only the 30 s timeout would end it |

The second ON session (2826–3352) shows the same pattern: 79 timeouts and 2 `BeginCastRight`. Each time magicka
regenerated enough, the game charged again, got drained again and cancelled again.

**Evidence: patch OFF, Bound Sword (3352–3420)**: `Casting (state=2)` → `charge complete (skipCh=false)` after
about 0.5 s (`held=0.515`) → `OnCastStarted` → `MRh_SpellFire_Event` → `BeginWeaponDraw` / `WeapEquip_Out`, which is
the weapon appearing in the hand. The flow works.

**Why it points to magicka**:

- `OnSlotPressed` only activates a slot the player can afford (`ComputeSlotAffordability`,
  `src/Domain/MagicStateSlot.cpp:332`), so magicka was sufficient at press time.
- The game cancelled right after the drain and then refused new casts until magicka regenerated. That is the game's
  "not enough magicka" behavior.
- Candlelight is cheap and stayed affordable after the drain, so it completed.
- H2 (the game rejects a zeroed timer specifically for bound spells) would show `BeginCastRight` → interrupt on every
  retry. The log shows the game not starting at all.

**Still to measure (task T015)**: the exact game cost model, meaning whether vanilla deducts fire-and-forget magicka
progressively while charging or once at release. The log has no magicka values. The diagnostic logging in R5 records
them so R2 can be confirmed.

**Correction to the spec**: Background item 4 originally assumed the summon equip caused the interruption. The log
disproves it, and the spec has been updated.

---

## R2. Magicka handling when the charge is skipped

**Decision**:
- Stop draining magicka manually before forcing the charge. The patch only completes the charge, and the game charges
  the spell's normal cost as it does without the patch.
- Decision gate (task T015): compare the magicka spent per cast with the patch OFF and ON.
  - If they match, keep the change.
  - If the game charges less when the charge is skipped (it deducts progressively while charging), drain the
    *undrained remainder* instead. That is `cost × castingTimer / chargeTime`, applied only when the player can afford
    it. If not, do not skip and let the spell charge normally.

**Rationale**:
- The current formula `currentSpellCost × castingTimer` is not normalized by charge time.
- The log shows the drain makes the game cancel the cast, so the extra cost is at least partly double-charged.
- Removing the drain makes the cost exactly vanilla in the at-release model, and slightly lenient in the progressive
  model. The gate catches the second case.
- This also affects other fire-and-forget spells under the patch: they now cost what they cost without the patch.
  The spec's FR-005 was refined to say so.

**Gate result (2026-11-19)**: **under-charge**, about `cost − one frame` per cast. The user reported that spells
cast with the patch ON cost much less, and the plugin log (19:28–19:30, release build) confirms it:

| Spell | cost | chargeTime | castingTimer at skip | magicka at skip | magicka at fire | spent |
|---|---|---|---|---|---|---|
| Bound Sword | 82.7 | 0.50 | 0.500 | 100.0 | 97.2 | 2.8 |
| Candlelight | 18.6 | 0.50 | 0.500 | 48.9 | 48.3 | 0.6 |

- The skip happens on the first charging frame (`castingTimer == chargeTime`), so the game has drained nothing yet.
- The game drains `cost × frame / chargeTime` per frame (82.7 × 0.0167 / 0.5 ≈ 2.8) and still drains the frame in
  which the timer is zeroed.
- Vanilla therefore deducts progressively while charging, not at release.

**Fallback as implemented (T016)**, which changes *where* the remainder is drained compared with the plan above:
- The owed amount is `cost × clamp(castingTimer − frame, 0, chargeTime) / chargeTime`. It is recorded at the skip
  and drained by `MagicAction::DrainMagicka` **after the spell fires** (`OnSpellFired`), not inside
  `ForceChargeComplete`.
- Reason: R1 shows that draining during the charge makes the game cancel the cast, which is exactly the loop being
  fixed. After the spell fires, the player's magicka matches vanilla (`start − cost`), a state the game already
  accepts.
- The "do not skip if not affordable" pre-check is replaced by clamping the drain at 0. Vanilla only starts a
  charge the player can afford in full, so the remainder is always affordable at the skip. The clamp only matters
  if an external drain hits the player between the skip and the fire.
- A failed attempt after the skip discards the owed amount (`FinishHand`), so it costs nothing extra.

**Re-run after T016 (2026-11-19, 19:53–19:54, release build)**: **parity**, confirmed by the user in game.

| Spell | cost | owed | magicka at skip | magicka after `DrainMagicka` | spent |
|---|---|---|---|---|---|
| Candlelight | 18.6 | 18.0 | 100.0 | 81.4 | 18.6 |
| Bound Sword | 82.7 | 80.0 | 100.0 | 17.3 | 82.7 |
| Candlelight | 18.6 | 18.0 | 69.5 | 50.9 | 18.6 |

- `casterCost` (`MagicCaster::currentSpellCost`) equals `CalculateMagickaCost`, so it is the full cost, not a
  per-second rate.
- Coverage: single hand (right). Fireball and Firebolt (TEST_MATRIX 5b.8) and dual cast (T018 / T025) are still
  to be measured.

**Alternatives considered**:
- Keep the drain and pre-check magicka (still double-charges cheap spells and refuses expensive ones).
- Exclude bound weapons from the patch (rejected by the user, Clarification 2026-11-19).
- `CastSpellImmediate` for bound spells (bypasses the charge state machine but changes the session flow; more risk
  for no extra benefit once R2 holds).

---

## R3. Interruption after a forced charge

**Decision**:
- Track per hand whether the patch forced the charge in the current attempt (`HandMode::chargeSkipped`).
- In `OnCasterInterrupt`, an interruption on a hand with `chargeSkipped == true` is a **real failure**, never
  "spurious". The plugin calls `FinishHand` and `ScheduleSpellFireFinalize`, so the session ends through the existing
  0.7 s finalize path and equipment is restored.
- In dual-cast, both hands finish together (Constitution Principle IV).

**Rationale**:
- The 0.2 s spurious window exists for pre-cast noise. A forced charge completes inside that window by design, so
  the window misclassifies every failure after a skip as noise.
- Retrying would repeat the same failure. Ending cleanly satisfies FR-001 and SC-002 (no loop, never stuck) even if a
  future cause other than magicka makes the forced charge fail.
- `FinishHand` alone does not finalize the session: the existing non-spurious branch relies on a later `CastStop`.
  Scheduling the finalize guarantees an exit (Constitution Principle III).

**Alternatives considered**:
- Shrink the spurious window when the patch is on (fragile, timing-dependent).
- Cap the number of restarts globally (wider blast radius on the dual-cast restart logic; not needed once R3 removes
  the trigger).

---

## R4. Where the game writes happen (Constitution Principle I)

**Decision**: move the game mutation (`castingTimer = 0`, plus any drain kept by the R2 gate) out of `Domain/` into
a new outbound adapter `MagicAction::ForceChargeComplete(RE::PlayerCharacter*, Hand)` in
`Adapters/Outbound/MagicEquip.*`.

The domain emits an intent:

- `PumpCastPhaseResult::forceChargeComplete` → `PumpAutomaticResult::forceChargeLeft/Right`.
- `SpellSystemController::OnFrame` executes it right after `PumpAutomatic`, in the same frame as today.

**Rationale**: the current code writes game state from `MagicStatePump.cpp`. Principle I forbids new direct game
mutations in `Domain/`, and modifying this block is the "when touched" moment to migrate it. Timing is unchanged:
the write still happens in the frame where the caster is in `kUnk02`.

**Alternatives considered**: leave the write in `Domain/` and justify it in Complexity Tracking (rejected; the
migration is small).

---

## R5. Diagnostics (FR-009)

**Decision**: add `MAGIC_DEBUG_LOG` lines under `[FLOW] SkipChanneling:`.

- **applied**: `hand`, `spell` (FormID + name), `chargeTime`, `castingTimer` before the change, `cost`, and player
  magicka before.
- **skipped**: one line per cast attempt with the reason (`chargeTime=0`, `not Fire-and-Forget`, `state!=kUnk02`).
- **magicka tracking**: player magicka at `MRh_SpellFire_Event` and on the interrupt after a skip. This lets T003
  compare cost with the patch ON and OFF.

**Rationale**: these lines confirm R1/R2 (Constitution Principle VI). The current code logs nothing when it skips,
which is why the root cause was not visible.

---

## R6. Weapon-appearing visual (FR-011)

**Decision**: no extra work.

**Rationale**:
- The patch only changes the charge, before the spell fires. The weapon summon (`BeginWeaponDraw` / `WeapEquip_Out`,
  lines 3402–3408) happens after `MRh_SpellFire_Event` and is driven by the bound-weapon effect, so it is untouched.
- **Interaction to verify (quickstart §5)**: with **Skip Equip Animations ON**, the plugin's own `InstantEquipAnim`
  window (`ScheduleDisableSkipEquip`, timer fallback) may still be open when a forced-fast cast equips the bound
  weapon, which could shorten the weapon draw.
- The spec's edge case leaves that to Skip Equip Animations, so this is accepted behavior. It is recorded for the
  user to confirm.

---

## R7. FR-003 (interruption caused by the summon itself)

**Decision**: no code change.

**Rationale**: with the patch OFF, the summon's own `InterruptCast` (line 3398) arrives after `OnSpellFired`, when
the hand's phase is already `Done`/`WaitingChargeRelease`. `OnCasterInterrupt` only reacts in `StartRequested` /
`Casting`, so it already treats that interruption as non-blocking. With R2/R3 the patch-ON flow follows the same
order. Quickstart §3 verifies it.

---

## R8. Not enough magicka (spec edge case)

**Decision**: covered by existing behavior plus R2 and R3.

- `OnSlotPressed` refuses unaffordable slots, so the player gets the HUD red ring and nothing starts.
- After R2 the patch no longer drains extra magicka, so a slot that was affordable at press stays affordable.
- If magicka drops mid-cast (damage) after a skip, R3 ends the session cleanly.
- **Out of scope**: the general restart loop when the game refuses a cast *before* any skip (for example, magicka
  drained by an enemy between press and cast) already exists for all automatic casts. It is recorded as a follow-up,
  not changed here.

---

## R9. Tooltip and documentation (FR-008)

**Decision**: update the tooltip in two places, plus the design note.

- **Default string**: `src/UI/MENU.cpp`, `Strings::Get("Tooltip_SkipChanneling", ...)`.
- **Distributed strings file**: the `Tooltip_SkipChanneling=` line in
  `D:\ModdingSkyrim\MO2\mods\Integrated Magic\SKSE\Plugins\IntegratedMagic_Strings.txt`. It overrides the default and
  is **not** versioned in the repo.
- **New text**: "When enabled, Fire-and-Forget spells cast automatically (Automatic mode, or Press with
  auto-attack) skip the hand charging animation and fire instantly. Bound weapons are included; their summon
  animation is kept. Concentration spells are not affected."
- **Design note**: `.claude/tasks/P1-skip-channeling-patch.md` describes an approach that was never implemented:
  `CastSpellImmediate`, `immediateDispatch`, `SlotPressAction::skipChanneling` and `OnImmediateCastComplete`. None of
  these exist in the code (verified 2026-11-19). Mark the note as superseded, point it to this spec and the contract,
  and update its scope table: Bound is included, and so is Press with auto-attack.

**Note**: MO2 has two mods with `IntegratedMagic.dll`: `IntegratedMagic` (the build output) and
`Integrated Magic` (the release package with the strings file). MO2 priority decides which DLL loads. The user should
keep only one enabled while testing.

---

## R10. Verification approach

**Decision**:
- Build both presets, debug last, so in-game validation runs on the DEBUG build (Constitution VI).
- Run the diagnostic gate (task T015): patch OFF and ON with Bound Sword, an expensive fire-and-forget spell (for
  example Fireball) and a cheap one (Candlelight or Firebolt), comparing the magicka spent.
- Then run the TEST_MATRIX cases in the spec's Verification section, plus the new bound + Skip Channeling cases
  (FR-010), including the concentration "not affected" case (5b.11) and the dual-cast pairing (FR-012).

---

## R11. Dual-cast synchronization of the forced charge (FR-012, Constitution IV)

**Decision**: in dual cast, pair the forced charge in `PumpAutomatic`.

- `PumpCastPhase` does not emit `forceChargeComplete` itself. It only reports `chargeSkipEligible = true` when the
  hand meets the applicability rule (contract §1).
- `PumpAutomatic` runs right after both `PumpCastPhase` calls, next to the existing dual-cast `syncForRestart`
  block.
  - If **both** hands are eligible in the same frame, it sets `chargeSkipped` on both and emits `forceChargeLeft`
    and `forceChargeRight` together.
  - If only one hand is eligible, it emits nothing. That hand keeps charging naturally until the other also reaches
    `kUnk02`, or completes on its own.
- Outside dual cast, the single-hand path is unchanged: `PumpCastPhase` emits directly.

**Rationale**:
- The release-to-fire UP for each hand follows that hand's charge completion (`MagicStatePump.cpp` ~404). Today
  the dual-cast sync (`syncForRestart`, ~714–728) covers restarts only.
- With per-hand forcing, a hand that reaches `kUnk02` one frame earlier would complete its charge first and fire
  first. The game's behavior graph would then leave dual-cast mode and fire two single casts, which is the exact
  failure the Principle IV invariant exists to prevent.
- Pairing at the `PumpAutomatic` level keeps both intents in the same frame, so both charges complete together and
  both release UPs follow in the same frame.
- The original P1 note already asked for both hands to be handled together in dual cast.

**Alternatives considered**:
- Force the earlier hand and accept a one-frame skew (risks two single casts).
- Delay the earlier hand's release UP until the other is charged (adds a new wait state to the release path, so more
  risk in Principle III/IV code).

---

## R12. Spell classification for applicability (F1)

**Decision**: decide applicability with the plugin's own classifier, `DetectSpellType(spell)`
(`Shared/SpellTypeDetector.h`), accepting `SpellType::Cast` and `SpellType::Bound`, instead of the raw
`spell->GetCastingType()`.

**Rationale**: this is the same categorization that drives per-type activation defaults (`BoundMode`, `CastMode`) and
that the spec's Key Entities refer to. The original P1 design keyed on `SpellType::Cast` too. One source of truth
avoids drift, and Concentration / Power / Unknown are excluded by construction.
