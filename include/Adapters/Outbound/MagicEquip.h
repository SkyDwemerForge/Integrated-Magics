#pragma once
#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::MagicAction {
    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand, bool skipAnim);
    void ClearHandSpell(RE::PlayerCharacter* player, Hand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand);
    void EquipShoutInVoice(RE::PlayerCharacter* player, RE::TESForm* shoutOrPower);
    void ClearVoiceShout(RE::PlayerCharacter* player);
    void ApplySkipEquipAnimReturn(RE::PlayerCharacter* player, bool skipAnimOnReturn);
    void SetSkipEquipVars(RE::PlayerCharacter* pc, bool enable);
    void ResetSkipEquipToken();
    // Skip Channeling: completes the hand caster's charge (castingTimer = 0). No magicka is deducted here.
    void ForceChargeComplete(RE::PlayerCharacter* player, Hand hand);
    // Skip Channeling: after the spell fired, drains the charge cost the skipped charge left unpaid.
    // Clamped so magicka never goes below 0.
    void DrainMagicka(RE::PlayerCharacter* player, float amount);
}