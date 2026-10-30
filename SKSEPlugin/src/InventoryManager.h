#pragma once

#include <unordered_set>
#include <vector>

namespace InTheirShoes {

    // The Dragonborn's own castable spells and equipped magic, stashed while impersonating
    struct SpellSnapshot {
        std::vector<RE::FormID> addedSpells;
        RE::SpellItem* leftSpell{ nullptr };
        RE::SpellItem* rightSpell{ nullptr };
        RE::TESShout* shout{ nullptr };
    };

    // Stateless helpers for moving items and spells between the three actors.
    // Session state (stashed spells, donor quest items) is owned by ImpersonationManager.
    namespace InventoryManager {
        // Moves every item from a_fromActor to a_toActor, replacing a_toActor's non-quest items.
        // a_donorQuestItems tracks donor quest items carried by the player so they can go back to the donor.
        // With a_unwornTo, the items a_fromActor isn't wearing go there instead of to a_toActor.
        // Worn items are equipped again; on an NPC they're forced, so it can't swap them for others.
        bool TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor,
            std::unordered_set<RE::FormID>& a_donorQuestItems, RE::TESObjectREFR* a_unwornTo = nullptr);

        // Moves everything in a_container to a_toActor, without equipping it
        void TakeAllItems(RE::TESObjectREFR* a_container, RE::Actor* a_toActor);

        // Removes the player's castable added spells and records them with the equipped magic
        void StashPlayerSpells(RE::Actor* a_player, SpellSnapshot& a_outSnapshot);
        void RestorePlayerSpells(RE::Actor* a_player, const SpellSnapshot& a_snapshot);

        // Grants the donor's added and base spells to the player
        void CopyDonorSpells(RE::Actor* a_donor, RE::Actor* a_player);

        // Removes every castable added spell from the player, handing back to the donor any that
        // the donor doesn't already get from its base record
        void ReturnSpellsToDonor(RE::Actor* a_player, RE::Actor* a_donor);
    }

}
