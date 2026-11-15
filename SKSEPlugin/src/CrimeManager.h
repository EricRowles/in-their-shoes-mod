#pragma once

#include <vector>

namespace InTheirShoes {

    // Keeps the Dragonborn's bounty apart from the donor's. Crime gold lives on the player, so without this the
    // donor would be wanted for the Dragonborn's crimes and the Dragonborn would carry the donor's. Each session
    // starts with no bounty; when it ends, what the donor ran up is dropped and the Dragonborn's comes back.
    // Not thread-safe.
    class CrimeManager : public REX::Singleton<CrimeManager> {
    public:
        struct FactionBounty {
            RE::TESFaction* faction{ nullptr };
            RE::CrimeGoldStruct gold{};
        };

        void OnSessionBegin(RE::PlayerCharacter* a_player);
        void OnSessionEnd(RE::PlayerCharacter* a_player);
        void Clear();

        // Serialization ('CRIM' record)
        bool IsActive() const { return _active; }
        const std::vector<FactionBounty>& GetDragonbornBounty() const { return _dragonborn; }
        void Restore(std::vector<FactionBounty> a_bounty);

    private:
        friend REX::Singleton<CrimeManager>;
        CrimeManager() = default;
        ~CrimeManager() = default;

        // False for a session saved before this was tracked: its bounty is left as it is
        bool _active{ false };
        std::vector<FactionBounty> _dragonborn;
    };

}
