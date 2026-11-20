#include <algorithm>
#include <utility>

#include "Config/ConfigAdapter.h"
#include "Config/Slots.h"
#include "Domain/CasterUtil.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/InventoryUtil.h"
#include "Shared/SpellTypeDetector.h"

namespace IntegratedMagic {
    namespace {
        // Skip Channeling applies to spells the plugin classifies as Cast or Bound (research R12).
        bool IsSkipChannelingType(SpellType type) { return type == SpellType::Cast || type == SpellType::Bound; }

        // The game drains a charging spell's cost progressively (cost × frame / chargeTime) and still drains the frame
        // in which the timer is zeroed. What a skipped charge leaves unpaid is the rest of the timer (gate T015, R2).
        float SkippedChargeCost(const RE::MagicCaster* caster, float chargeTime, float cost) {
            if (!caster || chargeTime <= 0.f || cost <= 0.f) return 0.f;
            const float unpaid = std::clamp(caster->castingTimer - RE::GetSecondsSinceLastFrame(), 0.f, chargeTime);
            return cost * unpaid / chargeTime;
        }

        [[maybe_unused]] const char* SpellTypeName(SpellType type) {
            using enum SpellType;
            switch (type) {
                case Concentration:
                    return "Concentration";
                case Cast:
                    return "Cast";
                case Bound:
                    return "Bound";
                case Power:
                    return "Power";
                case Shout:
                    return "Shout";
                default:
                    return "Unknown";
            }
        }
    }

    PumpResult MagicState::PumpAutoAttack(float dt) {
        using enum Hand;
        const float add = dt > 0.f ? dt : 0.f;
        PumpResult result;

        if (_aa.heldLeft) {
            _aa.secsLeft += add;
            result.leftAttack = {Left, 1.0f, _aa.secsLeft};
            static int leftTick = 0;
            if ((++leftTick % 30) == 0)
                MAGIC_DEBUG_LOG("[FLOW] PumpAutoAttack: continuous press Left heldSecs={:.2f}", _aa.secsLeft);
        }
        if (_aa.heldRight) {
            _aa.secsRight += add;
            result.rightAttack = {Right, 1.0f, _aa.secsRight};
            static int rightTick = 0;
            if ((++rightTick % 30) == 0)
                MAGIC_DEBUG_LOG("[FLOW] PumpAutoAttack: continuous press Right heldSecs={:.2f}", _aa.secsRight);
        }
        if (_shout.held) {
            _shout.heldSecs += add;
            result.shout = {1.0f, _shout.heldSecs};
        }

        return result;
    }

