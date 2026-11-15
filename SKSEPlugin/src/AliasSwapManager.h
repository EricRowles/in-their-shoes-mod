#pragma once

#include <vector>

namespace InTheirShoes {

    // Moves the donor's quest aliases to the player for the session and back afterwards. Swapping physically,
    // rather than only answering GetIsAliasRef for the player, is needed because scenes only run an alias's
    // actions when the actor filling it is loaded, and the parked donor isn't.
    // Don't hook TESQuest::ForceRefIntoAlias: a 5-byte context hook split a prologue instruction and froze the game.
    // Not thread-safe; the quest start sink hands its work to the main thread.
    class AliasSwapManager :
        public REX::Singleton<AliasSwapManager>,
        public RE::BSTEventSink<RE::TESQuestStartStopEvent> {
    public:
        using FactionRank = std::pair<RE::TESFaction*, std::int8_t>;

        // Registers for quest start events
        void Initialize();

        // BSTEventSink<RE::TESQuestStartStopEvent>: a quest that starts during the session may fill an alias with
        // the donor, which then goes to the player (see SwapNewDonorAliases)
        RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStartStopEvent* a_event,
            RE::BSTEventSource<RE::TESQuestStartStopEvent>* a_eventSource) override;

        struct SwappedAlias {
            RE::FormID questFormID{ 0 };
            std::uint32_t aliasID{ 0 };
            // What the alias gave its holder (e.g. a follower alias's CurrentFollowerFaction)
            std::vector<FactionRank> factions;
        };

        // Forces the player into the donor's aliases in running quests, recording each alias's factions
        void SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player);
        // Puts the donor back into the swapped aliases, except any another actor has taken since
        void RestoreSwappedAliases(RE::Actor* a_donor);
        // Moves outside quest aliases the puppet was forced into during self-dialogue to the player, tracked as swapped
        // so the donor gets them on revert
        void AdoptPuppetAliases(RE::Actor* a_puppet, RE::Actor* a_player);
        // Moves aliases a quest filled with the donor during the session (or before a save that lacks them) to
        // the player, tracked as swapped aliases
        void SwapNewDonorAliases();
        // Stops tracking swapped aliases a quest has cleared or given to someone else
        void DropReleasedAliases();

        bool IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const;
        std::size_t GetSwappedAliasCount() const { return _swappedAliases.size(); }
        void Clear();

        // Whether a swapped alias gives its holder a_faction. The player gets these with the aliases, so
        // FactionManager leaves them out of the factions it moves.
        bool IsAliasFaction(const RE::TESFaction* a_faction) const;

        // Serialization: the swapped aliases are part of the session ('SESS') record
        const std::vector<SwappedAlias>& GetSwappedAliases() const { return _swappedAliases; }
        void SetSwappedAliases(std::vector<SwappedAlias> a_aliases);

    private:
        friend REX::Singleton<AliasSwapManager>;
        AliasSwapManager() = default;
        ~AliasSwapManager() override = default;

        // Moves a_from's alias to a_to and tracks it as swapped, so the donor gets it on revert
        void TakeAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID, RE::Actor* a_from, RE::Actor* a_to);

        std::vector<SwappedAlias> _swappedAliases;
    };

}
