#pragma once

#include <vector>

namespace InTheirShoes {

    // Gives the player the donor's linked references for the session. Packages, conditions and scripts that ask
    // an actor for its linked reference (a UseIdleMarker package going to "my linked ref with keyword X") then
    // find the donor's on the player. When the session ends the donor gets the player's linked references as they
    // are then, and the Dragonborn's come back. Not thread-safe.
    class LinkedRefManager : public REX::Singleton<LinkedRefManager> {
    public:
        struct Link {
            RE::BGSKeyword* keyword{ nullptr };  // null for the reference's default link
            RE::TESObjectREFR* ref{ nullptr };
        };

        void OnSessionBegin(RE::Actor* a_donor, RE::PlayerCharacter* a_player);
        void OnSessionEnd(RE::Actor* a_donor, RE::PlayerCharacter* a_player);
        // A session saved before this was tracked gives the player the donor's links now
        void OnPostLoadGame(RE::Actor* a_donor, RE::PlayerCharacter* a_player);
        void Clear();

        // Serialization ('LINK' record)
        bool IsActive() const { return _active; }
        const std::vector<Link>& GetDragonbornLinks() const { return _dragonborn; }
        void Restore(std::vector<Link> a_dragonborn);

    private:
        friend REX::Singleton<LinkedRefManager>;
        LinkedRefManager() = default;
        ~LinkedRefManager() = default;

        bool _active{ false };
        // The Dragonborn's links when the session began
        std::vector<Link> _dragonborn;
    };

}
