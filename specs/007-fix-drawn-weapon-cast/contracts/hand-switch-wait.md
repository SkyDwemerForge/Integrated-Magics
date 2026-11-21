# Contract: hand-switch wait before the first plugin press (D1, research R3)

Applies only if research R1 confirms H1 (or R4's ready gate is chosen for H2). States and fields are in
[data-model.md](../data-model.md).

> **Not built (T009, research R11)**: the hand shows as magic within one frame of the equip, so this wait would not
> have helped. The shipped fix is D6, which keeps the game's equip animation out of the 1 s retry bound and leaves
> the presses unchanged. Its rules are in [data-model.md](../data-model.md) (D6) and research R11. This contract is
> kept as the record of the rejected design.

## §1. Which hands wait

A hand enters the wait in `OnSlotPressed`, right after `EnterHand`, when all of these hold:

- the activation started with the weapons out (`_session.wasHandsDown == false`),
- the snapshot item of that hand was a weapon, shield or torch (`_restore.snapshot.<hand>Obj.base != nullptr`),
- the plugin drives the hand's cast (`IsPluginCast`: Automatic, Hold with auto-attack, Press with auto-attack).

A hand that already held a spell, a sheathed activation, and a hand the player casts manually never wait. Their flow
is unchanged.

## §2. While a hand waits

- **No press**: `OnEquipComplete` does not call `RequestAutoAttackStart` for it, and no DOWN is dispatched.
- **No restart paths**: these leave a waiting hand alone:
  - `NotifyAttackEnabled` (`EnableBumper`),
  - `OnCasterInterrupt`,
  - the redispatch timer in `PumpCastPhase`.
  No UP is sent for a hand that was never pressed.
- **The hand is still plugin-cast**: the spec 005 gate keeps the player's press of that hand from the game, and a press
  cancels in Hold/Automatic.
- **Checks**: D3 and D5, if they ship, treat the wait as the start phase.

## §3. End of the wait

`PumpCastPhase` checks the hand first, before the deferred-restart and redispatch logic:

1. `handSwitchWaitSecs += dt`.
2. If the hand is ready, or `handSwitchWaitSecs >= kMaxHandSwitchWaitSecs` (0.5 s):
   - clear `waitingHandSwitch`,
   - call `RequestAutoAttackStart(hand)`, which resets `startFailSecs`, so the 1 s bound starts now,
   - return `startAttack`.
3. Otherwise, return with nothing to dispatch.

**Ready** is the signal research R1 picks. The domain reads it through the read-only helper of R2. Log lines:

- `[FLOW] HandSwitch: hand=<L|R> waiting (item=<kind>)` when the wait starts,
- `[FLOW] HandSwitch: hand=<L|R> ready after <s>s` or `... timed out after <s>s -> pressing anyway` when it ends.

## §4. Dual cast (Constitution IV, NON-NEGOTIABLE)

- If either hand of a dual-cast session waits, both do.
- The wait ends for both in the same `PumpAutomatic` frame, when both are ready or the timeout passes. Both
  `startAttack` flags are returned together, so both hands go DOWN on the same frame.

## §5. Exits (Constitution III)

- **Any exit during the wait** ends the hand as today: Hold released, block, stagger, menu, ForceExit, the 30 s
  timeout. `FinishHand` and every `HandMode` reset clear the wait.
- **No synthetic input is owed**, because none was sent.
- **The 0.5 s timeout** guarantees the wait cannot hang: a missing ready signal falls back to today's behavior after
  at most 0.5 s.
