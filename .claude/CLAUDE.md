# Integrated Magic — Development Guide

SKSE plugin for Skyrim SE/AE. A hotkey-driven magic slot system: assign spells, shouts, and powers to numbered slots; press a hotkey to equip them, cast, then auto-restore previous equipment. Inspired by action games like Black Myth: Wukong.

Built with CommonLibSSE-NG (C++23), ImGui (HUD), Microsoft Detours (hooks). Output: `IntegratedMagic.dll` in `SKSE/Plugins/`.

---

## Architecture layers

```
Inbound Hooks → Input Layer → Application Controllers → Domain (MagicState)
                                                       ↘ Outbound Adapters → Game
                HUD (ImGui) ←←←←←←←←←←←←←←←←←←←←←←←←←↗
```

| Layer | Namespace | Location |
|---|---|---|
| Inbound hooks (game → plugin) | — | `include/Adapters/Inbound/`, `src/Adapters/Inbound/` |
| Outbound adapters (plugin → game) | — | `include/Adapters/Outbound/`, `src/Adapters/Outbound/` |
| Application controllers | `Application::` | `include/Application/`, `src/Application/` |
| Domain / state machine | `IntegratedMagic::` | `include/Domain/`, `src/Domain/` |
| Input processing | `Input::` | `include/Input/`, `src/Input/` |
| UI / HUD | `UI::` | `include/UI/`, `src/UI/` |
| Config & persistence | `IntegratedMagic::Config::` | `include/Config/`, `src/Config/` |
| Shared value types | `IntegratedMagic::` | `include/Shared/` |
| Save/load persistence | `IntegratedMagic::` | `include/Persistence/`, `src/Persistence/` |

