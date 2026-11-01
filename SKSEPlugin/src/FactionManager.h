#pragma once

#include "Singleton.h"
#include <vector>

namespace InTheirShoes {

    // Makes the player a real member of the donor's factions for the session, and takes the Dragonborn out of
    // theirs. Unlike the faction condition hooks, this reaches what the engine decides from membership: faction
    // reactions (a bandit's allies are the player's), faction ownership (guild beds and stock) and Papyrus
    // IsInFaction / GetFactionRank on the player.
    //
    // Some factions stay with their owner: the Dragonborn's PlayerFaction, the follower factions (the alias
    // swap and ForwardPuppetFactionsToDonor handle those) and the factions of the donor's swapped aliases,
    // which reach the player with the aliases. Not thread-safe.
    class FactionManager : public Singleton<FactionManager> {
    public:
        using FactionRank = std::pair<RE::TESFaction*, std::int8_t>;

        // Before the donor's aliases move to the player: records the Dragonborn's factions
        void CaptureDragonborn(RE::Actor* a_player);
        // After the aliases have moved: the player joins the donor's factions and leaves the Dragonborn's
        void TakeOnDonorFactions(RE::Actor* a_donor, RE::Actor* a_player);
        // While the player still holds the donor's aliases: the donor gets the player's current factions (so
        // a script that promoted or expelled "the donor" through an alias sticks), and the player gets the
        // Dragonborn's back
        void ReturnFactions(RE::Actor* a_donor, RE::Actor* a_player);
        // A faction change made to the donor mid-session (self-dialogue forwarding): the player, who is the
        // donor for now, gets it too, so ReturnFactions doesn't undo it
        void OnDonorFactionChanged(RE::Actor* a_player, RE::TESFaction* a_faction, std::int8_t a_rank);

        bool IsActive() const { return _active; }
        void Clear();

        // Serialization ('FACT' record)
        const std::vector<FactionRank>& GetDragonbornFactions() const { return _dragonbornFactions; }
        const std::vector<FactionRank>& GetGivenFactions() const { return _givenFactions; }
        void Restore(std::vector<FactionRank> a_dragonborn, std::vector<FactionRank> a_given);

    private:
        friend Singleton<FactionManager>;
        FactionManager() = default;
        ~FactionManager() = default;

        bool _active{ false };
        // The Dragonborn's factions when the session began
        std::vector<FactionRank> _dragonbornFactions;
        // The donor's factions the player was given
        std::vector<FactionRank> _givenFactions;
    };

}