    void MagicState::ConfirmAutoCastStarted(Hand hand) {
        auto& hm = ModeFor(hand);

        if (!_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={} ignored - aaHeld=false",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }

        hm.autoCastPhase = AutoCastPhase::Casting;
        hm.waitingChargeComplete = true;
        hm.castingElapsedSecs = 0.f;

        MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={}", IsLeft(hand) ? "Left" : "Right");

        if (hm.autoActive && Config::MagicConfigAdapter::Get().SkipChanneling()) {
            const auto id = (_session.activeSlot >= 0) ? Slots::GetSlotSpell(_session.activeSlot, hand) : 0;
            if (const auto* spell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr) {
                const auto type = DetectSpellType(spell);
                if (spell->GetChargeTime() <= 0.f) {
                    MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: skipped hand={} spell={:#010x} reason=chargeTime=0",
                                    IsLeft(hand) ? "L" : "R", spell->GetFormID());
                } else if (!IsSkipChannelingType(type)) {
                    MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: skipped hand={} spell={:#010x} reason=type={}",
                                    IsLeft(hand) ? "L" : "R", spell->GetFormID(), SpellTypeName(type));
                }
            }
        }
    }

    bool MagicState::RequestAutoAttackStart(Hand hand, bool /*clearWaitAfterEquip*/) {
        auto& hm = ModeFor(hand);
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) || hm.finished) {
            MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} REJECT - not eligible or finished", handStr);
            return false;
        }

        if (_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} REJECT - aaHeld already true", handStr);
            return false;
        }

        hm.pendingRestartNextFrame = false;
        _aa.Held(hand) = true;
        _aa.Secs(hand) = 0.f;
        hm.autoCastPhase = AutoCastPhase::StartRequested;
        hm.startRequestSecs = 0.f;
        hm.startFailSecs = 0.f;
        hm.chargeSkipped = false;
        hm.skipMagickaOwed = 0.f;

        MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} ACCEPTED - aaHeld=true phase=StartRequested", handStr);
        return true;
    }

    AttackEnabledResult MagicState::NotifyAttackEnabled() {
        if (!_session.active) return {};

        AttackEnabledResult result;
        using enum Hand;

        MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: EnableBumper received left.phase={} right.phase={}",
                        static_cast<int>(_left.autoCastPhase), static_cast<int>(_right.autoCastPhase));

        constexpr float kMinRedispatchSecs = 0.1f;
        auto tryRedispatch = [&](Hand hand, bool& dispatchFlag) {
            auto& hm = ModeFor(hand);
            if (hm.autoCastPhase != AutoCastPhase::StartRequested) return;
            if (hm.pendingRestartNextFrame) return;
            if (_aa.Held(hand)) {
                hm.startRequestSecs = 0.f;
                MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: hand={} attack held → keeping DOWN, resetting timer",
                                IsLeft(hand) ? "Left" : "Right");
                return;
            }
            if (hm.startRequestSecs < kMinRedispatchSecs) {
                MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: hand={} skip re-dispatch (too soon secs={:.3f})",
                                IsLeft(hand) ? "Left" : "Right", hm.startRequestSecs);
                return;
            }
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            hm.startRequestSecs = 0.f;
            hm.pendingRestartNextFrame = true;
            dispatchFlag = true;
            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: hand={} → UP (DOWN next frame)",
                            IsLeft(hand) ? "Left" : "Right");
        };

        tryRedispatch(Left, result.dispatchLeft);
        tryRedispatch(Right, result.dispatchRight);
        return result;
    }

    StateExitResult MagicState::OnCastStop() {
        using enum Hand;

        StateExitResult result{};
        if (!_session.active) {
            MAGIC_DEBUG_LOG("[State] OnCastStop: ignored - not active");
            return result;
        }

        MAGIC_DEBUG_LOG(
            "[State] OnCastStop: isDualCasting={} "
            "left.autoActive={} left.chargeComplete={} left.finished={} "
            "right.autoActive={} right.chargeComplete={} right.finished={} "
            "left.holdFired={} right.holdFired={}",
            _session.isDualCasting, _left.autoActive, _left.chargeComplete, _left.finished, _right.autoActive,
            _right.chargeComplete, _right.finished, _left.holdFiredAndWaitingCastStop,
            _right.holdFiredAndWaitingCastStop);

        auto merge = [&](StateExitResult&& src) {
            if (!result.leftAttack) result.leftAttack = src.leftAttack;
            if (!result.rightAttack) result.rightAttack = src.rightAttack;
            if (!result.shout) result.shout = src.shout;

            if (!result.restorePlan && src.restorePlan) {
                result.restorePlan = std::move(src.restorePlan);
            }

            result.waitForSheatheRestore = result.waitForSheatheRestore || src.waitForSheatheRestore;
            result.waitForPendingRestore = result.waitForPendingRestore || src.waitForPendingRestore;
            result.waitForPowerRestore = result.waitForPowerRestore || src.waitForPowerRestore;
            result.finalizeAfterController = result.finalizeAfterController || src.finalizeAfterController;
            result.resetShoutAfterController = result.resetShoutAfterController || src.resetShoutAfterController;
        };

        auto handlePressAutocast = [&](Hand h) {
            auto& hm = ModeFor(h);
            if (hm.autoActive && !hm.finished && hm.chargeComplete && hm.pressActive &&
                hm.autoCastPhase != AutoCastPhase::Done) {
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.autoCastPhase = AutoCastPhase::Done;
            }
        };

        auto handleAutoComplete = [&](Hand h) {
            auto& hm = ModeFor(h);
            if (hm.autoActive && !hm.finished && hm.chargeComplete && !hm.pressActive &&
                hm.autoCastPhase != AutoCastPhase::Done) {
                const float finished = FinishHand(h);
                if (finished != -1.f) {
                    if (IsLeft(h))
                        result.leftAttack = StopDispatchIntent{finished};
                    else
                        result.rightAttack = StopDispatchIntent{finished};
                }
            }
        };

        handlePressAutocast(Left);
        handlePressAutocast(Right);
        handleAutoComplete(Left);
        handleAutoComplete(Right);

        if (_left.holdFiredAndWaitingCastStop && !_left.finished) {
            const float finishedL = FinishHand(Left);
            if (finishedL != -1.f) result.leftAttack = StopDispatchIntent{finishedL};
        }

        if (_right.holdFiredAndWaitingCastStop && !_right.finished) {
            const float finishedR = FinishHand(Right);
            if (finishedR != -1.f) result.rightAttack = StopDispatchIntent{finishedR};
        }

        merge(TryFinalizeExit());
        return result;
    }

    StateExitResult MagicState::OnShoutStop() {
        StateExitResult result{};

        if (!_session.active || _shout.modeShoutID == 0 || _shout.finished) return result;
        if (_shout.isPower) return result;

        MAGIC_DEBUG_LOG("[State] OnShoutStop: modeShoutID={:#010x} waitingStopEvent={}", _shout.modeShoutID,
                        _shout.waitingStopEvent);

        const auto ss = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
        if (!ss) return result;

        const bool isHold = (ss->mode == ActivationMode::Hold);
        const bool isAuto = (ss->mode == ActivationMode::Automatic);

        auto merge = [&](StateExitResult&& src) {
            if (!result.leftAttack) result.leftAttack = src.leftAttack;
            if (!result.rightAttack) result.rightAttack = src.rightAttack;
            if (!result.shout) result.shout = src.shout;

            if (!result.restorePlan && src.restorePlan) {
                result.restorePlan = std::move(src.restorePlan);
            }

            result.waitForSheatheRestore = result.waitForSheatheRestore || src.waitForSheatheRestore;
            result.waitForPendingRestore = result.waitForPendingRestore || src.waitForPendingRestore;
            result.waitForPowerRestore = result.waitForPowerRestore || src.waitForPowerRestore;
            result.finalizeAfterController = result.finalizeAfterController || src.finalizeAfterController;
            result.resetShoutAfterController = result.resetShoutAfterController || src.resetShoutAfterController;
        };

        if (isAuto || _shout.waitingStopEvent || isHold) {
            if (isHold && !_shout.waitingStopEvent) {
                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    result.shout = StopDispatchIntent{held};
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }
            }

            _shout.waitingStopEvent = false;
            _shout.finished = true;

            merge(TryFinalizeExit());
        }

        return result;
    }

    PumpCastPhaseResult MagicState::PumpCastPhase(Hand hand, float dt) {
        PumpCastPhaseResult result{};
        auto& hm = ModeFor(hand);

        if (!_session.active || hm.finished) {
            hm.pendingRestartNextFrame = false;
            return result;
        }

        // Total time without a stable cast, including deferred-restart frames; restarts never reset it (spec 005, R7).
        if (IsPluginCast(hm) && hm.autoCastPhase == AutoCastPhase::StartRequested)
            hm.startFailSecs += dt > 0.f ? dt : 0.f;

        if (hm.pendingRestartNextFrame) {
            if (_session.isDualCasting) {
                auto* p = GetPlayer();
                const auto s = IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand
                                            : RE::MagicSystem::CastingSource::kRightHand;
                const auto* c = p ? GetMagicCaster(p, s) : nullptr;
                if (c && std::to_underlying(c->state.get()) >= std::to_underlying(RE::MagicCaster::State::kUnk02)) {
                    hm.pendingRestartNextFrame = false;
                    hm.startRequestSecs = 0.f;
                    hm.autoCastPhase = AutoCastPhase::Casting;
                    hm.waitingChargeComplete = true;
                    hm.castingElapsedSecs = 0.f;
                    hm.needsManualFireInKReady = true;
                    MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} restart skipped — caster in kUnk02, confirming cast",
                                    IsLeft(hand) ? "L" : "R");
                    return result;
                }
            }
            hm.pendingRestartNextFrame = false;
            _aa.Held(hand) = true;
            _aa.Secs(hand) = 0.f;
            hm.startRequestSecs = 0.f;
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} deferred restart → DOWN", IsLeft(hand) ? "Left" : "Right");
            result.startAttack = true;
            return result;
        }

        const bool isAutoOrHold = hm.autoActive || (hm.holdActive && hm.wantAutoAttack);
        if (!isAutoOrHold && !hm.holdFiredAndWaitingCastStop) {
            if (hm.autoCastPhase != AutoCastPhase::Idle && hm.autoCastPhase != AutoCastPhase::Done) {
                MAGIC_DEBUG_LOG(
                    "[FLOW] PumpCastPhase: hand={} UNEXPECTED SKIP phase={} autoActive={} holdActive={} wantAuto={} "
                    "holdFired={}",
                    IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), hm.autoActive, hm.holdActive,
                    hm.wantAutoAttack, hm.holdFiredAndWaitingCastStop);
            }
            return result;
        }

        if (hm.autoCastPhase == AutoCastPhase::Idle || hm.autoCastPhase == AutoCastPhase::Done) return result;

        auto* player = GetPlayer();
        if (!player) return result;

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
        const auto* caster = GetMagicCaster(player, src);
        if (!caster) return result;

        const auto castStateEnum = caster->state.get();
        const auto casterState = std::to_underlying(castStateEnum);

