# Contract: Skip Channeling patch behavior

Defines what the patch does, which spells and modes it applies to, what it must never change, and the diagnostic
lines it writes. It is the reference for the tooltip text (FR-008) and the TEST_MATRIX cases (FR-010).

## 1. Applicability (per hand, per cast attempt)

The patch applies to a hand when **all** of these hold:

| Condition | Source |
|---|---|
| `skipChannelingPatch` is ON | `IPatchSettings::SkipChanneling()` |
| The hand's cast is driven automatically: Automatic mode, or Press with auto-attack (`HandMode::autoActive`) | `EnterHand` |
| The hand's phase is `Casting` and its caster state is `kUnk02` (charging) | `PumpCastPhase` |
| The hand's slot spell is classified by the plugin as `SpellType::Cast` or `SpellType::Bound` (`DetectSpellType`), with charge time > 0. **Bound weapon spells are included** | `Shared/SpellTypeDetector.h`, spell data |
| `chargeSkipped == false` for this attempt | `HandMode` |

The decision is made per hand (FR-007). Concentration, Power and Unknown spells never qualify (research R12).

**Dual cast (FR-012, research R11)**: when `_session.isDualCasting`, a hand that meets every condition above is only
*eligible* (`chargeSkipEligible`). `PumpAutomatic` forces the charge only when **both** hands are eligible in the
same frame, emitting both intents together. Otherwise it forces neither that frame.

## 2. Effect

- **Completes the charge.** The outbound adapter `MagicAction::ForceChargeComplete(player, hand)` sets the hand
  caster's `castingTimer` to 0 in the same frame the domain emits the intent. The game moves the caster to `kReady`,
  and the plugin's normal flow releases the attack and the spell fires.
- **Magicka.** The total cost per cast equals the vanilla cost (SC-007). Gate T015 showed that the game drains a
  charge's cost progressively, `cost × frame / chargeTime`, so a skipped charge pays only the skip frame by itself
  (research R2). The patch charges the rest:
  1. At the skip, the domain records `HandMode::skipMagickaOwed = cost × clamp(castingTimer − frame, 0, chargeTime) /
     chargeTime`. Here `frame` is `RE::GetSecondsSinceLastFrame()`, the part the game still drains in the skip frame.
     In dual cast each hand owes half of `cost × GetDualCastCostMultiplier`.
  2. When the spell fires (`MRh/MLh_SpellFire_Event`), `OnSpellFired` returns the owed amount as
     `SpellFiredResult::magickaToDrain`. In dual cast the first event pays for both hands. The controller calls
     `MagicAction::DrainMagicka`, which is clamped so magicka never goes below 0.
  3. **Never during the charge.** Draining before the spell fires makes the game cancel the cast. That was the
     bound-weapon restart loop (research R1).
  4. If the attempt fails after the skip, the owed amount is discarded, so the player pays nothing extra, as in
     vanilla.
- **Never touched.** Anything after the spell fires: projectile and hit effects, the bound weapon summon and its
  weapon-appearing visual (FR-011), and restore.

## 3. Failure handling

If the hand's caster is interrupted while `chargeSkipped == true`:

1. The interruption is a **real failure**. It is never treated as a "spurious" pre-cast interruption, and there is
   no UP→DOWN restart.
2. `FinishHand(hand)` runs and `ScheduleSpellFireFinalize(hand)` is set, so the session ends within 0.7 s and
   equipment is restored as usual (Constitution Principle III).
3. In dual-cast, both hands finish in the same call (Constitution Principle IV).

## 4. Diagnostic lines (FR-009)

These are `MAGIC_DEBUG_LOG` lines with the prefix `[FLOW] SkipChanneling:`:

```text
[FLOW] SkipChanneling: applied hand=<L|R> spell=<0xFORMID> '<name>' chargeTime=<s> castingTimer=<s> cost=<n> casterCost=<n> owed=<n> magicka=<n>
[FLOW] SkipChanneling: applied dual L+R spell=<0xFORMID> '<name>' castingTimerL=<s> castingTimerR=<s> casterCostL=<n> casterCostR=<n> owedL=<n> owedR=<n> magicka=<n>
[FLOW] SkipChanneling: skipped hand=<L|R> spell=<0xFORMID> reason=<chargeTime=0|type=Concentration|type=Power|type=Unknown>
[FLOW] SkipChanneling: fired hand=<L|R> magicka=<n> owed=<n>           (at MRh/MLh_SpellFire_Event, after an applied skip)
[Action] DrainMagicka: owed=<n> magicka=<n> -> <n>                     (right after "fired", when owed > 0)
[FLOW] SkipChanneling: interrupted after skip hand=<L|R> magicka=<n> -> finish (no retry)
```

`casterCost` is the caster's `currentSpellCost`, logged only to confirm the cost model. `owed` is computed from
`cost`.

`skipped` is logged at most once per cast attempt. `fired` and `interrupted` are logged only when `chargeSkipped`
was true.

## 5. Tooltip text (FR-008)

Key `Tooltip_SkipChanneling`. The default lives in `src/UI/MENU.cpp`; the override lives in the distributed
`IntegratedMagic_Strings.txt`.

```text
When enabled, Fire-and-Forget spells cast automatically (Automatic mode,
or Press with auto-attack) skip the hand charging animation and fire instantly.
Bound weapons are included; their summon animation is kept.
Concentration spells are not affected.
```

## 6. Compatibility

- Patch OFF: no behavior change at all (FR-006, Constitution Principle V).
- No INI key changes.
- Skip Equip Animations is independent. Whether the summoned weapon's draw is shortened is that patch's concern,
  not this one's.
