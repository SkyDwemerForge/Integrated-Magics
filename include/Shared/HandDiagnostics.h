#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "PCH.h"

namespace IntegratedMagic {

    // Read-only queries of the player's hands, for the log (spec 007, contracts/diagnostic-log.md) and the domain.
    // Nothing here changes game state.

    [[nodiscard]] const char* DescribeItemKind(const RE::TESForm* form);

    [[nodiscard]] std::optional<std::int32_t> GetHandAnimType(RE::PlayerCharacter* player, bool leftHand);

    [[nodiscard]] std::string DescribeHandsForLog(RE::PlayerCharacter* player);

    // The graph variable IsEquipping: the game's equip/draw animation is playing. False when the graph has no such
    // variable.
    [[nodiscard]] bool IsEquipAnimationPlaying(RE::PlayerCharacter* player);
}
