#include "PCH.h"
#include "FactionManager.h"
#include "AliasSwapManager.h"
#include "Forms.h"

#include <unordered_map>

namespace InTheirShoes {

    namespace {
        // An actor's memberships. VisitFactions lists the base record's factions, then the runtime changes; a
        // later entry overrides an earlier one, and a rank below 0 means the actor left the faction.
        std::unordered_map<RE::TESFaction*, std::int8_t> GetMemberships(RE::Actor* a_actor) {
            std::unordered_map<RE::TESFaction*, std::int8_t> ranks;
            a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
                if (a_faction) ranks[a_faction] = a_rank;
                return false;
            });
            std::erase_if(ranks, [](const auto& a_entry) { return a_entry.second < 0; });
            return ranks;
        }

        // Whether the faction moves between the Dragonborn and the donor (see FactionManager.h)
        bool IsSwappable(const RE::TESFaction* a_faction) {
            return a_faction && a_faction != Forms::playerFaction && a_faction != Forms::currentFollowerFaction &&
                   a_faction != Forms::playerFollowerFaction &&
                   !AliasSwapManager::GetSingleton().GetDonorAliasFactionRank(a_faction);
        }

        std::vector<FactionManager::FactionRank> GetSwappableMemberships(RE::Actor* a_actor) {
            std::vector<FactionManager::FactionRank> result;
            for (const auto& [faction, rank] : GetMemberships(a_actor)) {
                if (IsSwappable(faction)) result.emplace_back(faction, rank);
            }
            return result;
        }

        bool Contains(const std::vector<FactionManager::FactionRank>& a_list, const RE::TESFaction* a_faction) {
            return std::ranges::find(a_list, a_faction, &FactionManager::FactionRank::first) != a_list.end();
        }
    }

    void FactionManager::CaptureDragonborn(RE::Actor* a_player) {
        _dragonbornFactions = GetSwappableMemberships(a_player);
    }

    void FactionManager::TakeOnDonorFactions(RE::Actor* a_donor, RE::Actor* a_player) {
        // The aliases have moved, so the donor's memberships no longer include their factions
        _givenFactions = GetSwappableMemberships(a_donor);

        std::size_t left = 0;
        for (const auto& [faction, rank] : _dragonbornFactions) {
            if (!Contains(_givenFactions, faction)) {
                a_player->RemoveFromFaction(faction);
                ++left;
            }
        }
        for (const auto& [faction, rank] : _givenFactions) {
            a_player->AddToFaction(faction, rank);
            SKSE::log::info("  player joins donor faction {:08X} ({}) at rank {}", faction->GetFormID(),
                faction->GetName(), rank);
        }
        _active = true;
        SKSE::log::info("Player took on {} donor factions and left {} of the Dragonborn's.", _givenFactions.size(), left);
    }

    void FactionManager::ReturnFactions(RE::Actor* a_donor, RE::Actor* a_player) {
        // A session loaded from a save made before this feature never gave the player the donor's factions
        if (!_active || !a_player) {
            Clear();
            return;
        }

        const auto current = GetSwappableMemberships(a_player);
        if (a_donor) {
            const auto donorRanks = GetMemberships(a_donor);
            for (const auto& [faction, rank] : current) {
                if (auto it = donorRanks.find(faction); it == donorRanks.end() || it->second != rank) {
                    a_donor->AddToFaction(faction, rank);
                    SKSE::log::info("  donor now in faction {:08X} at rank {}", faction->GetFormID(), rank);
                }
            }
            for (const auto& [faction, rank] : _givenFactions) {
                if (!Contains(current, faction)) {
                    a_donor->RemoveFromFaction(faction);
                    SKSE::log::info("  donor left faction {:08X}", faction->GetFormID());
                }
            }
        }

        for (const auto& [faction, rank] : current) {
            if (!Contains(_dragonbornFactions, faction)) {
                a_player->RemoveFromFaction(faction);
            }
        }
        for (const auto& [faction, rank] : _dragonbornFactions) {
            a_player->AddToFaction(faction, rank);
        }
        SKSE::log::info("Returned {} factions to the donor and {} to the Dragonborn.", current.size(), _dragonbornFactions.size());
        Clear();
    }

    void FactionManager::OnDonorFactionChanged(RE::Actor* a_player, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!_active || !a_player || !IsSwappable(a_faction)) return;
        if (a_rank < 0) {
            a_player->RemoveFromFaction(a_faction);
        } else {
            a_player->AddToFaction(a_faction, a_rank);
        }
    }

    void FactionManager::Clear() {
        _active = false;
        _dragonbornFactions.clear();
        _givenFactions.clear();
    }

    void FactionManager::Restore(std::vector<FactionRank> a_dragonborn, std::vector<FactionRank> a_given) {
        _dragonbornFactions = std::move(a_dragonborn);
        _givenFactions = std::move(a_given);
        _active = true;
    }

}