#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
        static float s_lastPumpLog[2] = {-2.f, -2.f};
        const int hi = IsLeft(hand) ? 0 : 1;
        const float secs = _aa.Secs(hand);
        const bool shouldLog = (secs < s_lastPumpLog[hi]) || (secs - s_lastPumpLog[hi] >= 1.0f);
        if (shouldLog) s_lastPumpLog[hi] = secs;
        if (shouldLog) {
            MAGIC_DEBUG_LOG(
                "[FLOW] PumpCastPhase: hand={} phase={} casterState={} aaHeld={} secs={:.2f} chargeComplete={}",
                handStr, static_cast<int>(hm.autoCastPhase), casterState, _aa.Held(hand), secs, hm.chargeComplete);
        }
#endif

        const bool castIsStable =
            std::to_underlying(castStateEnum) >= std::to_underlying(RE::MagicCaster::State::kUnk02);
        if (hm.autoCastPhase == AutoCastPhase::StartRequested && castIsStable) {
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} StartRequested → Casting (state={})", handStr, casterState);
            ConfirmAutoCastStarted(hand);
        }

        constexpr float kRedispatchInterval = 0.15f;
        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            const float prev = hm.startRequestSecs;
            hm.startRequestSecs += dt > 0.f ? dt : 0.f;
            if (hm.startRequestSecs >= kRedispatchInterval &&
                static_cast<int>(hm.startRequestSecs / kRedispatchInterval) >
                    static_cast<int>(prev / kRedispatchInterval)) {
                const Hand other = IsLeft(hand) ? Hand::Right : Hand::Left;
                const bool otherIsCharging =
                    _session.isDualCasting && ModeFor(other).autoCastPhase > AutoCastPhase::StartRequested;
                if (castIsStable) {
                    if (_session.isDualCasting) {
                        hm.startRequestSecs = 0.f;
                        hm.autoCastPhase = AutoCastPhase::Casting;
                        hm.waitingChargeComplete = true;
                        hm.castingElapsedSecs = 0.f;
                        hm.needsManualFireInKReady = true;
                        MAGIC_DEBUG_LOG(
                            "[FLOW] PumpCastPhase: hand={} timeout suppressed — caster in kUnk02, confirming cast",
                            IsLeft(hand) ? "L" : "R");
                    } else {
                        hm.startRequestSecs = 0.f;
                        MAGIC_DEBUG_LOG(
                            "[FLOW] PumpCastPhase: hand={} timeout — kUnk02 single-hand, resetting timer",
                            IsLeft(hand) ? "L" : "R");
                    }
                } else if (otherIsCharging) {
                    hm.startRequestSecs = 0.f;
                } else {
                    MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} StartRequested for {:.2f}s → UP (DOWN next frame)",
                                    IsLeft(hand) ? "L" : "R", hm.startRequestSecs);
                    _aa.Held(hand) = false;
                    _aa.Secs(hand) = 0.f;
                    hm.pendingRestartNextFrame = true;
                    result.releaseAttack = true;
                }
            }
        }

        if (hm.autoCastPhase == AutoCastPhase::Casting) {
            hm.castingElapsedSecs += dt > 0.f ? dt : 0.f;
            // A cast that lasts past the spurious window is real: the attempt succeeded (R7).
            if (hm.castingElapsedSecs >= kSpuriousInterruptWindowSecs) hm.startFailSecs = 0.f;
        }

        if (hm.autoActive && hm.autoCastPhase == AutoCastPhase::Casting && hm.waitingChargeComplete) {
            const auto id = (_session.activeSlot >= 0) ? Slots::GetSlotSpell(_session.activeSlot, hand) : 0;
            const auto* spell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;

            const bool skipCh = Config::MagicConfigAdapter::Get().SkipChanneling();
            bool chargeComplete = false;
            if (spell) {
                // Skip Channeling: the domain only decides; MagicAction::ForceChargeComplete performs the game write
                // (Constitution I). The cost the skipped charge leaves unpaid is recorded and charged only after the
                // spell fires (OnSpellFired): draining it during the charge makes the game cancel the cast (R1, R2).
                const bool eligible = skipCh && !hm.chargeSkipped && castStateEnum == RE::MagicCaster::State::kUnk02 &&
                                      spell->GetChargeTime() > 0.f && IsSkipChannelingType(DetectSpellType(spell));
                if (eligible && _session.isDualCasting) {
                    // Dual cast: PumpAutomatic forces both hands in the same frame, or neither (R11, FR-012).
                    result.chargeSkipEligible = true;
                } else if (eligible) {
                    hm.chargeSkipped = true;
                    hm.skipMagickaOwed =
                        SkippedChargeCost(caster, spell->GetChargeTime(), GetSpellMagickaCost(player, spell));
                    result.forceChargeComplete = true;
                    MAGIC_DEBUG_LOG(
                        "[FLOW] SkipChanneling: applied hand={} spell={:#010x} '{}' chargeTime={:.2f} "
                        "castingTimer={:.3f} cost={:.1f} casterCost={:.1f} owed={:.1f} magicka={:.1f}",
                        IsLeft(hand) ? "L" : "R", spell->GetFormID(), spell->GetName(), spell->GetChargeTime(),
                        caster->castingTimer, GetSpellMagickaCost(player, spell), caster->currentSpellCost,
                        hm.skipMagickaOwed, GetPlayerMagicka(player));
                } else {
                    chargeComplete = IsChargeComplete(caster, spell);
                }
            }

            if (chargeComplete) {
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} charge complete (skipCh={}) → WaitingChargeRelease",
                                handStr, skipCh);
                hm.autoCastPhase = AutoCastPhase::WaitingChargeRelease;
                hm.waitingChargeComplete = false;
                hm.chargeComplete = true;

                if (_aa.Held(hand)) {
                    const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
                    result.stopAttack = StopDispatchIntent{held};
                    _aa.Held(hand) = false;
                    _aa.Secs(hand) = 0.f;
                } else if (hm.needsManualFireInKReady) {
                    hm.needsManualFireInKReady = false;
                    result.stopAttack = StopDispatchIntent{0.1f};
                    MAGIC_DEBUG_LOG(
                        "[FLOW] PumpCastPhase: hand={} needsManualFireInKReady → stopAttack to fire from kReady",
                        handStr);
                }
            }
        }

        const bool autoModeEnded =
            hm.autoActive &&
            (hm.autoCastPhase == AutoCastPhase::Casting || hm.autoCastPhase == AutoCastPhase::WaitingChargeRelease) &&
            casterState == 0;
        const bool holdModeEnded = hm.holdFiredAndWaitingCastStop && casterState == 0;

        if (autoModeEnded || holdModeEnded) {
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} cast ended (state=0 phase={} holdFired={})", handStr,
                            static_cast<int>(hm.autoCastPhase), hm.holdFiredAndWaitingCastStop);

            if (hm.pressActive) {
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.autoCastPhase = AutoCastPhase::Done;
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} press-autocast done, staying equipped", handStr);
            } else {
                const float finished = FinishHand(hand);
                if (finished != -1.f) result.stopAttack = StopDispatchIntent{finished};
            }
        }

        return result;
    }

    StateExitResult MagicState::PumpSpellFireFinalize(float dt) {
        StateExitResult result{};

        if (!_session.active) {
            _left.waitingSpellFireFinalize = false;
            _left.spellFireFinalizeSecs = 0.f;
            _right.waitingSpellFireFinalize = false;
            _right.spellFireFinalizeSecs = 0.f;
            return result;
        }

        auto merge = [&](StateExitResult&& src) {
            if (!result.leftAttack) result.leftAttack = src.leftAttack;
            if (!result.rightAttack) result.rightAttack = src.rightAttack;
            if (!result.shout) result.shout = src.shout;

            if (!result.restorePlan && src.restorePlan) {
                result.restorePlan = std::move(src.restorePlan);
            }

            result.waitForSheatheRestore = result.waitForSheatheRestore || src.waitForSheatheRestore;
            result.waitForPendingRestore = result.waitForPendingRestore || src.waitForPendingRestore;
            result.waitForPowerRestore = result.waitForPowerRestore || src.waitForPowerRestore;
            result.finalizeAfterController = result.finalizeAfterController || src.finalizeAfterController;
            result.resetShoutAfterController = result.resetShoutAfterController || src.resetShoutAfterController;
        };

        constexpr float kSpellFireFinalizeDelay = 0.7f;

        auto pumpOne = [&](Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingSpellFireFinalize) return;

            hm.spellFireFinalizeSecs += dt > 0.f ? dt : 0.f;

            bool casterIdle = true;
            if (auto* player = GetPlayer()) {
                const auto src = IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand
                                              : RE::MagicSystem::CastingSource::kRightHand;
                if (const auto* caster = GetMagicCaster(player, src)) {
                    casterIdle = caster->state.get() == RE::MagicCaster::State::kNone;
                }
            }

            MAGIC_DEBUG_LOG("[FLOW] PumpSpellFireFinalize: hand={} timer={:.3f}/{:.3f} casterIdle={}",
                            IsLeft(hand) ? "Left" : "Right", hm.spellFireFinalizeSecs, kSpellFireFinalizeDelay,
                            casterIdle);

            if (!casterIdle && hm.spellFireFinalizeSecs < kSpellFireFinalizeDelay) return;

            MAGIC_DEBUG_LOG("[FLOW] PumpSpellFireFinalize: hand={} → TryFinalizeExit (casterIdle={} timer={:.3f})",
                            IsLeft(hand) ? "Left" : "Right", casterIdle, hm.spellFireFinalizeSecs);
            hm.waitingSpellFireFinalize = false;
            hm.spellFireFinalizeSecs = 0.f;

            merge(TryFinalizeExit());
        };

        using enum Hand;
        pumpOne(Left);
        pumpOne(Right);

        return result;
    }

    SpellFiredResult MagicState::OnSpellFired(Hand hand) {
        SpellFiredResult result{};

        if (!_session.active) return result;

        auto& hm = ModeFor(hand);

        if (hm.chargeSkipped) {
            // The spell fired: charge the cost the skipped charge left unpaid (R2). In dual cast the first fire event
            // pays for both hands, because FinishHand below clears the other hand before its own event arrives.
            float owed = std::exchange(hm.skipMagickaOwed, 0.f);
            if (_session.isDualCasting) {
                owed += std::exchange(ModeFor(IsLeft(hand) ? Hand::Right : Hand::Left).skipMagickaOwed, 0.f);
            }
            result.magickaToDrain = owed;
            MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: fired hand={} magicka={:.1f} owed={:.1f}", IsLeft(hand) ? "L" : "R",
                            GetPlayerMagicka(GetPlayer()), owed);
        }

        MAGIC_DEBUG_LOG(
            "[State] OnSpellFired: hand={} autoActive={} chargeComplete={} pressActive={} finished={} isDual={}",
            IsLeft(hand) ? "Left" : "Right", hm.autoActive, hm.chargeComplete, hm.pressActive, hm.finished,
            _session.isDualCasting);

        if (!(hm.autoActive && !hm.finished && hm.chargeComplete)) return result;

        if (hm.pressActive) {
            hm.autoActive = false;
            hm.chargeComplete = false;
            hm.waitingChargeComplete = false;
            hm.autoCastPhase = AutoCastPhase::Done;
            return result;
        }

        if (_session.isDualCasting) {
            using enum IntegratedMagic::Hand;

            const float finishedL = FinishHand(Left);
            if (finishedL != -1.f) {
                result.leftAttack = SpellFiredResult::StopHandDispatchIntent{Left, finishedL};
            }

            const float finishedR = FinishHand(Right);
            if (finishedR != -1.f) {
                result.rightAttack = SpellFiredResult::StopHandDispatchIntent{Right, finishedR};
            }

            _session.isDualCasting = false;

            result.finalizeLeft = true;
            result.finalizeRight = true;
        } else {
            const float finished = FinishHand(hand);
            if (finished != -1.f) {
                if (IsLeft(hand))
                    result.leftAttack = SpellFiredResult::StopHandDispatchIntent{hand, finished};
                else
                    result.rightAttack = SpellFiredResult::StopHandDispatchIntent{hand, finished};
            }

            if (IsLeft(hand))
                result.finalizeLeft = true;
            else
                result.finalizeRight = true;
        }

        return result;
    }

    void MagicState::ScheduleSpellFireFinalize(Hand hand) {
        auto& hm = ModeFor(hand);
        hm.waitingSpellFireFinalize = true;
        hm.spellFireFinalizeSecs = 0.f;
    }

    PumpAutomaticResult MagicState::PumpAutomatic(float dt) {
        PumpAutomaticResult result{};

        auto mergeExit = [&](StateExitResult&& src) {
            if (src.leftAttack && !result.stopLeftAttack)
                result.stopLeftAttack = StopDispatchIntent{src.leftAttack->heldSecs};

            if (src.rightAttack && !result.stopRightAttack)
                result.stopRightAttack = StopDispatchIntent{src.rightAttack->heldSecs};

            if (src.shout && !result.stopShout) result.stopShout = StopDispatchIntent{src.shout->heldSecs};

            if (!result.restorePlan && src.restorePlan) result.restorePlan = std::move(src.restorePlan);

            result.finalizeAfterExecution = result.finalizeAfterExecution || src.finalizeAfterController;

            result.resetShoutAfterExecution = result.resetShoutAfterExecution || src.resetShoutAfterController;
        };

        auto mergeForceExit = [&](StateExitResult&& src) {
            if (src.leftAttack && !result.stopLeftAttack)
                result.stopLeftAttack = StopDispatchIntent{src.leftAttack->heldSecs};

            if (src.rightAttack && !result.stopRightAttack)
                result.stopRightAttack = StopDispatchIntent{src.rightAttack->heldSecs};

            if (src.shout && !result.stopShout) result.stopShout = StopDispatchIntent{src.shout->heldSecs};

            if (!result.restorePlan && src.restorePlan) result.restorePlan = std::move(src.restorePlan);

            result.finalizeAfterExecution = result.finalizeAfterExecution || src.finalizeAfterController;

            result.resetShoutAfterExecution = result.resetShoutAfterExecution || src.resetShoutAfterController;
        };

        auto mergePumpPhase = [&](Hand hand, const PumpCastPhaseResult& src) {
            if (src.startAttack) {
                if (IsLeft(hand))
                    result.startLeftAttack = true;
                else
                    result.startRightAttack = true;
            }
            if (src.releaseAttack) {
                if (IsLeft(hand))
                    result.releaseLeftAttack = true;
                else
                    result.releaseRightAttack = true;
            }
            if (src.forceChargeComplete) {
                if (IsLeft(hand))
                    result.forceChargeLeft = true;
                else
                    result.forceChargeRight = true;
            }
            if (src.stopAttack) {
                if (IsLeft(hand)) {
                    if (!result.stopLeftAttack) result.stopLeftAttack = StopDispatchIntent{src.stopAttack->heldSecs};
                } else {
                    if (!result.stopRightAttack) result.stopRightAttack = StopDispatchIntent{src.stopAttack->heldSecs};
                }
            }
        };

        if (_restore.pendingPowerRestore) {
            if (_restore.pendingPowerRestoreDelaySecs > 0.f) {
                _restore.pendingPowerRestoreDelaySecs -= dt > 0.f ? dt : 0.f;
                return result;
            }

            MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingPowerRestore -> RestoreSnapshot");

            _restore.pendingPowerRestore = false;
            _restore.pendingPowerRestoreDelaySecs = 0.f;

            if (auto* player = GetPlayer()) {
                auto plan = BuildRestoreSnapshotPlan(player);
                if (plan.valid) {
                    result.restorePlan = std::move(plan);
                    result.finalizeAfterExecution = true;
                }
            }

            return result;
        }

        if (_restore.pendingRestoreAfterSheathe) {
            if (auto* player = GetPlayer()) {
                _restore.sheatheWaitSecs += dt > 0.f ? dt : 0.f;

                const auto ws = player->AsActorState()->GetWeaponState();
                const bool sheatheInProgress =
                    ws == RE::WEAPON_STATE::kWantToSheathe || ws == RE::WEAPON_STATE::kSheathing;
                const bool timedOut =
                    _restore.sheatheWaitSecs >= RestoreContext::kSheatheWaitTimeoutSec && !sheatheInProgress;
                const bool giveUp =
                    player->IsInCombat() || ws == RE::WEAPON_STATE::kWantToDraw || timedOut;

                if (_restore.sheatheAnimComplete || giveUp) {
                    _restore.sheatheWaitSecs = 0.f;

                    MAGIC_DEBUG_LOG(
                        "[State] PumpAutomatic: pendingRestoreAfterSheathe -> restore (giveUp={} ws={})", giveUp,
                        static_cast<int>(std::to_underlying(ws)));

                    _restore.pendingRestoreAfterSheathe = false;
                    _restore.sheatheAnimComplete = false;

                    auto plan = BuildRestoreSnapshotPlan(player);
                    if (plan.valid) {
                        result.restorePlan = std::move(plan);
                        result.finalizeAfterExecution = true;
                    }

                    ResetSessionState();
                }
            }

            return result;
        }

        if (_restore.pendingRestore) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingRestore -> RestoreSnapshot + deactivate");

            _restore.pendingRestore = false;

            if (auto* player = GetPlayer()) {
                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    result.stopShout = StopDispatchIntent{held};
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }

                auto plan = BuildRestoreSnapshotPlan(player);
                if (plan.valid) {
                    result.restorePlan = std::move(plan);
                    result.finalizeAfterExecution = true;
                }
            }

            ResetSessionState();
            return result;
        }

        using enum Hand;

        // Retry bound (spec 005, Q2, R7): a plugin-cast hand with no stable cast after kMaxCastStartSecs ends the
        // session through the same exit as the safety timeout. ForceExit ends both hands in dual cast.
        for (const Hand h : {Left, Right}) {
            const auto& hm = ModeFor(h);
            if (_session.active && IsPluginCast(hm) && hm.autoCastPhase == AutoCastPhase::StartRequested &&
                hm.startFailSecs >= kMaxCastStartSecs) {
                MAGIC_DEBUG_LOG("[FLOW] PumpAutomatic: hand={} cast did not start in {:.2f}s -> ForceExit",
                                IsLeft(h) ? "L" : "R", hm.startFailSecs);
                mergeForceExit(ForceExit());
                return result;
            }
        }

        const auto leftPhase = PumpCastPhase(Left, dt);
        const auto rightPhase = PumpCastPhase(Right, dt);
        mergePumpPhase(Left, leftPhase);
        mergePumpPhase(Right, rightPhase);

        // Skip Channeling in dual cast: force both hands in the same frame, or neither (R11, FR-012, Constitution IV).
        if (_session.isDualCasting && leftPhase.chargeSkipEligible && rightPhase.chargeSkipEligible) {
            _left.chargeSkipped = true;
            _right.chargeSkipped = true;
            result.forceChargeLeft = true;
            result.forceChargeRight = true;
            if (auto* player = GetPlayer()) {
                const auto* cL = GetMagicCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
                const auto* cR = GetMagicCaster(player, RE::MagicSystem::CastingSource::kRightHand);
                const auto id = (_session.activeSlot >= 0) ? Slots::GetSlotSpell(_session.activeSlot, Left) : 0;
                const auto* sp = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;
                if (sp) {
                    // Each hand owes half of the dual-cast cost (cost × dual multiplier, as SlotCostUtil computes it).
                    const float handCost =
                        GetSpellMagickaCost(player, sp) * GetDualCastCostMultiplier(player, sp) * 0.5f;
                    _left.skipMagickaOwed = SkippedChargeCost(cL, sp->GetChargeTime(), handCost);
                    _right.skipMagickaOwed = SkippedChargeCost(cR, sp->GetChargeTime(), handCost);
                }
                MAGIC_DEBUG_LOG(
                    "[FLOW] SkipChanneling: applied dual L+R spell={:#010x} '{}' castingTimerL={:.3f} "
                    "castingTimerR={:.3f} casterCostL={:.1f} casterCostR={:.1f} owedL={:.1f} owedR={:.1f} "
                    "magicka={:.1f}",
                    id, sp ? sp->GetName() : "<null>", cL ? cL->castingTimer : -1.f, cR ? cR->castingTimer : -1.f,
                    cL ? cL->currentSpellCost : -1.f, cR ? cR->currentSpellCost : -1.f, _left.skipMagickaOwed,
                    _right.skipMagickaOwed, GetPlayerMagicka(player));
            }
        }

        if (_session.isDualCasting) {
            auto syncForRestart = [&](bool thisReleasing, bool& otherReleasing, Hand other) {
                if (!thisReleasing || otherReleasing) return;
                auto& otherHm = ModeFor(other);
                if (otherHm.finished || otherHm.autoCastPhase != AutoCastPhase::StartRequested) return;
                if (!_aa.Held(other) || otherHm.pendingRestartNextFrame) return;
                _aa.Held(other) = false;
                _aa.Secs(other) = 0.f;
                otherHm.startRequestSecs = 0.f;
                otherHm.pendingRestartNextFrame = true;
                otherReleasing = true;
                MAGIC_DEBUG_LOG("[FLOW] PumpAutomatic: dual-cast sync → {} restarting, syncing {} UP (DOWN next frame)",
                                IsLeft(other) ? "Right" : "Left", IsLeft(other) ? "Left" : "Right");
            };
            syncForRestart(result.releaseLeftAttack, result.releaseRightAttack, Right);
            syncForRestart(result.releaseRightAttack, result.releaseLeftAttack, Left);
        }

        mergeExit(PumpSpellFireFinalize(dt));

        if (!_session.active) return result;

        {
            static float sDumpAccum = 0.f;
            sDumpAccum += dt > 0.f ? dt : 0.f;
            if (sDumpAccum >= 2.0f) {
                sDumpAccum = 0.f;
                if ((_left.autoActive && !_left.finished) || (_right.autoActive && !_right.finished)) {
                    MAGIC_DEBUG_LOG(
                        "[State] PumpAutomatic: HEARTBEAT slot={} "
                        "L(auto={} charge={} charged={} aaHeld={} secs={:.1f} phase={}) "
                        "R(auto={} charge={} charged={} aaHeld={} secs={:.1f} phase={})",
                        _session.activeSlot, _left.autoActive, _left.waitingChargeComplete, _left.chargeComplete,
                        _aa.heldLeft, _aa.secsLeft, static_cast<int>(_left.autoCastPhase), _right.autoActive,
                        _right.waitingChargeComplete, _right.chargeComplete, _aa.heldRight, _aa.secsRight,
                        static_cast<int>(_right.autoCastPhase));
                }
            }
        }

        if (ShouldForceInterrupt()) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: ShouldForceInterrupt -> ForceExit");
            mergeForceExit(ForceExit());
            return result;
        }

        _session.activeTimeoutSecs += dt > 0.f ? dt : 0.f;
        if (_session.activeTimeoutSecs > kMaxActiveTimeoutSecs) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: TIMEOUT -> ForceExit");
            mergeForceExit(ForceExit());
            return result;
        }

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.held && !_shout.finished) {
            const auto ss = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
            if (ss && ss->mode == ActivationMode::Automatic) {
                constexpr float kPowerAutoDuration = 0.2f;
                _shout.powerAutoSecs += dt > 0.f ? dt : 0.f;

                MAGIC_DEBUG_LOG("[State] PumpAutomatic: power auto secs={:.3f}/{:.3f}", _shout.powerAutoSecs,
                                kPowerAutoDuration);

                if (_shout.powerAutoSecs >= kPowerAutoDuration) {
                    MAGIC_DEBUG_LOG("[State] PumpAutomatic: power auto duration elapsed -> StopShoutPress + finish");

                    if (_shout.held) {
                        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                        result.stopShout = StopDispatchIntent{held};
                        _shout.held = false;
                        _shout.heldSecs = 0.f;
                    }

                    _shout.finished = true;
                    mergeExit(TryFinalizeExit());
                }
            }
        }

        return result;
    }

    void MagicState::OnCasterStartCast(Hand hand, const RE::MagicItem* spell,
                                       RE::MagicSystem::CastingType
#ifdef DEBUG
                                           type
#endif
    ) {
        if (!_session.active) return;

        auto& hm = ModeFor(hand);
        const auto* expected = IsLeft(hand) ? _session.modeSpellLeft : _session.modeSpellRight;
        if (!expected || spell != expected) return;

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return;
        if (hm.finished) return;

        MAGIC_DEBUG_LOG("[State] OnCasterStartCast: hand={} phase={} type={} spell={:#010x}",
                        IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), static_cast<int>(type),
                        spell ? spell->GetFormID() : 0u);

        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            ConfirmAutoCastStarted(hand);
        }
    }

    CastInterruptResult MagicState::OnCasterInterrupt(Hand hand, const RE::MagicItem* spell,
                                                      bool
#ifdef DEBUG
                                                          depleteEnergy
#endif
    ) {
        CastInterruptResult result;
        if (!_session.active) return result;

        auto& hm = ModeFor(hand);
        const auto* expected = IsLeft(hand) ? _session.modeSpellLeft : _session.modeSpellRight;
        if (!expected) return result;
        if (spell && spell != expected) return result;

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return result;
        if (hm.finished) return result;

        if (hm.autoCastPhase != AutoCastPhase::StartRequested && hm.autoCastPhase != AutoCastPhase::Casting)
            return result;

        MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} phase={} depleteEnergy={} castingSecs={:.3f}",
                        IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), depleteEnergy,
                        hm.castingElapsedSecs);

        // Skip Channeling forced this charge: an interruption now is a real failure, never "spurious".
        // Retrying would repeat it (the restart loop), so finish the hand(s) and let the finalize path restore (R3).
        if (hm.chargeSkipped) {
            MAGIC_DEBUG_LOG("[FLOW] SkipChanneling: interrupted after skip hand={} magicka={:.1f} -> finish (no retry)",
                            IsLeft(hand) ? "L" : "R", GetPlayerMagicka(GetPlayer()));
            FinishAndFinalize(hand, result);
            if (_session.isDualCasting) {
                const Hand other = IsLeft(hand) ? Hand::Right : Hand::Left;
                if (!ModeFor(other).finished) FinishAndFinalize(other, result);
            }
            return result;
        }

        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            hm.pendingRestartNextFrame = true;
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} pre-cast spurious → UP (DOWN next frame)",
                            IsLeft(hand) ? "Left" : "Right");
            if (IsLeft(hand))
                result.releaseLeft = true;
            else
                result.releaseRight = true;

            if (_session.isDualCasting) {
                const Hand other = IsLeft(hand) ? Hand::Right : Hand::Left;
                auto& otherHm = ModeFor(other);
                if (!otherHm.finished && otherHm.autoCastPhase == AutoCastPhase::StartRequested &&
                    _aa.Held(other) && !otherHm.pendingRestartNextFrame) {
                    _aa.Held(other) = false;
                    _aa.Secs(other) = 0.f;
                    otherHm.startRequestSecs = 0.f;
                    otherHm.pendingRestartNextFrame = true;
                    MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: dual-cast sync → also releasing {} UP (DOWN next frame)",
                                    IsLeft(other) ? "Left" : "Right");
                    if (IsLeft(other))
                        result.releaseLeft = true;
                    else
                        result.releaseRight = true;
                }
            }

            return result;
        }

        const bool isSpurious = (hm.castingElapsedSecs < kSpuriousInterruptWindowSecs);

        if (isSpurious) {
            if (hm.needsManualFireInKReady) {
                MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} needsManualFire spurious → ignored",
                                IsLeft(hand) ? "Left" : "Right");
                return result;
            }
            hm.autoCastPhase = AutoCastPhase::StartRequested;
            hm.waitingChargeComplete = false;
            hm.chargeComplete = false;
            hm.castingElapsedSecs = 0.f;
            hm.chargeSkipped = false;
            hm.skipMagickaOwed = 0.f;
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            hm.pendingRestartNextFrame = true;
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} post-cast spurious → UP (DOWN next frame)",
                            IsLeft(hand) ? "Left" : "Right");
            if (IsLeft(hand))
                result.releaseLeft = true;
            else
                result.releaseRight = true;
            return result;
        }

        // A real interruption: finish the hand (and its dual-cast partner) and schedule the finalize, so the session
        // ends once nothing is left instead of staying active until the timeout or the release (spec 005, R12).
        MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} real interrupt -> finish + finalize session",
                        IsLeft(hand) ? "Left" : "Right");
        FinishAndFinalize(hand, result);
        if (_session.isDualCasting) {
            const Hand other = IsLeft(hand) ? Hand::Right : Hand::Left;
            if (!ModeFor(other).finished) FinishAndFinalize(other, result);
        }
        return result;
    }

    void MagicState::FinishAndFinalize(Hand hand, CastInterruptResult& result) {
        const float finished = FinishHand(hand);
        if (IsLeft(hand))
            result.finishedLeft = finished;
        else
            result.finishedRight = finished;
        ScheduleSpellFireFinalize(hand);
    }
}
