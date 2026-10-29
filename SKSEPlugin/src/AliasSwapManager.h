#pragma once

#include <mutex>
#include <optional>
#include <vector>

namespace InTheirShoes {

    // Moves the donor's quest aliases to the player for the session and back afterwards. Swapping physically,
    // rather than only answering GetIsAliasRef for the player, is needed because scenes only run an alias's
    // actions when the actor filling it is loaded, and the parked donor isn't.
    // Main thread only, except GetDonorAliasFactionRank, which faction condition hooks call from AI threads.
    class AliasSwapManager {
    public:
        static AliasSwapManager& GetSingleton() {
            static AliasSwapManager instance;
            return instance;
        }

        struct SwappedAlias {
            RE::FormID questFormID{ 0 };
            std::uint32_t aliasID{ 0 };
        };

        // Forces the player into the donor's aliases in running quests, recording the alias factions this
        // takes off the donor
        void SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player);
        // Puts the donor back into the swapped aliases, except any another actor has taken since
        void RestoreSwappedAliases(RE::Actor* a_donor);
        // Moves any outside quest alias the puppet was forced into (e.g. by a recruitment fragment that
        // calls ForceRefTo(speaker) during self-dialogue) to the player, tracked as a swapped alias so
        // the donor gets it on revert
        void AdoptPuppetAliases(RE::Actor* a_puppet, RE::Actor* a_player);

        bool IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const;
        std::size_t GetSwappedAliasCount() const { return _swappedAliases.size(); }
        void Clear();

        // Factions the donor's swapped aliases gave it (alias factions, e.g. a follower alias's
        // CurrentFollowerFaction). Swapping the aliases moves them to the player, so the donor's faction
        // conditions also count these. May be read from any thread.
        std::optional<std::int8_t> GetDonorAliasFactionRank(const RE::TESFaction* a_faction) const;

        // The swapped aliases are part of the session ('SESS') record
        void WriteSwappedAliases(SKSE::SerializationInterface* a_serde) const;
        // False if the record ends before the alias count
        static bool ReadSwappedAliases(SKSE::SerializationInterface* a_serde, std::vector<SwappedAlias>& a_out);
        void SetSwappedAliases(std::vector<SwappedAlias> a_aliases) { _swappedAliases = std::move(a_aliases); }

        // The 'AFAC' co-save record
        void SaveDonorAliasFactions(SKSE::SerializationInterface* a_serde) const;
        void LoadDonorAliasFactions(SKSE::SerializationInterface* a_serde);

    private:
        AliasSwapManager() = default;
        ~AliasSwapManager() = default;
        AliasSwapManager(const AliasSwapManager&) = delete;
        AliasSwapManager& operator=(const AliasSwapManager&) = delete;

        std::vector<SwappedAlias> _swappedAliases;

        mutable std::mutex _donorAliasFactionsLock;
        std::vector<std::pair<RE::TESFaction*, std::int8_t>> _donorAliasFactions;
    };

}
