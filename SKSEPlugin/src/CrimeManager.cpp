#include "PCH.h"
#include "CrimeManager.h"

namespace InTheirShoes {

    namespace {
        // Current bounty (violent and non-violent) and infamy, per crime faction. The values are written directly,
        // like a save loading them, rather than through Actor::SetCrimeGoldValue, which only sets the current bounty.
        auto& GetCrimeGoldMap(RE::PlayerCharacter* a_player) {
            return a_player->GetCrimeValue().crimeGoldMap;
        }

        void ClearAll(RE::PlayerCharacter* a_player) {
            for (auto& entry : GetCrimeGoldMap(a_player)) {
                entry.second = {};
            }
        }
    }

    void CrimeManager::OnSessionBegin(RE::PlayerCharacter* a_player) {
        if (!a_player) return;
        // A switch to another donor ends the last session first, which put the Dragonborn's bounty back
        _dragonborn.clear();
        for (const auto& entry : GetCrimeGoldMap(a_player)) {
            if (entry.first) _dragonborn.push_back({ const_cast<RE::TESFaction*>(entry.first), entry.second });
        }
        ClearAll(a_player);
        _active = true;
        SKSE::log::info("Set aside the Dragonborn's bounty in {} crime factions.", _dragonborn.size());
    }

    void CrimeManager::OnSessionEnd(RE::PlayerCharacter* a_player) {
        if (!_active || !a_player) {
            Clear();
            return;
        }
        ClearAll(a_player);
        auto& map = GetCrimeGoldMap(a_player);
        for (const auto& [faction, gold] : _dragonborn) {
            if (auto it = map.find(faction); it != map.end()) {
                it->second = gold;
            } else {
                map.emplace(faction, gold);
            }
        }
        SKSE::log::info("Restored the Dragonborn's bounty in {} crime factions.", _dragonborn.size());
        Clear();
    }

    void CrimeManager::Clear() {
        _active = false;
        _dragonborn.clear();
    }

    void CrimeManager::Restore(std::vector<FactionBounty> a_bounty) {
        _dragonborn = std::move(a_bounty);
        _active = true;
    }

}
