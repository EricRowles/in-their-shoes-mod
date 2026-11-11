#pragma once

namespace InTheirShoes::Forms {

    // The player's base record and reference, which have the same ID in every load order
    inline constexpr RE::FormID kPlayerBaseID = 0x07;
    inline constexpr RE::FormID kPlayerRefID = 0x14;

    // Forms the plugin uses, resolved once by Resolve() on kDataLoaded. A form that can't be found stays null
    // (and is logged), so check before dereferencing. See docs/esp_quest_structure.md for the mod's records.

    // InTheirShoes.esp
    inline RE::TESQuest* itsQuest{ nullptr };              // ITS_Quest
    inline RE::TESObjectREFR* storageMarker{ nullptr };    // ITS_StorageMarker, where the donor is parked
    inline RE::TESObjectREFR* playerStorageChest{ nullptr };  // ITS_PlayerStorageChest, the Dragonborn's unworn items
    inline RE::SpellItem* impersonateSpell{ nullptr };     // ITS_ImpersonateSpell
    inline RE::SpellItem* revertSpell{ nullptr };          // ITS_RevertSpell
    inline RE::BGSOutfit* emptyOutfit{ nullptr };          // ITS_EmptyOutfit, given to unique donors
    // ITS_PackageManager: one alias per idle AI package the MCM offers, filled with the player while it runs
    inline RE::TESQuest* idlePackageQuest{ nullptr };

    // Skyrim.esm
    inline RE::TESFaction* playerFaction{ nullptr };  // The Dragonborn's own; owns their homes and belongings
    inline RE::TESQuest* playerWerewolfQuest{ nullptr };

    // Dawnguard.esm
    inline RE::TESQuest* dlc1PlayerVampireQuest{ nullptr };  // Vampire Lord transformation

    // Where the Travel idle package can go: the hold capitals, in the MCM's order (ITS_MCMScript.GetTravelDestinationNames)
    struct TravelDestinationForms {
        RE::BGSLocation* location{ nullptr };  // The city, to tell when the player has arrived
        RE::TESObjectREFR* marker{ nullptr };  // Its LocationCenterMarker, which the package travels to
    };
    inline std::array<TravelDestinationForms, 9> travelDestinations{};

    // Null when a_index is out of range
    inline const TravelDestinationForms* TravelDestination(std::int32_t a_index) {
        return a_index >= 0 && static_cast<std::size_t>(a_index) < travelDestinations.size() ? &travelDestinations[a_index] : nullptr;
    }

    void Resolve();

    // The mod's own quests, which are never swapped to the player or used for idle chatter
    inline bool IsModQuest(const RE::TESQuest* a_quest) {
        return a_quest && (a_quest == itsQuest || a_quest == idlePackageQuest);
    }

}
