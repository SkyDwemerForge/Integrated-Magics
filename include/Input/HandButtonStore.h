#pragma once
#include <cstdint>
#include <optional>

#include "Input/ExclusiveStore.h"
#include "Shared/Hand.h"

// Hand-button gate (spec 005): the player's own attack/block button for each hand while the plugin drives a hand's
// cast. Pure logic with no RE types; the event mapping lives in src/Input/InputFilter.cpp.
namespace Input {

    struct HandButtonState {
        // The game sees this hand's attack/block action held by the player; updated only from events that reach the
        // game (spec 005, R4).
        bool gameHeld{false};
        float lastHeldSecs{0.f};
        // None or Consumed: a Consumed press is kept from the game until its release (R4).
        PressFate fate{PressFate::None};
    };

    struct HandButtonStore {
        HandButtonState left{};
        HandButtonState right{};
        // A fresh press asked to end the session (Q1, Q3); SpellSystemController::OnFrame consumes it (R6).
        bool cancelRequested{false};

        [[nodiscard]] HandButtonState& For(IntegratedMagic::Hand h) noexcept {
            return h == IntegratedMagic::Hand::Left ? left : right;
        }
        void ResetHands() noexcept {
            left = {};
            right = {};
        }
    };

    // Built by InputController from MagicState at the start of each frame (data-model "HandGateFlags").
    struct HandGateFlags {
        bool pluginCastLeft{false};
        bool pluginCastRight{false};
        // A hand is plugin-cast and the session is not in Press mode: the blockStart rule (R4).
        bool mayCancel{false};

        [[nodiscard]] bool PluginCast(IntegratedMagic::Hand h) const noexcept {
            return h == IntegratedMagic::Hand::Left ? pluginCastLeft : pluginCastRight;
        }
    };

    enum class HandEventPhase : std::uint8_t { Press, Held, Release };
    enum class HandGateOutcome : std::uint8_t {
        Pass,
        KeptCancel,
        KeptIgnored,
        PassCancel,
        KeptUntilRelease,
        Cleared,
        KeptOrphan
    };

    struct HandGateDecision {
        bool filter{false};
        bool requestCancel{false};
        HandGateOutcome outcome{HandGateOutcome::Pass};
    };

    // Decides one event of a hand's attack/block action (research R4, contract §2).
    [[nodiscard]] inline HandGateDecision DecideHandEvent(HandButtonState& st, bool thisHandPluginCast,
                                                          bool otherHandPluginCast, bool mayCancel,
                                                          HandEventPhase phase, float heldSecs) noexcept {
        HandGateDecision d{};
        if (phase == HandEventPhase::Press) {
            // A fresh press always starts a new fate, so a missed release never swallows it.
            st.fate = PressFate::None;
            const bool cancel = mayCancel && (thisHandPluginCast || otherHandPluginCast);
            d.requestCancel = cancel;
            if (thisHandPluginCast) {
                st.fate = PressFate::Consumed;
                d.filter = true;
                d.outcome = cancel ? HandGateOutcome::KeptCancel : HandGateOutcome::KeptIgnored;
            } else {
                d.outcome = cancel ? HandGateOutcome::PassCancel : HandGateOutcome::Pass;
            }
        } else if (st.fate == PressFate::Consumed) {
            d.filter = true;
            if (phase == HandEventPhase::Release) {
                st.fate = PressFate::None;
                d.outcome = HandGateOutcome::Cleared;
            } else {
                d.outcome = HandGateOutcome::KeptUntilRelease;
            }
        } else if (thisHandPluginCast && !st.gameHeld) {
            d.filter = true;
            d.outcome = HandGateOutcome::KeptOrphan;
        }

        if (!d.filter) {
            st.gameHeld = (phase != HandEventPhase::Release);
            st.lastHeldSecs = heldSecs;
        }
        return d;
    }

    // At the plugin's first press for a hand: if the game sees this action held by the player, take it. The caller
    // sends the game a release first, and the press is then kept until the button is released (R5, contract §3).
    [[nodiscard]] inline std::optional<float> TakeHeldForCast(HandButtonState& st) noexcept {
        if (!st.gameHeld || st.fate == PressFate::Consumed) return std::nullopt;
        st.fate = PressFate::Consumed;
        st.gameHeld = false;
        return st.lastHeldSecs > 0.f ? st.lastHeldSecs : 0.1f;
    }

}
