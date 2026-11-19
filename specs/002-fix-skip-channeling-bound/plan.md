# Implementation Plan: Fix Skip Channeling patch looping on bound weapon spells

**Branch**: `feat/patch-for-spell-channeling-skip` (feature dir `specs/002-fix-skip-channeling-bound`) | **Date**:
2026-11-19 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-fix-skip-channeling-bound/spec.md`

## Summary

With Skip Channeling ON, a Bound Sword cast loops: start → interrupted → restart. The user's log shows the cause.
When the patch forces the charge to complete, it also drains `cost × castingTimer` magicka itself. For an expensive
spell this leaves the player below the cost, so the **game cancels the cast** before anything is summoned. The
plugin's 0.2 s "spurious interruption" rule then retries, and the game refuses new casts until magicka regenerates
(research R1).

The fix:

1. **Stop the manual magicka drain.** The game charges the normal cost. A diagnostic gate confirms the cost model,
   with a correct-remainder fallback (R2).
2. **Treat an interruption after a forced charge as a real failure.** No retry: finish the hand(s) and schedule the
   finalize (R3).
3. **Move the game write out of `Domain/`** into a new outbound adapter `MagicAction::ForceChargeComplete`, emitted
   as an intent (R4, Constitution I).
4. **Add `[FLOW] SkipChanneling:` diagnostics** (R5).
5. **Make the patch apply explicitly to spells the plugin classifies as `Cast` or `Bound`** (`DetectSpellType`, R12),
   bound weapons included. In dual cast, force **both hands in the same frame** or neither (R11, FR-012).
6. **Fix the tooltip**, both the default and the distributed strings file, and the design note (R9).

The weapon-appearing visual is not touched: it happens after the spell fires (R6).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: CommonLibSSE-NG v16.9.0 (`RE::ActorMagicCaster`, `RE::MagicCaster::State`,
`RE::SpellItem`), spdlog 1.17, ImGui 1.89.1 (menu tooltip)

**Storage**: N/A. No INI or persistence changes. The tooltip override lives in the distributed
`IntegratedMagic_Strings.txt`, which is not versioned.

**Testing**: no automated harness. `release` + `debug` builds, then a DEBUG-log diagnostic gate and the
TEST_MATRIX cases in `quickstart.md` (Constitution VI).

**Target Platform**: Windows x64, Skyrim SE / AE via Address Library

**Project Type**: SKSE plugin (`IntegratedMagic.dll`)

**Performance Goals**: no per-frame cost increase beyond one boolean check per hand. Logs only when the patch acts.

**Constraints**:

- Main-thread only (Constitution II).
- Dual-cast hands start and stop together (IV).
- Patch OFF means zero behavior change (V).
- No new game mutation in `Domain/` (I).

**Scale/Scope**: about 9 source files plus 3 docs/strings files. One new `HandMode` flag, two new result intents and
one new outbound function.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle / gate | Assessment | Status |
| --- | --- | --- |
| **I. Layered architecture** | The existing skip block writes game state (`castingTimer`, `DamageActorValue`) from `Domain/MagicStatePump.cpp`. The design **migrates** that write to `Adapters/Outbound` (`MagicAction::ForceChargeComplete`) driven by a domain intent, and removes the magicka write. Net result: fewer game mutations in `Domain/`. | ✅ Pass (improves) |
| **II. Main-thread state machine** | New `HandMode::chargeSkipped` lives in the existing struct and is reset by `hm = {}` (`EnterHand`), `FinishHand`, `ResetSessionState` and the transition back to `StartRequested`. It is documented in `.claude/CLAUDE.md` in the same change. | ✅ Pass |
| **III. Never strand the player** | A failure after a forced charge now ends the hand and schedules the 0.7 s finalize instead of looping until the 30 s timeout. Every DOWN still gets its UP (`FinishHand` → `finished*` → release). | ✅ Pass (fixes a III violation) |
| **IV. Dual-cast sync (NON-NEGOTIABLE)** | In dual cast the forced charge is **paired**: `PumpCastPhase` only reports `chargeSkipEligible`, and `PumpAutomatic` emits `forceChargeLeft` and `forceChargeRight` together, only when both hands are eligible in the same frame (R11, FR-012). Both charges therefore complete together and both release UPs follow in the same frame. A forced-charge failure finishes **both** hands in the same call. The restart/sync code paths for non-skipped casts are untouched. TEST_MATRIX 1.3 and 5b.4 with the patch ON are in the plan. | ✅ Pass (gate run planned) |
| **V. Opt-in & compatibility** | Patch flag unchanged (default OFF). Patch OFF means no code path change (FR-006). No INI key renames. Magicka cost becomes vanilla, which is a correctness fix documented in the tooltip. | ✅ Pass |
| **VI. Observable & verified** | Adds `[FLOW] SkipChanneling:` lines, a diagnostic gate before finalizing the cost handling, and new TEST_MATRIX cases (FR-010). In-game validation runs on the **release** build, because the DEBUG DLL fails to load; see Complexity Tracking. | ⚠️ Deviation (accepted) |
| **Tech constraints** | No new dependency; no Address Library changes; no per-frame allocation. | ✅ Pass |
| **Merge gates** | Plan includes both presets, the TEST_MATRIX cases including section 1, the 5.1 baseline, and the CLAUDE.md update (new flag, intent and adapter). | ✅ Planned |

**Pre-design result**: PASS.

**Post-design re-check**:

- The data model adds one per-hand flag with explicit reset points, and two intents merged like the existing
  `start/release` intents.
- The contract confines game writes to the outbound adapter.
- No principle changes status. **PASS.**

## Project Structure

### Documentation (this feature)

```text
specs/002-fix-skip-channeling-bound/
├── spec.md                    # updated with log-confirmed root cause (Background 4–6, FR-005, SC-007)
├── plan.md                    # this file
├── research.md                # R1–R10 (log evidence, cost decision gate, failure handling, architecture)
├── data-model.md              # HandMode::chargeSkipped, new intents, transitions
├── quickstart.md              # diagnostic gate + bound/regression/tooltip validation
├── contracts/
│   └── skip-channeling.md     # applicability, effect, failure handling, log lines, tooltip text
├── checklists/
│   └── requirements.md
└── tasks.md                   # /speckit-tasks (not created here)
```

### Source Code (repository root)

```text
include/
├── Domain/State.h                         # + HandMode::chargeSkipped (R3)
├── Shared/PumpResults.h                   # + PumpCastPhaseResult::forceChargeComplete / chargeSkipEligible;
│                                          #   PumpAutomaticResult::forceChargeLeft/Right (R4, R11)
└── Adapters/Outbound/MagicEquip.h         # + MagicAction::ForceChargeComplete(player, hand) (R4)

