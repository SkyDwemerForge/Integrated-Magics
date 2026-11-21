#include "Shared/HandDiagnostics.h"

#include <format>
#include <utility>

#include "PCH.h"
#include "Shared/InventoryUtil.h"

namespace IntegratedMagic {
    namespace {
        const char* DescribeGraphBool(RE::PlayerCharacter* player, const RE::BSFixedString& name) {
            bool value = false;
            if (!player || !player->GetGraphVariableBool(name, value)) return "n/a";
            return value ? "1" : "0";
        }

        std::string DescribeHand(RE::PlayerCharacter* player, bool leftHand) {
            static const RE::BSFixedString kIsEquipping("IsEquipping");
            static const RE::BSFixedString kIsUnequipping("IsUnequipping");
            static const RE::BSFixedString kEquipOk("bEquipOk");
            static const RE::BSFixedString kIsBlocking("IsBlocking");
            static const RE::BSFixedString kIsAttacking("IsAttacking");

            std::string out;

            const RE::TESForm* item = player->GetEquippedObject(leftHand);
            if (item) {
                out = std::format("{}:0x{:08X}", DescribeItemKind(item), item->GetFormID());
            } else {
                out = "empty";
            }

            const auto anim = GetHandAnimType(player, leftHand);
            out += anim ? std::format(" anim={}", *anim) : std::string(" anim=n/a");

            out += std::format(" eq={}/{} ok={} blk={} atk={}", DescribeGraphBool(player, kIsEquipping),
                               DescribeGraphBool(player, kIsUnequipping), DescribeGraphBool(player, kEquipOk),
                               DescribeGraphBool(player, kIsBlocking), DescribeGraphBool(player, kIsAttacking));

            using CS = RE::MagicSystem::CastingSource;
            if (auto const* caster = GetMagicCaster(player, leftHand ? CS::kLeftHand : CS::kRightHand)) {
                out += std::format(" cast={}:0x{:08X}", std::to_underlying(caster->state.get()),
                                   caster->currentSpell ? caster->currentSpell->GetFormID() : 0u);
            } else {
                out += " cast=n/a";
            }
            return out;
        }
    }

    const char* DescribeItemKind(const RE::TESForm* form) {
        if (!form) return "empty";
        if (form->As<RE::SpellItem>()) return "spell";
        if (auto const* weap = form->As<RE::TESObjectWEAP>()) {
            if (weap->IsStaff()) return "staff";
            if (weap->IsBow()) return "bow";
            if (weap->IsCrossbow()) return "xbow";
            if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe()) return "2h";
            return "1h";
        }
        if (auto const* armo = form->As<RE::TESObjectARMO>(); armo && armo->IsShield()) return "shield";
        if (form->As<RE::TESObjectLIGH>()) return "torch";
        return "other";
    }

    std::optional<std::int32_t> GetHandAnimType(RE::PlayerCharacter* player, bool leftHand) {
        static const RE::BSFixedString kLeftHandType("iLeftHandType");
        static const RE::BSFixedString kRightHandType("iRightHandType");

        if (!player) return std::nullopt;
        std::int32_t value = 0;
        if (!player->GetGraphVariableInt(leftHand ? kLeftHandType : kRightHandType, value)) return std::nullopt;
        return value;
    }

    bool IsEquipAnimationPlaying(RE::PlayerCharacter* player) {
        static const RE::BSFixedString kIsEquipping("IsEquipping");

        bool value = false;
        return player && player->GetGraphVariableBool(kIsEquipping, value) && value;
    }

    std::string DescribeHandsForLog(RE::PlayerCharacter* player) {
        if (!player) return "player=null";

        const auto ws = std::to_underlying(player->AsActorState()->GetWeaponState());
        return std::format("ws={} L={} | R={}", static_cast<int>(ws), DescribeHand(player, true),
                           DescribeHand(player, false));
    }
}
