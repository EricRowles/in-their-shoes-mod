#pragma once

#include <unordered_map>

namespace InTheirShoes {

    struct StatSnapshot {
        std::unordered_map<std::uint32_t, float> baseValues;
    };

    // Stateless helpers for copying base attributes and skills between actors.
    // The Dragonborn's original stats live in ImpersonationManager's Baseline.
    namespace StatManager {
        bool CaptureSnapshot(RE::Actor* a_actor, StatSnapshot& a_outSnapshot);
        bool ApplySnapshot(RE::Actor* a_targetActor, const StatSnapshot& a_snapshot);
        bool CopyStats(RE::Actor* a_fromActor, RE::Actor* a_toActor);
    }

}