src/
├── Domain/MagicStatePump.cpp              # PumpCastPhase: Cast/Bound applicability via DetectSpellType (R12), emit intent
│                                          #   once (single hand) or report chargeSkipEligible (dual), no magicka write,
│                                          #   SkipChanneling logs (R2, R4, R5); PumpAutomatic: merge intents + dual-cast
│                                          #   pairing of the forced charge (R11);
│                                          #   OnCasterInterrupt: chargeSkipped → real failure, dual-cast both hands (R3);
│                                          #   OnSpellFired: "fired" log (R5); reset chargeSkipped on StartRequested
├── Domain/MagicStateSlot.cpp              # FinishHand: reset chargeSkipped (R3)
├── Adapters/Outbound/MagicEquip.cpp       # ForceChargeComplete: castingTimer = 0 (+ R2 fallback if the gate requires it)
├── Application/SpellSystemController.cpp  # OnFrame: execute forceChargeLeft/Right after PumpAutomatic;
│                                          #   OnCastInterrupted: handle finished + finalize result (R3)
└── UI/MENU.cpp                            # default Tooltip_SkipChanneling text (R9)

docs/TEST_MATRIX.md                        # + bound × Skip Channeling cases (FR-010)
.claude/CLAUDE.md                          # skipChanneling description, chargeSkipped, new intent/adapter (merge gate 4)
.claude/tasks/P1-skip-channeling-patch.md  # Bound row: now included, only the charge is skipped (FR-008)

(outside the repo)
D:\ModdingSkyrim\MO2\mods\Integrated Magic\SKSE\Plugins\IntegratedMagic_Strings.txt  # Tooltip_SkipChanneling= line (R9)
```

**Structure decision**: the existing layered layout.

- The decision (when to skip, how to classify the interruption) stays in `Domain/`.
- The game write moves to `Adapters/Outbound/`.
- The controller wires the intent, as it already does for `start/release/stop` attack intents.

**Notes**:

- `OnCasterInterrupt` currently returns a `CastInterruptResult` without finalize information. For R3 the domain
  calls `ScheduleSpellFireFinalize` itself; the existing `PumpSpellFireFinalize` then exits within 0.7 s. No new
  controller plumbing is needed beyond the existing `finished*` release.
- The distributed strings file is outside the repository. Its edit is a separate deliverable step, and the user
  should keep a single Integrated Magic mod enabled in MO2 while testing (R9).

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **Principle VI**: in-game validation (T015 and the remaining in-game tasks) runs on the **release** DLL, not on a DEBUG build. Accepted by the user on 2026-11-19. | The DEBUG DLL fails to load. SKSE reports "fatal error occurred while loading plugin", and the plugin log stops after `[StyleConfig] styles.ini carregado.`. The release DLL writes the same `[FLOW]` / `[Action]` lines, because `include/PCH.h` defines `DEBUG` unconditionally, so the log evidence the principle asks for is still produced. | Blocking the feature on the DEBUG load failure: that failure is unrelated to this change (it happens before any hook is installed, and the load path is unchanged since spec 001) and needs its own investigation. |