Entry point: `src/plugin.cpp` — SKSE plugin init, registers hooks, handles save/load events.
Hook installation: `include/Hooks.h` / `src/Hooks.cpp` (see [Hooks](#hooks)).

---

## Hooks

All hooks are installed once by `IntegratedMagic::Hooks::Install_Hooks()` during `SKSEPluginLoad`. The exceptions are `WndProcHook`, which `D3DInitHook` installs once the game window exists, and `MenuInputShieldHook`, which `DXGIPresentHook` installs on the first hotkey capture. Each `Install()` logs `[Hooks] <Name> installed` at `info` level, so the lines appear in release builds too.

| Hook | Kind | Trampoline bytes | Target |
|---|---|---|---|
| `PollInputDevicesHook` | call (5-byte) | 14 | `RelocationID(67315, 68617)` + `VariantOffset(0x7B, 0x7B, 0x81)` |
| `PlayerAnimGraphHook` | vtable | 0 | `VTABLE_PlayerCharacter[2]`, slot 1 (`BSTEventSink<BSAnimationGraphEvent>::ProcessEvent`) |
| `D3DInitHook` | call (5-byte) | 14 | `RelocationID(75595, 77226)` + `VariantOffset(0x9, 0x275, 0x9)` |
| `DXGIPresentHook` | call (5-byte) | 14 | `RelocationID(75461, 77246)` + `VariantOffset(0x9, 0x9, 0x15)` |
| `MagicCasterInterruptHook` | vtable | 0 | `VTABLE_ActorMagicCaster[0]`, slot 8 |
| `MagicCasterStartCastHook` | vtable | 0 | `VTABLE_ActorMagicCaster[0]`, slot 6 |
| `WndProcHook` | window procedure | 0 | `SetWindowLongPtr` on the game window |
| `MenuInputShieldHook` | call (5-byte) | 14 | Same site as `PollInputDevicesHook`. Installed on the first hotkey capture, after the hooks other plugins install there at load (SKSE Menu Framework, Input Manager), so it is the outermost thunk (spec 009) |

### Hook space (trampoline) policy

Call-site hooks (`Hook::stl::write_call` / `write_jmp` in `extern/Detours/HookUtil.hpp`) write their jump stubs into `SKSE::GetTrampoline()`. That space is **reserved exactly once**:

- **Where**: `src/plugin.cpp` → `SKSE::Init(skse, {.trampoline = true, .trampolineSize = IntegratedMagic::Hooks::kHookSpaceBytes})`. The budget lives in `include/Hooks.h` (`kCallHookBytes`, `kCallHookCount`, `kHookSpaceBytes = 128`), and a `static_assert` keeps usage at or below 75%.
- **Fallback**: if SKSE exposes no trampoline interface, `Install_Hooks()` allocates `kHookSpaceBytes` itself with `Trampoline::create` and logs a warning.
- **Cost per helper**: `write_call<T, 5>` / `write_jmp` = 14 B, `write_call<T, 6>` = 8 B, `write_vfunc` / `write_detour*` = 0.
- **Forbidden**: `SKSE::AllocTrampoline`. It is deprecated in CommonLib v16, and since v10.1.0 only the *first* call is honored. Helpers must never reserve space themselves; they only check it (`Hook::stl::detail::EnsureHookSpace`, which fails with a message naming the calling `Install()`) and consume it.
- **Startup report**: `Install_Hooks()` ends with `[Hooks] hook space: <used> / <capacity> B used (<pct>%), SKSE trampoline interface: <yes|no>` and warns above 75%.

**Adding a hook**:

1. Install it from `Install_Hooks()`, or from a hook that runs later, as `WndProcHook` does.
2. If it uses the trampoline, bump `kCallHookCount`, or adjust the byte math for 6-byte patches. If the `static_assert` fires, raise `kHookSpaceBytes`.
3. Log `spdlog::info("[Hooks] <Name> installed")` after the patch is written.
4. Add it to the table above and to `docs/TEST_MATRIX.md` section 0.

> History: commit `6188de8` (#64) removed the old upfront `AllocTrampoline(64)`. It went unnoticed until the CommonLib update to v10.1.0 (`8aaf4bb`) made repeated reservation requests no-ops, after which the second call hook failed with `Trampoline.cpp(164): Failed to handle allocation request`. See `specs/001-fix-trampoline-allocation/`.

### Animation events

`PlayerAnimGraphHook` patches the player's `BSTEventSink<BSAnimationGraphEvent>::ProcessEvent` (`RE::VTABLE_PlayerCharacter[2]`, slot 1). After calling the original handler, its thunk calls `AnimListener::HandleAnimEvent` (`include/Adapters/Inbound/AnimEventAdapter.h`), which forwards the event tag to `Application::SpellSystemController::NotifyAnimEvent`.

`AnimEventAdapter` is a stateless free function and needs **no instance and no `AddEventSink` registration**. Registering it as an event sink as well would deliver every animation event twice.

---

## Key singletons

| Singleton | Purpose |
|---|---|
| `IntegratedMagic::MagicState::Get()` | Core state machine — spell activation lifecycle |
| `Application::SpellSystemController::Get()` | Drives MagicState each frame, handles events |
| `Application::InputController::Get()` | Processes raw input, slot edge detection |
| `Application::HudController::Get()` | Drives HUD frame rendering |
| `IntegratedMagic::SlotCooldownTracker::Get()` | Per-slot cooldown progress tracking |
| `IntegratedMagic::Config::MagicConfigAdapter::Get()` | Config read/write |
| `IntegratedMagic::SaveSpellDB::Get()` | Per-save slot assignments |
| `IntegratedMagic::SpellSettingsDB::Get()` | Global per-spell activation mode settings |

> **Note:** Spell assignment is done via free functions in `IntegratedMagic::MagicAssign` (see `include/Shared/AssignService.h`): `ComputeSpellAssignment`, `ComputeShoutAssignment`, `ComputeClearHand`, `ComputeClearShout`. These are not a singleton.

---

## Per-frame data flow

```
PollInputDevicesHook::ProcessAndFilter(input_events)
  → InputController processes raw input
  → KeyStateStore updated (atomic per-key booleans)
  → HotkeyMatcher tests each slot combo
  → SlotEdgeStore detects press/release edges
  → ExclusiveStore enforces one-at-a-time activation
  ↓
SpellSystemController::OnFrame(dt, inputBlocked)
  → InputController::ConsumePressedSlot() / ConsumeReleasedSlot()
  → MagicState::OnSlotPressed(slot) → returns SlotPressAction
      • Snapshots current equipment → RestoreContext
      • Builds EquipIntents (which spells for which hands)
      • Optionally calls MagicAction::SetSkipEquipVars + DrawWeaponMagicHands
  → Executes EquipIntents via MagicAction::EquipSpellInHand / EquipShoutInVoice
  → MagicState::PumpAutomatic(dt) — advances auto-cast phases
  → MagicState::PumpAutoAttack(dt) — holds synthetic attack input
  ↓
DXGIPresentHook → HudManager::DrawHudFrame()
  → Reads MagicState + slot config → HudView
  → SlotDrawer renders slot circles, icons, cooldowns
  → PopupDrawer renders assignment popup if HUD toggle was pressed
  → Mouse click on popup → MagicAssign::ComputeSpellAssignment()
```

---

## MagicState — state machine

`include/Domain/State.h`. The heart of the system. Three files implement it:
- `src/Domain/MagicStateLifecycle.cpp` — slot press/release, snapshot, restore, equip-complete
- `src/Domain/MagicStatePump.cpp` — per-frame pump, interrupt/castStop handling, phase transitions
- `src/Domain/MagicStateSlot.cpp` — `EnterHand`, `TogglePressHand`, `DisableHand`, `FinishHand`, `PrepareSlotEntry`, `SetModeSpellsFromHand`

### Key structs inside MagicState

**`SessionState _session`** — overall session status:
- `active`, `activeSlot` — is a slot currently active and which one
- `wasHandsDown` — was the player's weapon state `kSheathed` when the slot was pressed
- `isDualCasting` — both hands have the same spell (dual cast mode)
- `activeTimeoutSecs` — safety timeout counter to force-exit hung sessions
- `activeLeftID`, `activeRightID`, `activeShoutID` — FormIDs of the active slot's spells/shout
- `modeSpellLeft`, `modeSpellRight` — pointers to the equipped spells (used to match caster events)

**`HandMode _left / _right`** — per-hand cast state (same struct for both hands):
- `mode` — `ActivationMode::Hold | Press | Automatic`
- `holdActive` — hotkey is being held (Hold mode)
- `pressActive` — toggle is on (Press mode)
- `autoActive` — automatic cast is running
- `wantAutoAttack` — whether synthetic attack input should be injected for this hand
- `autoCastPhase` — `Idle → StartRequested → Casting → WaitingChargeRelease → Done`
- `waitingChargeComplete` — charge is in progress, waiting for `IsChargeComplete`
- `chargeComplete` — charge spell has been detected as fully charged
- `holdFiredAndWaitingCastStop` — Hold mode: attack was sent, waiting for final castStop
- `pendingRestartNextFrame` — UP was dispatched this frame; DOWN should go on the next frame
- `needsManualFireInKReady` — cast was confirmed externally without a synthetic hold; fire via stopAttack from kReady
- `chargeSkipped` — Skip Channeling forced this hand's charge in the current attempt. An interruption while set is a real failure (finish + finalize, no retry), never "spurious". Reset by `FinishHand`, `RequestAutoAttackStart`, the post-cast spurious branch and every value reset of `HandMode`
- `skipMagickaOwed` — Skip Channeling: the charge cost the game did not drain because the charge was skipped. Set with `chargeSkipped`, paid by `OnSpellFired` (`SpellFiredResult::magickaToDrain`), and reset in the same places as `chargeSkipped`, so a failed attempt costs nothing extra
- `startRequestSecs` — time spent in `StartRequested` before timing out and retrying
- `startFailSecs` — total time the current attempt has spent without a stable cast. Restarts do not reset it; `RequestAutoAttackStart`, `FinishHand`, every `HandMode` reset and a cast lasting `kSpuriousInterruptWindowSecs` (0.2 s) do. At `kMaxCastStartSecs` (1.0 s) the session ends (spec 005)
- `equipAnimGraceSecs` / `equipAnimGraceActive` — drawn activation only: time the game spent playing its equip/draw animation (`IsEquipping`), which goes here instead of `startFailSecs`, at most `kMaxEquipAnimGraceSecs` (2.0 s). Reset wherever `startFailSecs` is (spec 007)
- `castingElapsedSecs` — elapsed time since cast confirmed (Casting phase)
- `waitingSpellFireFinalize` — waiting for spell fire event or caster idle to finalize
- `spellFireFinalizeSecs` — time since spell fire finalize was scheduled
- `finished` — this hand is done

**`RestoreContext _restore`** — equipment restoration:
- `snapshot` (`HandSnapshot`) — captured left/right objects + spells + shout before activation
- `prevExtraEquipped` — additional worn items to restore (overlay items)
- `dirtyLeft / dirtyRight / dirtyShout` — which slots were modified and need restoring
- `pendingRestore` — restore scheduled immediately after session ends
- `pendingRestoreAfterSheathe` — restore waits for sheathe animation to complete
- `sheatheAnimComplete` — sheathe animation event was received
- `pendingPowerRestore` — power/shout restore deferred until animation settles
- `pendingPowerRestoreDelaySecs` — countdown for power restore delay
- `sheatheWaitSecs` — time spent waiting for sheathe; gives up after `kSheatheWaitTimeoutSec`

**`AutoAttackState _aa`** — synthetic attack hold state: `heldLeft/heldRight`, `secsLeft/secsRight`; helpers `Held(Hand)`, `Secs(Hand)`.

**`ShoutState _shout`** — voice slot lifecycle:
- `modeShoutID`, `finished`, `held`, `heldSecs` — shout/power identity and hold state
- `isPower` — true if the equipped form is a Power (not a shout)
- `powerAutoSecs` — elapsed time for Automatic-mode power hold
- `waitingStopEvent` — waiting for `shoutStop` anim event before finishing
- `dirty` — shout slot was modified

### Activation modes

| Mode | Behavior |
|---|---|
| `Hold` | Hotkey held → cast loop. Release → restore. Uses `holdActive` + `holdFiredAndWaitingCastStop`. |
| `Press` | First press activates, second press (or cast end) deactivates. Uses `pressActive`. |
| `Automatic` | Fires once, auto-exits on cast complete. Uses `autoActive` + `AutoCastPhase`. |

### SpellType enum

`include/Shared/SpellType.h`: `Unknown`, `Concentration`, `Cast`, `Bound`, `Power`, `Shout`.

---

## Automatic mode — cast phase loop

For `Automatic` (and `Hold` with `wantAutoAttack`), the plugin drives the cast via synthetic attack input:

1. **StartRequested** — attack button held DOWN. Waits for caster to reach `kUnk02` (stable charging state).  
   - If interrupted by a spurious `OnCasterInterrupt` (pre-cast noise): releases UP, sets `pendingRestartNextFrame`. On the next frame, goes DOWN again.
   - If `kRedispatchInterval` (0.15 s) elapses without progress: same UP → DOWN restart.
   - **Retry bound**: when a plugin-cast hand's `startFailSecs` reaches `kMaxCastStartSecs` (1.0 s), `PumpAutomatic` ends the session through `ForceExit` (the same exit as the 30 s timeout), before pumping the cast phases. Log: `PumpAutomatic: hand=<L|R> cast did not start in <s>s -> ForceExit`. See `specs/005-fix-left-hand-block/`.
   - **Equip-animation pause** (spec 007): in a drawn activation, while the game reports `IsEquipping` (read through `IsEquipAnimationPlaying` in `Shared/HandDiagnostics.h`), `PumpCastPhase` adds the frame time to `equipAnimGraceSecs` instead of `startFailSecs`, for at most 2.0 s per attempt. The presses do not change.
     - **Why**: with Skip Equip Animation OFF, a spell equipped in the left hand during the draw restarts the draw animation (about 1.4 s), and a left-hand cast begins only after that animation ends.
     - **Dual cast**: both hands read the same variable, so they pause together.
     - **Logs**: `[FLOW] EquipAnimGrace: hand=<L|R> equip animation playing -> retry bound paused` and `... retry bound resumes after <s>s (animation done|cap reached)`.
     - See `specs/007-fix-drawn-weapon-cast/` (research R11).
   - **Dual-cast sync**: in dual-cast mode, whenever one hand restarts, the other is also released so both hands go DOWN together on the same frame — required for the game's behavior machine to stay in dual-cast mode.
2. **Casting** — caster reached `kUnk02`. Accumulates `castingElapsedSecs`. Waits for `IsChargeComplete`.  
   - `SkipChanneling` patch (Automatic, or Press with auto-attack): once per attempt, when the caster is in `kUnk02` and the slot spell is classified `Cast` or `Bound` (`DetectSpellType`) with charge time > 0, the domain emits `forceChargeComplete`. `SpellSystemController` then calls `MagicAction::ForceChargeComplete`, which zeroes `castingTimer`.
     - **Magicka**: the game drains a charge's cost progressively (cost × frame / chargeTime), so a skipped charge pays almost nothing by itself. The domain records the unpaid part in `skipMagickaOwed`: `cost × (castingTimer − frame) / chargeTime`, where the frame term is what the game still drains in the skip frame. Dual cast uses half of `cost × GetDualCastCostMultiplier` per hand. `OnSpellFired` returns it as `magickaToDrain`, and `SpellSystemController` calls `MagicAction::DrainMagicka` **after the spell fired**. Never drain during the charge: that makes the game cancel the cast, which caused the bound-weapon restart loop.
     - **Dual cast**: `PumpCastPhase` only reports `chargeSkipEligible`, and `PumpAutomatic` forces both hands in the same frame, or neither.
     - **Failure**: an interruption after the skip ends the hand(s) with no retry.
     - See `specs/002-fix-skip-channeling-bound/`.
   - **Real interruption** (cast longer than `kSpuriousInterruptWindowSecs`): `OnCasterInterrupt` calls `FinishAndFinalize` for the hand, and for its dual-cast partner. That is `FinishHand` + `ScheduleSpellFireFinalize`, so the session ends and the gear comes back once nothing is left. Before spec 005 the hand was only finished, and the slot stayed active until the 30 s timeout (Automatic) or the hotkey release (Hold). Log: `OnCasterInterrupt: hand=<..> real interrupt -> finish + finalize session`.
3. **WaitingChargeRelease** — charge detected as complete. Attack button released UP. Spell fires.
4. **Done** — `OnSpellFired` received; `FinishHand` called; session exits.

---

## Input system

`include/Input/` — all stateless-ish stores, no singletons.

**`KeyStateStore`** — atomic `bool[kMaxCode]` tracking which keys are currently down. Updated by `PollInputDevicesHook` each frame.
- **Mouse codes**: stored as `kMouseButtonBase + id` (256 + `RE::BSWin32MouseDevice::Keys`).
- **The wheel (ids 8 and 9) is never recorded.** The game sends a press with no release, and the wheel is not a hotkey input. `ProcessButtonEvents` skips it before hotkey capture and before any write, and `FilterEvents` always passes it to the game. See `specs/003-fix-mouse-wheel-hotkeys/`.
- **`mouseNoVkLastPressedMs`**: the last pressed (down or held) event for mouse ids 5–7, which have no Windows key state. Main thread only.

**Stuck-key recovery** — `ReconcilePhysicalKeyState` runs every 200 ms while no slot session and no exclusive press is pending. It clears inputs that are recorded as held but are no longer held:
- keyboard and mouse ids 0–4: Windows key state (`GetAsyncKeyState`),
- mouse ids 5–7: a 500 ms heartbeat (`kMouseHeartbeatTimeoutMs`),
- gamepad: XInput.

`ClearLikelyStuckKeysAfterMenuClose` also clears non-combo keys when a menu that blocks input closes.

**`HotkeyCacheStore`** — cached hotkey combos (up to 3 keys per slot, keyboard + gamepad) loaded from config.

**`HotkeyMatcher`** — tests `AreAllKeysInComboDown(slot)` against `KeyStateStore`.

**`SlotEdgeStore`** — detects rising/falling edges per slot (press vs. hold).

**`ExclusiveStore`** — tracks per-slot timing windows used by two optional patches (`MagicConfig`):

- `requireExclusiveHotkeyPatch` — a slot only activates if *exactly* the keys in its combo are pressed and no others. Example: combo R1+L1 will not fire if R1+L1+R2 are all held simultaneously. Only W/A/S/D are allowed extras (keyboard). On gamepad no extra button is allowed; the sticks are not buttons, so they never count. A refusal is logged once per press with the culprit, `[Input] ComputeAcceptedExclusive: slot=<n> <kb|gp> REFUSED (not exclusive) extra=<kb:0x..|mouse:<id>|gp:<n>>` (`FindExtraHeldCode` in `HotkeyMatcher.h`). A cancelled pending press logs `pending CANCELLED ... extra=<label>`.
- `pressBothAtSamePatch` — all keys in the combo must be pressed within a short time window of each other. Holding R1 for a long time and then pressing L1 will not activate the slot even though both are held. Key fields: `pendingSrc`, `pendingTimer`, `simWindowActive`, `simWindowRemaining`, `filterWindowActive`, `filterWindowTimer`, `deactivatedThisPress`, `fullComboSeen`.

**`ReplaySystem`** (`ReplayArr`, `DeferredVec`, `RetainedArr`) — defers input events that arrive while a slot's timing window is active and replays them once the window closes, so a held key that spans the window is not lost.

**Shared buttons and held-back presses** (`ShouldFilterAndSave` in `InputFilter.cpp`, `ExclusiveTracker.cpp`; see `specs/004-fix-gamepad-slot-priority/`). On the default gamepad layout every slot shares `RT` (the modifier).
- **Candidates**: every slot whose combo contains the button. The slot number never decides.
- **Fresh press**:
  - if any candidate is down, or has its combo fully held, it is filtered and **consumed**;
  - otherwise it is **held back** under one owner (most of its combo held, then the longest combo, then the lowest slot), and a filter window of `kComboToleranceSec` (50 ms) opens.
- **Fate of a held-back press**:
  - **consumed** when any slot whose combo contains it activates (`ConsumeHeldBackForCombo`);
  - kept while a slot containing it is **pending** (`kMaxContestedHoldSec` = 0.5 s);
  - kept while 2 or more of a combo's buttons are held (`kMaxChordHoldSec` = 100 ms, two tolerance gaps): a **chord in progress**, or a combo completed this frame whose slot goes pending later in the frame (`complete`). The cap counts the frame that crosses it;
  - otherwise **handed to the game** (replayed).
- **Combo tolerance** (`kComboToleranceSec` = 50 ms, combos of 2+ buttons): the second button within 50 ms of the first, and a 3-button combo within 100 ms. A multi-button slot activates 50 ms after its full combo is held, so a longer combo completed within that time still wins. Single-key slots with Require exclusive hotkey keep a 40 ms wait (`kSingleKeyAcceptDelaySec`).
- **Press fate** (`ExclusiveStore::kbFate` / `gpFate`, `PressFate`): a button's held repeats and release follow its own press, consumed or passed. An active session does **not** block new presses, so `RT` still casts manually during a Press session. `slotWasAccepted` is no longer used to filter events; it only keeps its edge role in `RecomputeSlotEdges`.

**Overlapping combos** (`RecomputeSlotEdges` pass 2, `ResolveOverlaps`):
- Among slots completing at the same moment (rising together, or the longer one still pending), the **longest** combo wins.
- For identical combos, the lower slot wins.
- The loser is `suppressedUntilRelease` until its combo is released.
- `LoadHotkeyCache_FromConfig` warns about identical combos: `HotkeyCache: slots <a> and <b> have the same <kb|gp> combo`.

**Logs**:
- `HoldBack: <label> held for slot=… / consumed by slot=… / handed to game (slot=…)`
- `HoldBack: slot=… window extended (slot=… pending|chord|complete)`
- `RecomputeSlotEdges: slot=… SUPPRESSED by slot=…`

**Hand-button gate** (`include/Input/HandButtonStore.h`, `GateHandEvent` in `src/Input/InputFilter.cpp`; see `specs/005-fix-left-hand-block/`). The plugin casts a hand with the same game action the player's button sends (`"Left Attack/Block"` = right mouse button / `LT`, `"Right Attack/Block"` = left mouse button / `RT`), so the two must not mix.
- **Where it runs**: on events of those user events that would reach the game: in `FilterEvents` **after** the combo rules (so combo presses such as the `RT` modifier are never gated), and on replays before `InputController` injects them.
- **Flags**: `InputController` builds `HandGateFlags` from `MagicState::IsHandPluginCast(hand)` each frame. A hand is plugin-cast when its session is active and it is `!finished && (autoActive || (holdActive && wantAutoAttack))`. `mayCancel` is "a hand plugin-cast && `!IsPressMode()`", the same rule as `blockStart`.
- **Rules** (`DecideHandEvent`):
  - a fresh press of a **plugin-cast hand** is kept from the game; it cancels in Hold/Automatic and is ignored in Press;
  - a fresh press of the **other hand** (for example a shield) reaches the game and cancels in Hold/Automatic;
  - a kept press stays kept until its release, even after the session;
  - a fresh press always starts a new fate.
- **Takeover** (`SpellSystemController::DispatchAttackPress`, every synthetic press): for each hand whose action the game sees held by the player (`HandButtonState::gameHeld`), a release is dispatched first and that press is kept until released. This is what lowers a shield held up before a slot.
- **Cancel**: the gate sets `HandButtonStore::cancelRequested`. `SpellSystemController::OnFrame` → `ConsumeInputExitRequests` runs `ForceExit` (unless `IsPressMode()`) before `DispatchSlotEvents`. The same function consumes the transform-shout `ConsumeForceExit()`, which had no caller before spec 005.
- **Resets**: `ResetInputState` (game load) clears the store. Menu close clears both hands and the pending cancel.
- **Require exclusive hotkey** is unchanged: a held block button still counts as an extra key.
- **Logs**: `HandGate: <hand> press during plugin cast -> cancel session | ignored (Press mode)`, `other-hand press -> passes, cancel session`, `button taken at cast start -> released in game`, `button released -> gate cleared`, `[SpellSystem] HandGate cancel -> ForceExit`.

**Magic Menu hold** (`include/Input/MagicMenuHold.h`, `src/Input/MagicMenuHold.cpp`; see `specs/008-fix-magic-menu-hotkey-leak/`). Quick bind (hover + slot hotkey) and the wheel hotkey work in the Magic Menu, but the menu must not run its own functions for those keys.
- **Why a separate path**: the Magic Menu is in `IsInputBlockedByMenus`, which skips `FilterEvents` entirely. So `InputController::ProcessAndFilter` runs this hold while `IsMagicMenuOpen()` and no capture is pending, after `FilterMouseForPopup`. Slots still never activate in menus.
- **Hold codes**: keys, mouse buttons and gamepad buttons in any active slot combo or in the wheel combo (`cache.hud`). Everything else, plus mouse movement, thumbsticks and the mouse wheel, passes untouched.
- **Rules** (`FilterMagicMenuEvents`, then `TickMagicMenuHold` every frame):
  - a fresh press is **consumed** if a combo containing it is fully held, otherwise **held** in a FIFO queue;
  - each frame, every fully held slot or wheel combo consumes its codes' queued and outbox events, and their held repeats and releases are dropped;
  - a queued press is handed over when `kMagicMenuHoldSec` (0.3 s, Magic Menu only) passes or its key is released. A queued release goes right after its press;
  - handed events go to an outbox that sends **one event per frame** through `EnqueueRetainedEvent`, with no hand-button gate.
- **Fates** (`MenuFate`, per code): `Held`, `Handing`, `Passed`, `Consumed`. A repeat or release follows its press and never arrives before it.
- **Clears** (`ClearMagicMenuHold`): Magic Menu close, game load (`ResetInputState`), hotkey reload or config change, and a pending capture in the Magic Menu. Queued events are dropped, not sent.
- **Logs**: `[Input] MenuHold: <label> held (queue=<n>)`, `consumed by slot=<n>|wheel`, `release consumed`, `handed to menu after <s>s (window|released)`, `<n> queued event(s) dropped (<reason>)`.
- The old `ShouldFilterHudToggle` was removed: it lived in `FilterEvents`, which never runs in the Magic Menu.
- **Which slot a quick bind assigns** (`include/Input/QuickBindResolver.h`, `ResolveQuickBind`, via `InputController::ResolveQuickBindSlots` from `HudController::OnFrame`):
  - among hotkeys completed together, the **longest** wins, and the lower slot wins between identical ones (the spec 004 rule);
  - a hotkey contained in a longer one waits up to `kMagicMenuHoldSec`, or its release, and is suppressed if the longer one completes meanwhile;
  - hotkeys contained in no other one are assigned at once.
  - **State**: `QuickBindState` (`wasDown`, `pending`, `pendingSecs`), reset on game load, hotkey reload and config change.
  - **Logs**: `[Input] QuickBind: slot=<s> waiting for a longer combo (slot=<t>)`, `SUPPRESSED by slot=<t> (longer combo|identical combo)`, `assigned after <x>s (released|window)`.

**`CaptureState`** — captures the next keypress for hotkey rebinding. Activated via `InputController::RequestHotkeyCapture()`, polled via `PollCapturedHotkey()`.

---

## Outbound adapters

**`MagicAction`** (`include/Adapters/Outbound/MagicEquip.h`) — namespace with functions to equip spells/shouts and manage animation variables:
- `EquipSpellInHand(player, spell, hand, skipAnim)`
- `EquipShoutInVoice(player, form)`
- `ClearHandSpell(player, hand)` / `ClearVoiceShout(player)`
- `SetSkipEquipVars(player, on)` / `ResetSkipEquipToken()` / `ApplySkipEquipAnimReturn(...)`
- `DrawWeaponMagicHands(bool)`
- `ForceChargeComplete(player, hand)` — Skip Channeling: zeroes the hand caster's `castingTimer` so the charge completes now (no magicka write)
- `DrainMagicka(player, amount)` — Skip Channeling: after the spell fired, drains the charge cost the skipped charge left unpaid. Clamped so magicka never goes below 0

**`Outbound::RestoreOneHand` / `ReequipPrevExtraEquipped`** (`include/Adapters/Outbound/RestoreEquip.h`) — restores items from `RestoreContext::snapshot`. Handles per-hand objects, spells, shout, and extra worn items.

**`SyntheticInput` / `detail::DispatchAttack` / `detail::DispatchShout`** (`include/Adapters/Outbound/SyntheticInput.h`) — injects synthetic attack/release input events into the game's input queue to trigger auto-cast without user pressing buttons.

---

## Config & persistence

**`include/Config/Config.h`** — main config struct: per-slot hotkeys, spell assignments, activation modes, HUD style settings, patch flags.

**`include/Config/Ports/PatchSettings.h`** — patch flags:
- `skipEquipAnimationPatch` — injects skip-equip-animation variables when equipping spells
- `skipEquipAnimationOnReturnPatch` — same, applied when restoring equipment
- `requireExclusiveHotkeyPatch` — exclusive hotkey detection (see Input system)
- `pressBothAtSamePatch` — simultaneous key window detection (see Input system)
- `skipChannelingPatch` — instantly completes the charge of `Cast` / `Bound` spells cast automatically (Automatic, or Press with auto-attack) via `MagicAction::ForceChargeComplete`; the part of the cost the skipped charge did not drain is charged after the spell fires (`MagicAction::DrainMagicka`), so the total equals the vanilla cost

**`include/Config/Ports/`** — sub-configs: `InputBindings`, `SlotAssignments`, `SpellSettings`, `HudSettings`, `PatchSettings`.

**`include/Persistence/SaveSpellDB.h`** — per-save slot assignments, keyed by save filename. Loaded on save load, written on assignment change.

**`include/Persistence/SpellSettingsDB.h`** — global activation mode settings per spell `FormID`. Persists across saves.

**`include/Persistence/Slots.h`** — API to read/write slot assignments (used by both config and persistence layers).

Max slots: `IntegratedMagic::Config::kMaxSlots = 64` (`include/Config/Limits.h`).

---

## HUD / UI

`include/UI/` — all rendered via ImGui on the DXGI Present hook.

**`HudManager`** — coordinator. Reads `MagicState` + slot config, builds `HudView`, delegates to drawers.

**`HudView`** — view model: array of `SlotView` (64 slots), each with `rightSpell`, `leftSpell`, `shoutFormID`, `cooldownProgress`, `kbCodes[3]`, `gpCodes[3]`, `canCast`, `onCooldown`.

**`SlotDrawer`** — renders slot circles, spell icons (from `TextureManager`), glow on active slot, cooldown arc, hotkey label.

**`PopupDrawer`** — renders the assignment popup when HUD toggle is pressed while Magic Menu is open. Clicking a slot circle calls `MagicAssign::ComputeSpellAssignment(slot, hand, existingLeftID)`.

**`SlotCooldownTracker`** (`include/Domain/SlotCooldownTracker.h`) — tracks per-slot cooldown progress for the HUD ring display.

**SKSE menu pair rule** (`src/UI/MENU.cpp`, spec 006). The settings page draws into SKSE Menu Framework's **shared** ImGui context (`ImGuiMCP::`), so an unbalanced pair breaks the whole SKSE menu, other mods' pages included.
- `BeginDisabled(cond)` / `EndDisabled()` are always an **unconditional** pair. Never guard them with two `if`s on a state the control in between can change: that is how the capture button made the menu fade with every capture (`specs/006-fix-menu-capture-transparency/`).
- A conditional `PushStyleColor` / `PopStyleColor` (or `PushStyleVar` / `PopStyleVar`) pair tests one `const` local computed before its control.
- `PushID` / `PopID` are unconditional, with no early `return` between them.

**Hotkey capture input guard** (`include/UI/CaptureInputGuard.h`, `src/UI/CaptureInputGuard.cpp`; see `specs/009-fix-capture-input-leak/`).
- **Why**: SKSE Menu Framework hooks the **same input call site** as `PollInputDevicesHook`, as the outer hook. While its window is open, it feeds every event to its ImGui context and passes an empty list on. So the plugin's own event filtering never reaches the menu, and the capture reads XInput (`PollCapturedInput` in `DXGIPresentHook`) and window messages (`WndProcHook`).
- **The guard**:
  - **What it does**:
    - **The outer hook**: `MenuInputShieldHook` hands the framework an empty event list while `ShouldHideMenuInput()` holds (guard on or capture waiting, with the page drawn in the last 0.25 s). That is what stops the menu: the framework acts on raw events in its own thunk, before ImGui and before its `AddInputEvent` callbacks, so key ownership and the input callback alone did not stop it in game.
    - **Key ownership**: it also claims every named ImGui key once per frame with `igSetKeyOwner` (owner `0x494D4347`, `ImGuiInputFlags_LockUntilRelease`), for input ImGui reads by itself.
    - **The hotkey**: it calls `SKSEMenuFramework::SetHotkeyEnabled(false)`.
  - **When**: while a capture waits, and until every gamepad button, mouse button and key is released, for at most `kCaptureGuardReleaseCapSec` (3 s).
  - **Restore**: only the hotkey state needs it. Once no longer claimed, unheld keys lose the owner by themselves, and held keys at their release.
  - **Updated**: once per frame from `PollCapturedInput` through `HudController::UpdateCaptureGuard`.
  - **Do not use `ImGuiConfigFlags`**: the framework rewrites its navigation flags every frame, so clearing `NavEnableGamepad` does nothing (the first design, which failed in game).
- **Edge-triggered captures**:
  - the first waiting frame records what is already held and captures nothing, so `A` on "Cap" is not captured;
  - `WndProcHook` ignores auto-repeat `WM_KEYDOWN` (`lParam` bit 30);
  - XInput is read only while a capture waits or the guard is on, because it is slow with no controller connected.
- **Outcomes**:
  - the other device cancels the capture (`DrawKeyValueCapture`), and so does `Escape`, which is never a value;
  - a capture whose page (`DrawSettings` → `NoteCapturePageDrawn`) was not drawn for `kCapturePageTimeoutSec` (0.25 s) is cancelled;
  - `DrawKeyValueCapture` clears a stale `g_fieldCapture`.
- **Held state**: undefined virtual keys and the gamepad VK range are skipped (`IsIgnoredVirtualKey`), because VK `0x07` was reported as held all the time.
- **Logs**: `[Hooks] MenuInputShieldHook installed (outer, previous target in <module>)`, where `<module>` is the hook that was outermost before (`InputManager.dll` in the validation run, with the framework's hook further in); `[UI] CaptureGuard: on (menu input hidden, keys claimed, hotkey off)`, `<n> ImGui keys claimed`, `waiting for release of <gp:n|vk:0x..>`, `off after <s>s (released|cap), input frames hidden from the menu=<n>`; `[UI] Capture: <kb|gp> input during a <gp|kb> capture -> cancelled`, `Escape -> cancelled`, `page not shown -> cancelled`, `stale field capture cleared`.

---

## Common tasks → files

| Task | Files to look at |
|---|---|
| Change how a spell is equipped | `include/Adapters/Outbound/MagicEquip.h`, `src/Adapters/Outbound/MagicEquip.cpp` |
| Change restore logic | `include/Adapters/Outbound/RestoreEquip.h`, `src/Domain/MagicStateLifecycle.cpp` (`RestoreContext`) |
| Bug in castStop / cast interruption | `src/Domain/MagicStatePump.cpp` (`OnCastStop`, `OnCasterInterrupt`) |
| Bug in Hold mode | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase`), `HandMode::holdActive/holdFiredAndWaitingCastStop` |
| Bug in Press mode | `src/Domain/MagicStateSlot.cpp` (`TogglePressHand`), `HandMode::pressActive` |
| Bug in Automatic mode | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase`, `PumpAutomatic`), `AutoCastPhase` |
| Bug in dual-cast sync | `src/Domain/MagicStatePump.cpp` (`PumpAutomatic` dual-cast sync, `OnCasterInterrupt` dual-cast sync) |
| Bug in auto-attack timing | `src/Domain/MagicStatePump.cpp` (`PumpAutoAttack`, `RequestAutoAttackStart`), `AutoAttackState` |
| Shout/power behavior | `include/Domain/State.h` (`ShoutState`), `src/Domain/MagicStatePump.cpp` (`OnShoutStop`) |
| Skip channeling behavior | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase` → SkipChanneling block, `PumpAutomatic` dual-cast pairing, `OnCasterInterrupt` `chargeSkipped` failure path, `OnSpellFired` magicka owed), `src/Adapters/Outbound/MagicEquip.cpp` (`ForceChargeComplete`, `DrainMagicka`), `src/Application/SpellSystemController.cpp` (`NotifyAnimEvent` → `DrainMagicka`) |
| Skip equip animation | `include/Adapters/Outbound/MagicEquip.h` (`SetSkipEquipVars`, `ApplySkipEquipAnimReturn`) |
| Hotkey not detected / double-firing | `include/Input/ExclusiveStore.h`, `src/Input/ExclusiveTracker.cpp`, `src/Input/InputFilter.cpp` (`ProcessButtonEvents`, `FilterEvents`), `src/Input/PhysicalReconciler.cpp` (stuck-key recovery). Check the log for `REFUSED (not exclusive) extra=` first. For shared buttons or a game action leaking during a combo, check the `HoldBack:` lines (`specs/004-fix-gamepad-slot-priority/`) |
| Block / attack button breaking a cast (twitching, slot not ending, a hand stuck after blocking) | `include/Input/HandButtonStore.h`, `src/Input/InputFilter.cpp` (`GateHandEvent`), `src/Application/InputController.cpp` (`BuildHandGateFlags`, `TakeHeldHandButtons`), `src/Application/SpellSystemController.cpp` (`DispatchAttackPress`, `ConsumeInputExitRequests`), `src/Domain/MagicStatePump.cpp` (`OnCasterInterrupt`, `PumpAutomatic` retry bound). Check the `HandGate:` lines (`specs/005-fix-left-hand-block/`) |
| Hotkey in the Magic Menu triggers a menu action (quick bind or wheel), or a quick bind fills the wrong slot | `src/Input/MagicMenuHold.cpp`, `src/Input/QuickBindResolver.cpp` (overlapping hotkeys, `QuickBind:` lines), `src/Application/InputController.cpp` (`ProcessAndFilter`, Magic Menu block). Check the `MenuHold:` lines first: `held` with no `consumed` = the hotkey never became fully held; no line = not a hold code, or the event never reached the filter (`specs/008-fix-magic-menu-hotkey-leak/`) |
| Hotkey rebinding | `include/Input/CaptureState.h`, `Application::InputController::RequestHotkeyCapture()` |
| The SKSE menu reacts during a hotkey capture, or a capture takes the wrong input | `src/UI/CaptureInputGuard.cpp`, `src/Adapters/Inbound/DXGIPresentHook.cpp` (`PollCapturedInput`), `src/Adapters/Inbound/WndProcHook.cpp`, `src/UI/MENU.cpp` (`DrawKeyValueCapture`). Check the `CaptureGuard:` / `Capture:` lines first (`specs/009-fix-capture-input-leak/`) |
| HUD slot rendering | `include/UI/SlotDrawer.h`, `src/UI/SlotDrawer.cpp` |
| Assignment popup | `include/UI/PopupDrawer.h`, `src/UI/PopupDrawer.cpp`, `IntegratedMagic::MagicAssign` |
| Cooldown display | `include/Domain/SlotCooldownTracker.h` |
| Adding a new config option | `include/Config/Config.h` + relevant `Ports/` file + `src/Config/ConfigAdapter.cpp` |
| Save/load slot assignments | `include/Persistence/SaveSpellDB.h`, `src/Persistence/SaveSpellDB.cpp` |
| Global spell settings (mode, auto-attack) | `include/Persistence/SpellSettingsDB.h` |
| Slot fails with weapons drawn and a weapon or shield in a hand (draw restarts, `cast did not start`) | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase` equip-animation pause, `PumpAutomatic` retry bound), `include/Shared/HandDiagnostics.h`. Check the `Activation hands:` / `Press hands:` lines first (`eq=1/0` = the game's equip animation is playing). See `specs/007-fix-drawn-weapon-cast/` |

---

## Debug logging

Enabled only in DEBUG builds. Macro: `MAGIC_DEBUG_LOG(fmt, ...)`. Output: `IntegratedMagic.log` in SKSE's log folder (`SKSE::log::log_directory()`, i.e. `Documents\My Games\Skyrim Special Edition\SKSE`). It is written line by line and emptied at each game launch.

Key prefixes in logs:
- `[FLOW]` — high-level cast flow events
- `[State]` — MagicState transitions
- `[SaveLoad]` — persistence events
- `[Input]` — input processing

**Hand diagnostics** (spec 007). These lines describe the weapon state and both hands: item kind and ID, `iLeftHandType` / `iRightHandType`, `IsEquipping` / `IsUnequipping`, `bEquipOk`, `IsBlocking`, `IsAttacking`, and the caster's state and spell. The format is in `specs/007-fix-drawn-weapon-cast/contracts/diagnostic-log.md`, written by `DescribeHandsForLog` (`include/Shared/HandDiagnostics.h`).
- `[State] Activation hands: ... slot=<n>`: at each new session.
- `[SpellSystem] Press hands: hand=<L|R> ...`: at each plugin press of a hand, restarts included.
- `[SpellSystem] Restore hands (before|after): ...`: around the restore. Queued equips may not show yet in `after`.
- `[Action] synthetic attack dropped: hand=<L|R> value=<v> reason=<no handler|disabled|cannot process>`: a plugin press or release the game's attack/block handler did not take.

---

## Invariants to preserve

- `MagicState` is not thread-safe. All calls must come from the game's main thread (hooks run on main thread).
- `KeyStateStore` is `atomic<bool>[]` — safe to read from any thread for display, but write only from `PollInputDevicesHook`.
- When `_session.active = true`, `_session.activeSlot >= 0` is always true.
- `RestoreContext` is only valid while `_session.active`. Cleared by `ResetSessionState()`.
- Never call `MagicState::OnSlotPressed` while `_inSlotSetup = true` (re-entrant guard).
- In dual-cast mode (`_session.isDualCasting`), both hands must always restart DOWN together. Any UP dispatched for one hand must be paired with an UP for the other in the same frame so both go DOWN on the next frame — otherwise the game's behavior machine exits dual-cast and fires individual casts.
