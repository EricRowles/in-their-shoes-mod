#include "PCH.h"
#include "InventoryManager.h"
#include "Forms.h"
#include <unordered_set>

namespace InTheirShoes {

    namespace {
        bool IsModSystemSpell(RE::SpellItem* a_spell) {
            return a_spell && (a_spell == Forms::impersonateSpell || a_spell == Forms::revertSpell);
        }

        bool IsCastableSpell(RE::SpellItem* a_spell) {
            if (!a_spell) return false;
            auto spellType = a_spell->GetSpellType();
            return (spellType == RE::MagicSystem::SpellType::kSpell ||
                    spellType == RE::MagicSystem::SpellType::kPower ||
                    spellType == RE::MagicSystem::SpellType::kLesserPower ||
                    spellType == RE::MagicSystem::SpellType::kVoicePower);
        }

        bool IsBaseSpellOf(RE::SpellItem* a_spell, RE::TESNPC* a_base) {
            auto* effects = a_base ? a_base->actorEffects : nullptr;
            if (!effects) return false;
            for (std::uint32_t i = 0; i < effects->numSpells; ++i) {
                if (effects->spells[i] == a_spell) return true;
            }
            for (std::uint32_t i = 0; i < effects->numShouts; ++i) {
                if (auto* shout = effects->shouts[i]) {
                    for (const auto& var : shout->variations) {
                        if (var.spell == a_spell) return true;
                    }
                }
            }
            return false;
        }

        std::vector<RE::SpellItem*> GetCastableAddedSpells(RE::Actor* a_actor) {
            std::vector<RE::SpellItem*> result;
            for (auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
                if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                    result.push_back(spell);
                }
            }
            return result;
        }

        RE::BGSEquipSlot* GetEquipSlot(RE::BGSDefaultObjectManager::DefaultObject a_slot) {
            auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
            return defaultObjMgr ? defaultObjMgr->GetObject<RE::BGSEquipSlot>(a_slot) : nullptr;
        }

        // The spells in an actor's hands and its selected shout or power
        struct EquippedMagic {
            RE::SpellItem* left{ nullptr };
            RE::SpellItem* right{ nullptr };
            RE::TESShout* shout{ nullptr };
        };

