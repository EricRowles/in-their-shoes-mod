#pragma once

#include "Serialization.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace InTheirShoes {

    // State the player takes on from the donor for a session while the Dragonborn's is set aside: factions, linked
    // references and bounty. When the session ends the donor gets the player's as it is then (so a script that
    // changed it sticks), and the Dragonborn's comes back. Each stash saves itself in its own co-save record.
    // Main thread, except where noted.
    class SessionStash {
    public:
        virtual ~SessionStash() = default;

        // After the donor's aliases have moved to the player
        void Begin(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
            OnBegin(a_donor, a_player);
            _active = true;
        }
        // While the player still holds the donor's aliases. Nothing to hand back unless Begin or Load ran.
        void End(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
            if (_active && a_player) OnEnd(a_donor, a_player);
            Clear();
        }
        void Clear() {
            _active = false;
            OnClear();
        }
        // Any thread
        bool IsActive() const { return _active.load(std::memory_order_acquire); }

        // Co-save record, written while a session is active
        const std::uint32_t record;
        const std::uint32_t version;
        const std::string_view name;
        virtual void Save(Serialization::Serde* a_serde) const = 0;
        // False if the record ends early, and nothing is restored
        bool Load(Serialization::Serde* a_serde) {
            if (!OnLoad(a_serde)) return false;
            _active = true;
            return true;
        }

        // Every stash, in the order they begin and end
        static std::span<SessionStash* const> All();

    protected:
        SessionStash(std::uint32_t a_record, std::uint32_t a_version, std::string_view a_name) :
            record(a_record), version(a_version), name(a_name) {}

        virtual void OnBegin(RE::Actor* a_donor, RE::PlayerCharacter* a_player) = 0;
        virtual void OnEnd(RE::Actor* a_donor, RE::PlayerCharacter* a_player) = 0;
        virtual void OnClear() = 0;
        virtual bool OnLoad(Serialization::Serde* a_serde) = 0;

    private:
        std::atomic<bool> _active{ false };
    };

    // Makes the player a real member of the donor's factions for the session, and takes the Dragonborn out of
    // theirs. This reaches what the engine decides from membership: faction reactions (a bandit's allies are the
    // player's), faction ownership (guild beds and stock), faction conditions and Papyrus IsInFaction /
    // GetFactionRank. The donor's dialogue needs nothing more: its faction conditions ask the player.
    // Two kinds of faction stay put: the Dragonborn's PlayerFaction, and the factions of the donor's swapped aliases,
    // which reach the player with the aliases.
    class FactionStash : public SessionStash {
    public:
        using FactionRank = Serialization::FactionRank;

        FactionStash() : SessionStash('FACT', 1, "factions") {}

        // While the player holds the donor's factions, the Dragonborn's rank in a_faction (-1 if not a member), for
        // conditions asked of the Dragonborn. nullopt when the player's own membership is the answer. Any thread.
        std::optional<std::int8_t> GetDragonbornFactionRank(const RE::TESFaction* a_faction) const;
        // How many of the donor's factions the player was given (the MCM shows it)
        std::size_t GetGivenCount() const { return _given.size(); }

        void Save(Serialization::Serde* a_serde) const override;

    protected:
        void OnBegin(RE::Actor* a_donor, RE::PlayerCharacter* a_player) override;
        void OnEnd(RE::Actor* a_donor, RE::PlayerCharacter* a_player) override;
        void OnClear() override;
        bool OnLoad(Serialization::Serde* a_serde) override;

    private:
        // Guards _dragonborn, which the condition hooks read
        mutable std::mutex _lock;
        // The Dragonborn's factions when the session began
        std::vector<FactionRank> _dragonborn;
        // The donor's factions the player was given
        std::vector<FactionRank> _given;
    };

    namespace SessionStashes {
        FactionStash& Factions();
    }

}
