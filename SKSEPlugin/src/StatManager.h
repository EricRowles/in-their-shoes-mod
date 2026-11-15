#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

namespace InTheirShoes {

    struct StatSnapshot {
        std::unordered_map<std::uint32_t, float> baseValues;
    };

    // The Dragonborn's character progress: level, level and skill XP, perk points and perks. Skill and attribute
    // gains while impersonating belong to the donor (they're in its stats); the Dragonborn's level goes back to
    // this on revert, with the perks bought since.
    struct ProgressSnapshot {
        bool recorded{ false };  // false for a baseline saved before this was kept
        std::uint16_t level{ 0 };
        std::uint8_t perkPoints{ 0 };
        RE::PlayerCharacter::PlayerSkills::Data skills{};
        std::vector<RE::FormID> perks;
    };

    // Stateless helpers for copying base attributes and skills between actors.
    // The Dragonborn's original stats live in ImpersonationManager's Baseline.
    namespace StatManager {
        std::optional<StatSnapshot> CaptureSnapshot(RE::Actor* a_actor);
        bool ApplySnapshot(RE::Actor* a_targetActor, const StatSnapshot& a_snapshot);
        bool CopyStats(RE::Actor* a_fromActor, RE::Actor* a_toActor);

        ProgressSnapshot CaptureProgress(RE::PlayerCharacter* a_player);
        void RestoreProgress(RE::PlayerCharacter* a_player, const ProgressSnapshot& a_snapshot);
    }

}
