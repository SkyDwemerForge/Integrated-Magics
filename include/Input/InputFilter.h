#pragma once
#include "Input/ExclusiveStore.h"
#include "Input/HandButtonStore.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/ReplaySystem.h"
#include "Input/SlotEdgeStore.h"
#include "PCH.h"
#include "Shared/CaptureState.h"
#include "Shared/ReplayTypes.h"

namespace Input::detail {

    [[nodiscard]] bool IsInputBlockedByMenus();
    // The Magic Menu is open: its hotkey keys go through the Magic Menu hold (spec 008).
    [[nodiscard]] bool IsMagicMenuOpen();
    // Maps an input event to the code slot combos store: scancode (keyboard), kMouseButtonBase + id (mouse),
    // gamepad index 0..15, or -1 for an unknown gamepad button (spec 004, data-model "Effective code").
    [[nodiscard]] int EffectiveComboCode(RE::INPUT_DEVICE dev, std::uint32_t rawIdCode);
    [[nodiscard]] ProcessButtonEventsResult ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap,
                                                                bool& wantCapture, KeyStateStore& keys);
    void FilterMouseForPopup(RE::InputEvent** a_evns);
    void FilterEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots,
                      const HotkeyCacheStore& cache, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                      HandButtonStore& hands, const HandGateFlags& flags);
    // Hand-button gate for an event of a hand's attack/block action (spec 005, R3/R4). Returns true to keep the event
    // from the game. Events of other actions always return false.
    [[nodiscard]] bool GateHandEvent(const RE::BSFixedString& userEvent, float value, float heldSecs,
                                     HandButtonStore& hands, const HandGateFlags& flags);
    void UpdateSlotsIfAllowed(bool blocked, float dt, SlotEdgeStore& slots, ExclusiveStore& excl,
                              const HotkeyCacheStore& cache, const KeyStateStore& keys, RetainedArr& retained,
                              DeferredVec& deferred, ReplayArr& replay, bool spellSystemActive, int activeSlot);
}