#pragma once

#include "Singleton.h"
#include <mutex>
#include <optional>
#include <vector>

namespace InTheirShoes {

    // Makes the player a real member of the donor's factions for the session, and takes the Dragonborn out of
    // theirs. This reaches what the engine decides from membership: faction reactions (a bandit's allies are
    // the player's), faction ownership (guild beds and stock), faction conditions and Papyrus IsInFaction /
    // GetFactionRank. The donor's dialogue needs nothing more: its faction conditions ask the player.
    //
    // Two kinds of faction stay put: the Dragonborn's PlayerFaction, and the factions of the donor's swapped
    // aliases, which reach the player with the aliases. Not thread-safe, except GetDragonbornFactionRank.
    class FactionManager : public Singleton<FactionManager> {
    public:
        using FactionRank = std::pair<RE::TESFaction*, std::int8_t>;

        // Before the donor's aliases move to the player: records the Dragonborn's factions
        void CaptureDragonborn(RE::Actor* a_player);
        // After the aliases have moved: the player joins the donor's factions and leaves the Dragonborn's
        void TakeOnDonorFactions(RE::Actor* a_donor, RE::Actor* a_player);
        // While the player still holds the donor's aliases: the donor gets the player's current factions (so a
        // script or topic that promoted or expelled the donor sticks), and the player gets the Dragonborn's back
        void ReturnFactions(RE::Actor* a_donor, RE::Actor* a_player);
        // A session saved before the player took on the donor's factions does so now
        void OnPostLoadGame(RE::Actor* a_donor, RE::Actor* a_player);

        // While the player holds the donor's factions, the Dragonborn's rank in a_faction (-1 if not a member),
        // for conditions asked of the Dragonborn. nullopt when the player's own membership is the answer.
        // May be read from any thread.
        std::optional<std::int8_t> GetDragonbornFactionRank(const RE::TESFaction* a_faction) const;

        bool IsActive() const;
        void Clear();

        // Serialization ('FACT' record)
        const std::vector<FactionRank>& GetDragonbornFactions() const { return _dragonbornFactions; }
        const std::vector<FactionRank>& GetGivenFactions() const { return _givenFactions; }
        void Restore(std::vector<FactionRank> a_dragonborn, std::vector<FactionRank> a_given);

    private:
        friend Singleton<FactionManager>;
        FactionManager() = default;
        ~FactionManager() = default;

        // Guards _active and _dragonbornFactions, which the condition hooks read; written on the main thread only
        mutable std::mutex _lock;
        bool _active{ false };
        // The Dragonborn's factions when the session began
        std::vector<FactionRank> _dragonbornFactions;
        // The donor's factions the player was given
        std::vector<FactionRank> _givenFactions;
    };

}
