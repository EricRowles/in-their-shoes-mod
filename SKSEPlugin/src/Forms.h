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

    // Skyrim.esm
    inline RE::TESFaction* currentFollowerFaction{ nullptr };
    inline RE::TESFaction* playerFollowerFaction{ nullptr };
    inline RE::TESPackage* defaultSandboxPackage{ nullptr };  // DefaultSandboxCurrentLocation1024
    inline RE::TESQuest* playerWerewolfQuest{ nullptr };

    // Dawnguard.esm
    inline RE::TESQuest* dlc1PlayerVampireQuest{ nullptr };  // Vampire Lord transformation

    void Resolve();

    // The mod's own quest, which is never swapped to the player or used for idle chatter
    inline bool IsModQuest(const RE::TESQuest* a_quest) {
        return a_quest && a_quest == itsQuest;
    }

}