        EquippedMagic GetEquippedMagic(RE::Actor* a_actor) {
            auto& runtime = a_actor->GetActorRuntimeData();
            return { skyrim_cast<RE::SpellItem*>(runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]),
                skyrim_cast<RE::SpellItem*>(runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand]),
                skyrim_cast<RE::TESShout*>(runtime.selectedPower) };
        }

        void EquipMagic(RE::Actor* a_actor, const EquippedMagic& a_magic) {
            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (!equipMgr) return;
            auto* leftSlot = GetEquipSlot(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip);
            auto* rightSlot = GetEquipSlot(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);
            if (a_magic.left && leftSlot) {
                equipMgr->EquipSpell(a_actor, a_magic.left, leftSlot);
            }
            if (a_magic.right && rightSlot) {
                equipMgr->EquipSpell(a_actor, a_magic.right, rightSlot);
            }
            if (a_magic.shout) {
                equipMgr->EquipShout(a_actor, a_magic.shout);
            }
        }

        struct ItemStack {
            RE::TESBoundObject* object{ nullptr };
            std::int32_t count{ 0 };
        };

        // Removes everything from a_actor except quest items. Skins (non-playable body armor) go too: they move
        // with the rest of the items, and a non-unique donor may have been given its outfit's skin again while
        // parked. Collected first: removing items while walking the inventory would invalidate it.
        void ClearInventory(RE::Actor* a_actor) {
            std::vector<ItemStack> toRemove;
            for (const auto& [object, entry] : a_actor->GetInventory()) {
                const auto& [count, data] = entry;
                if (!object || count <= 0) continue;
                if (data && data->IsQuestObject()) continue;
                toRemove.push_back({ object, count });
            }
            for (const auto& stack : toRemove) {
                a_actor->RemoveItem(stack.object, stack.count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            }
        }

        // A worn item, and whether it's worn in the left hand. An item worn in both hands has a record for each.
        struct WornItem {
            RE::TESBoundObject* object{ nullptr };
            bool isLeft{ false };
        };

        std::vector<WornItem> GetWornItems(const RE::TESObjectREFR::InventoryItemMap& a_inventory) {
            std::vector<WornItem> worn;
            for (const auto& [object, entry] : a_inventory) {
                const auto& data = entry.second;
                if (!object || !data || !data->IsWorn()) continue;

                bool foundHand = false;
                if (data->extraLists) {
                    for (auto* xList : *data->extraLists) {
                        if (!xList) continue;
                        if (xList->HasType(RE::ExtraDataType::kWornLeft)) {
                            worn.push_back({ object, true });
                            foundHand = true;
                        } else if (xList->HasType(RE::ExtraDataType::kWorn)) {
                            worn.push_back({ object, false });
                            foundHand = true;
                        }
                    }
                }
                if (!foundHand) {
                    worn.push_back({ object, false });
                }
            }
            return worn;
        }

        // The stacks to move from a_from to a_to, updating which donor quest items the player carries.
        // The player keeps their own quest items.
        std::vector<ItemStack> GetItemsToMove(RE::Actor* a_from, RE::Actor* a_to, const RE::TESObjectREFR::InventoryItemMap& a_inventory,
            std::unordered_set<RE::FormID>& a_donorQuestItems) {
            const bool toDonor = a_from->IsPlayerRef() && !a_to->IsPlayerRef();
            const bool toPlayer = !a_from->IsPlayerRef() && a_to->IsPlayerRef();

            std::vector<ItemStack> toMove;
            for (const auto& [object, entry] : a_inventory) {
                const auto& [count, data] = entry;
                if (!object || count <= 0) continue;

                if (data && data->IsQuestObject()) {
                    if (toDonor) {
                        if (!a_donorQuestItems.erase(object->GetFormID())) {
                            continue;  // the player's own quest item
                        }
                    } else if (toPlayer) {
                        a_donorQuestItems.insert(object->GetFormID());
                    }
                }
                toMove.push_back({ object, count });
            }
            return toMove;
        }

        // Makes a stack's outfit items ordinary items. The tag names the outfit an item came from, and the engine
        // removes tagged items when that actor's outfit changes. The extra data lists are the inventory's own
        // (GetInventory copies only the list of pointers), so this changes the items themselves.
        void ClearOutfitTags(const RE::InventoryEntryData* a_data) {
            if (!a_data || !a_data->extraLists) return;
            for (auto* xList : *a_data->extraLists) {
                if (xList) {
                    xList->RemoveByType(RE::ExtraDataType::kOutfitItem);
                }
            }
        }

        // Equips a_worn on a_actor again, from the stacks it now holds
        void EquipWornItems(RE::Actor* a_actor, const std::vector<WornItem>& a_worn, bool a_forceEquip) {
            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (!equipMgr) return;
            auto* leftSlot = GetEquipSlot(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip);

            auto inventory = a_actor->GetInventory();
            for (const auto& worn : a_worn) {
                auto it = inventory.find(worn.object);
                if (it == inventory.end() || it->second.first <= 0) continue;

                // Prefer an extra data list that isn't already worn
                const auto& data = it->second.second;
                RE::ExtraDataList* xList = nullptr;
                if (data && data->extraLists && !data->extraLists->empty()) {
                    for (auto* x : *data->extraLists) {
                        if (x && !x->HasType(RE::ExtraDataType::kWorn) && !x->HasType(RE::ExtraDataType::kWornLeft)) {
                            xList = x;
                            break;
                        }
                    }
                    if (!xList) {
                        xList = data->extraLists->front();
                    }
                }
                const RE::BGSEquipSlot* slot = worn.isLeft ? leftSlot : nullptr;
                std::uint32_t equipCount = worn.object->Is(RE::FormType::Ammo) ? static_cast<std::uint32_t>(it->second.first) : 1;
                // Not forced on the player, non-playable items included: a forced skin that also fills the body
                // slot kept the player from wearing armor. Unforced, armor in the same slot replaces it.
                equipMgr->EquipObject(a_actor, worn.object, xList, equipCount, slot, false, a_forceEquip, false, true);
            }
        }

        // Unequips what an NPC put on by itself when the items arrived (AddObjectToContainer auto-equips),
        // keeping a_keep
        void UnequipAutoEquipped(RE::Actor* a_actor, const std::unordered_set<RE::TESBoundObject*>& a_keep) {
            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (!equipMgr) return;
            for (const auto& [object, entry] : a_actor->GetInventory()) {
                const auto& data = entry.second;
                if (object && data && data->IsWorn() && !a_keep.contains(object)) {
                    equipMgr->UnequipObject(a_actor, object, nullptr, 1, nullptr, false, true, false, true);
                }
            }
        }
    }

    void InventoryManager::StashPlayerSpells(RE::Actor* a_player, SpellSnapshot& a_outSnapshot) {
        const auto magic = GetEquippedMagic(a_player);
        a_outSnapshot.leftSpell = magic.left;
        a_outSnapshot.rightSpell = magic.right;
        a_outSnapshot.shout = magic.shout;

        a_outSnapshot.addedSpells.clear();
        for (auto* spell : GetCastableAddedSpells(a_player)) {
            a_outSnapshot.addedSpells.push_back(spell->GetFormID());
            a_player->RemoveSpell(spell);
        }
        SKSE::log::info("Stashed {} player castable spells.", a_outSnapshot.addedSpells.size());
    }

    void InventoryManager::RestorePlayerSpells(RE::Actor* a_player, const SpellSnapshot& a_snapshot) {
        std::int32_t restoredCount = 0;
        for (auto formID : a_snapshot.addedSpells) {
            if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID)) {
                if (a_player->AddSpell(spell)) {
                    restoredCount++;
                }
            }
        }

        EquipMagic(a_player, { a_snapshot.leftSpell, a_snapshot.rightSpell, a_snapshot.shout });
        SKSE::log::info("Restored {} player spells.", restoredCount);
    }

    void InventoryManager::CopyDonorSpells(RE::Actor* a_donor, RE::Actor* a_player) {
        std::int32_t copiedCount = 0;
        auto grantSpell = [&](RE::SpellItem* spell) {
            if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell) && a_player->AddSpell(spell)) {
                copiedCount++;
            }
        };

        for (auto* spell : a_donor->GetActorRuntimeData().addedSpells) {
            grantSpell(spell);
        }
        if (auto* donorBase = a_donor->GetActorBase(); donorBase && donorBase->actorEffects) {
            auto* effects = donorBase->actorEffects;
            for (std::uint32_t i = 0; i < effects->numSpells; ++i) {
                grantSpell(effects->spells[i]);
            }
            for (std::uint32_t i = 0; i < effects->numShouts; ++i) {
                if (auto* shout = effects->shouts[i]) {
                    for (const auto& var : shout->variations) {
                        grantSpell(var.spell);
                    }
                }
            }
        }
        SKSE::log::info("Copied {} castable spells from donor {:08X} to player.", copiedCount, a_donor->GetFormID());
    }

    void InventoryManager::ReturnSpellsToDonor(RE::Actor* a_player, RE::Actor* a_donor) {
        // The player's own spells were stashed at baseline capture, so everything castable left on the
        // player is either a donor copy or was learned while impersonating
        auto* donorBase = a_donor ? a_donor->GetActorBase() : nullptr;
        std::int32_t returnedCount = 0;
        for (auto* spell : GetCastableAddedSpells(a_player)) {
            if (a_donor && !IsBaseSpellOf(spell, donorBase) && a_donor->AddSpell(spell)) {
                returnedCount++;
            }
            a_player->RemoveSpell(spell);
        }
        SKSE::log::info("Returned {} spells to donor.", returnedCount);
    }

    bool InventoryManager::TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor,
        std::unordered_set<RE::FormID>& a_donorQuestItems, RE::TESObjectREFR* a_unwornTo) {
        if (!a_fromActor || !a_toActor) {
            SKSE::log::warn("TransferAllItems called with null actor pointer.");
            return false;
        }

        ClearInventory(a_toActor);

        // What the source wears and casts, read before its items leave
        const auto magic = GetEquippedMagic(a_fromActor);
        const auto fromInventory = a_fromActor->GetInventory();
        const auto worn = GetWornItems(fromInventory);
        std::unordered_set<RE::TESBoundObject*> wornObjects;
        for (const auto& item : worn) {
            wornObjects.insert(item.object);
        }

        // A stack that is partly worn goes whole to a_toActor
        const auto toMove = GetItemsToMove(a_fromActor, a_toActor, fromInventory, a_donorQuestItems);
        std::int32_t unwornStacks = 0;
        for (const auto& stack : toMove) {
            ClearOutfitTags(fromInventory.at(stack.object).second.get());
            RE::TESObjectREFR* destination = a_toActor;
            if (a_unwornTo && !wornObjects.contains(stack.object)) {
                destination = a_unwornTo;
                unwornStacks++;
            }
            a_fromActor->RemoveItem(stack.object, stack.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, destination);
        }

        EquipWornItems(a_toActor, worn, !a_toActor->IsPlayerRef());
        if (a_toActor->IsPlayerRef()) {
            EquipMagic(a_toActor, magic);
        } else {
            UnequipAutoEquipped(a_toActor, wornObjects);
        }

        SKSE::log::info("Transferred {} item stacks from {:08X} to {:08X} ({} unworn stacks to {:08X}).",
            toMove.size(), a_fromActor->GetFormID(), a_toActor->GetFormID(), unwornStacks,
            a_unwornTo ? a_unwornTo->GetFormID() : 0u);

        a_toActor->Update3DModel();
        return true;
    }

    void InventoryManager::TakeAllItems(RE::TESObjectREFR* a_container, RE::Actor* a_toActor) {
        if (!a_container || !a_toActor) return;
        // Collected first: removing items while walking the inventory would invalidate it
        std::vector<ItemStack> toMove;
        for (const auto& [object, entry] : a_container->GetInventory()) {
            if (object && entry.first > 0) {
                toMove.push_back({ object, entry.first });
            }
        }
        for (const auto& stack : toMove) {
            a_container->RemoveItem(stack.object, stack.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_toActor);
        }
        SKSE::log::info("Took {} item stacks from {:08X} to {:08X}.", toMove.size(), a_container->GetFormID(), a_toActor->GetFormID());
    }

}
