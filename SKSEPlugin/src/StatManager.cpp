#include "PCH.h"
#include "StatManager.h"

namespace InTheirShoes {

    constexpr RE::ActorValue kCoreStatsAndSkills[] = {
        // Attributes
        RE::ActorValue::kHealth,
        RE::ActorValue::kMagicka,
        RE::ActorValue::kStamina,

        // Combat Skills
        RE::ActorValue::kOneHanded,
        RE::ActorValue::kTwoHanded,
        RE::ActorValue::kArchery,
        RE::ActorValue::kBlock,
        RE::ActorValue::kSmithing,
        RE::ActorValue::kHeavyArmor,

        // Stealth Skills
        RE::ActorValue::kLightArmor,
        RE::ActorValue::kPickpocket,
        RE::ActorValue::kLockpicking,
        RE::ActorValue::kSneak,
        RE::ActorValue::kAlchemy,
        RE::ActorValue::kSpeech,

        // Magic Skills
        RE::ActorValue::kAlteration,
        RE::ActorValue::kConjuration,
        RE::ActorValue::kDestruction,
        RE::ActorValue::kIllusion,
        RE::ActorValue::kRestoration,
        RE::ActorValue::kEnchanting
    };

    std::optional<StatSnapshot> StatManager::CaptureSnapshot(RE::Actor* a_actor) {
        if (!a_actor) return std::nullopt;

        auto* avOwner = a_actor->AsActorValueOwner();
        if (!avOwner) return std::nullopt;

        StatSnapshot snapshot;
        for (auto av : kCoreStatsAndSkills) {
            snapshot.baseValues[static_cast<std::uint32_t>(av)] = avOwner->GetBaseActorValue(av);
        }
        return snapshot;
    }

    bool StatManager::ApplySnapshot(RE::Actor* a_targetActor, const StatSnapshot& a_snapshot) {
        if (!a_targetActor) return false;

        auto* avOwner = a_targetActor->AsActorValueOwner();
        if (!avOwner) return false;

        for (const auto& [avInt, val] : a_snapshot.baseValues) {
            auto av = static_cast<RE::ActorValue>(avInt);
            avOwner->SetBaseActorValue(av, val);
        }

        // Restore the pools to clear damage modifiers, so a smaller donor's pools don't go negative (instant death).
        for (auto poolAV : { RE::ActorValue::kHealth, RE::ActorValue::kMagicka, RE::ActorValue::kStamina }) {
            avOwner->RestoreActorValue(poolAV, 99999.0f);
        }

        SKSE::log::info("Applied {} stat/skill values to actor {:08X}.",
            a_snapshot.baseValues.size(),
            a_targetActor->GetFormID());

        return true;
    }

    ProgressSnapshot StatManager::CaptureProgress(RE::PlayerCharacter* a_player) {
        ProgressSnapshot snapshot;
        auto* base = a_player ? a_player->GetActorBase() : nullptr;
        auto* skills = a_player ? a_player->GetInfoRuntimeData().skills : nullptr;
        if (!base || !skills || !skills->data) return snapshot;

        snapshot.level = base->actorData.level;
        snapshot.perkPoints = a_player->GetGameStatsData().perkCount;
        snapshot.skills = *skills->data;
        for (const auto* rank : a_player->GetPlayerRuntimeData().addedPerks) {
            if (rank && rank->perk) snapshot.perks.push_back(rank->perk->GetFormID());
        }
        return snapshot;
    }

    void StatManager::RestoreProgress(RE::PlayerCharacter* a_player, const ProgressSnapshot& a_snapshot) {
        auto* base = a_player ? a_player->GetActorBase() : nullptr;
        auto* skills = a_player ? a_player->GetInfoRuntimeData().skills : nullptr;
        if (a_snapshot.level == 0 || !base || !skills || !skills->data) return;

        // Perks bought with the session's level-ups. Collected first: RemovePerk changes addedPerks.
        std::vector<RE::BGSPerk*> added;
        for (const auto* rank : a_player->GetPlayerRuntimeData().addedPerks) {
            if (rank && rank->perk && !std::ranges::contains(a_snapshot.perks, rank->perk->GetFormID())) {
                added.push_back(rank->perk);
            }
        }
        for (auto* perk : added) {
            a_player->RemovePerk(perk);
        }

        const auto level = base->actorData.level;
        base->actorData.level = a_snapshot.level;
        a_player->GetGameStatsData().perkCount = a_snapshot.perkPoints;
        *skills->data = a_snapshot.skills;
        SKSE::log::info("Restored the Dragonborn's level {} (was {}), {} perk points; removed {} perks gained while impersonating.",
            a_snapshot.level, level, a_snapshot.perkPoints, added.size());
    }

    bool StatManager::CopyStats(RE::Actor* a_fromActor, RE::Actor* a_toActor) {
        if (!a_fromActor || !a_toActor) {
            SKSE::log::warn("CopyStats called with null actor pointers.");
            return false;
        }

        auto snapshot = CaptureSnapshot(a_fromActor);
        if (!snapshot) {
            SKSE::log::error("Failed to capture stats from actor {:08X}.",
                a_fromActor->GetFormID());
            return false;
        }

        return ApplySnapshot(a_toActor, *snapshot);
    }

}
