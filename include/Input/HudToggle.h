#pragma once
#include <atomic>

#include "Config/InputConstants.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/HotkeyMatcher.h"
#include "Input/KeyStateStore.h"

extern std::atomic_bool g_hudTogglePending;  // NOSONAR

namespace Input::detail {

    // In the Magic Menu, the wheel combo's keys are kept from the menu by the Magic Menu hold (spec 008).
    void UpdateHudToggleState(const HotkeyCacheStore& cache, const KeyStateStore& keys);
}