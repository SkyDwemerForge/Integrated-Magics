#pragma once

#include <optional>

#include "PCH.h"
#include "Shared/DispatchIntents.h"
#include "Shared/Hand.h"
#include "Shared/RestoreSnapshotPlan.h"

namespace IntegratedMagic {

    struct SpellFiredResult {
        struct StopHandDispatchIntent {
            Hand hand{Hand::Right};
            float heldSecs{-1.f};
        };

        std::optional<StopHandDispatchIntent> leftAttack;
        std::optional<StopHandDispatchIntent> rightAttack;
        bool finalizeLeft{false};
        bool finalizeRight{false};
        float magickaToDrain{0.f};  // Skip Channeling: skipped charge cost the controller must drain now
    };

    struct PumpCastPhaseResult {
        bool startAttack{false};
        bool releaseAttack{false};
        bool forceChargeComplete{false};  // single hand: controller must complete this hand's charge now
        bool chargeSkipEligible{false};   // dual cast: hand is eligible; PumpAutomatic pairs both hands
        std::optional<StopDispatchIntent> stopAttack;
    };

    struct PumpResult {
        struct AttackEvent {
            Hand hand{Hand::Right};
            float power{0.f};
            float secsHeld{0.f};
        };

        struct ShoutEvent {
            float power{0.f};
            float secsHeld{0.f};
        };

        std::optional<AttackEvent> leftAttack;
        std::optional<AttackEvent> rightAttack;
        std::optional<ShoutEvent> shout;
    };

    using DisableHandResult = SingleHandStopResult;
    using PrepareOverwriteResult = DualHandStopResult;

    struct CastInterruptResult {
        float finishedLeft{-1.f};
        float finishedRight{-1.f};
        bool releaseLeft{false};
        bool releaseRight{false};
    };

    struct PumpAutomaticResult {
        bool startLeftAttack{false};
        bool startRightAttack{false};
        bool releaseLeftAttack{false};
        bool releaseRightAttack{false};
        bool forceChargeLeft{false};
        bool forceChargeRight{false};

        std::optional<StopDispatchIntent> stopLeftAttack;
        std::optional<StopDispatchIntent> stopRightAttack;
        std::optional<StopDispatchIntent> stopShout;

        std::optional<RestoreSnapshotPlan> restorePlan;

        bool finalizeAfterExecution{false};
        bool resetShoutAfterExecution{false};
    };

}