#include "PCH.h"
#include "InventoryManager.h"
#include <algorithm>
#include <unordered_set>

namespace InTheirShoes {

    static bool IsModSystemSpell(RE::SpellItem* a_spell) {
        if (!a_spell) return false;

        static RE::SpellItem* s_impersonateSpell = nullptr;
        static RE::SpellItem* s_revertSpell = nullptr;

        if (!s_impersonateSpell || !s_revertSpell) {
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                if (!s_impersonateSpell) {
                    s_impersonateSpell = dataHandler->LookupForm<RE::SpellItem>(0x00000D69, "InTheirShoes.esp"sv);
                }
                if (!s_revertSpell) {
                    s_revertSpell = dataHandler->LookupForm<RE::SpellItem>(0x00000802, "InTheirShoes.esp"sv);
                }
            }
        }

        return (a_spell == s_impersonateSpell || a_spell == s_revertSpell);
    }

    static bool IsCastableSpell(RE::SpellItem* a_spell) {
        if (!a_spell) return false;
        auto spellType = a_spell->GetSpellType();
        return (spellType == RE::MagicSystem::SpellType::kSpell ||
                spellType == RE::MagicSystem::SpellType::kPower ||
                spellType == RE::MagicSystem::SpellType::kLesserPower ||
                spellType == RE::MagicSystem::SpellType::kVoicePower);
    }

    static bool IsBaseSpellOf(RE::SpellItem* a_spell, RE::TESNPC* a_base) {
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

    static std::vector<RE::SpellItem*> GetCastableAddedSpells(RE::Actor* a_actor) {
        std::vector<RE::SpellItem*> result;
        for (auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
            if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                result.push_back(spell);
            }
        }
        return result;
    }

    void InventoryManager::StashPlayerSpells(RE::Actor* a_player, SpellSnapshot& a_outSnapshot) {
        auto& runtime = a_player->GetActorRuntimeData();
        a_outSnapshot.leftSpell = skyrim_cast<RE::SpellItem*>(runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]);
        a_outSnapshot.rightSpell = skyrim_cast<RE::SpellItem*>(runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand]);
        a_outSnapshot.shout = skyrim_cast<RE::TESShout*>(runtime.selectedPower);

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

        auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        if (defaultObjMgr && equipMgr) {
            auto* leftSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip);
            auto* rightSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);
            if (a_snapshot.leftSpell && leftSlot) {
                equipMgr->EquipSpell(a_player, a_snapshot.leftSpell, leftSlot);
            }
            if (a_snapshot.rightSpell && rightSlot) {
                equipMgr->EquipSpell(a_player, a_snapshot.rightSpell, rightSlot);
            }
            if (a_snapshot.shout) {
                equipMgr->EquipShout(a_player, a_snapshot.shout);
            }
        }
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

    bool InventoryManager::TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor, bool a_forceEquip,
        std::unordered_set<RE::FormID>& a_donorQuestItems) {
        if (!a_fromActor || !a_toActor) {
            SKSE::log::warn("TransferAllItems called with null actor pointer.");
            return false;
        }

        struct ItemRecord {
            RE::TESBoundObject* object{ nullptr };
            std::int32_t count{ 0 };
        };

        // 1. Clear the target's non-quest inventory in two passes (collect first, then remove)
        std::vector<ItemRecord> itemsToRemove;
        for (const auto& [boundObj, entryPair] : a_toActor->GetInventory()) {
            std::int32_t count = entryPair.first;
            const auto& entryData = entryPair.second;

            if (!boundObj || count <= 0) continue;
            if (entryData && entryData->IsQuestObject()) continue;
            itemsToRemove.push_back({ boundObj, count });
        }

        for (const auto& entry : itemsToRemove) {
            a_toActor->RemoveItem(entry.object, entry.count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        }

        // 2. Get equipped spells & shout from source actor
        auto& fromRuntime = a_fromActor->GetActorRuntimeData();
        auto* leftSpell = skyrim_cast<RE::SpellItem*>(fromRuntime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]);
        auto* rightSpell = skyrim_cast<RE::SpellItem*>(fromRuntime.selectedSpells[RE::Actor::SlotTypes::kRightHand]);
        auto* shout = skyrim_cast<RE::TESShout*>(fromRuntime.selectedPower);

        // 3. Identify worn items and their equipped slots before transfer
        auto fromInv = a_fromActor->GetInventory();
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
        auto* leftSlot = defaultObjMgr ? defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip) : nullptr;

        struct WornItemRecord {
            RE::TESBoundObject* object{ nullptr };
            bool isLeft{ false };
        };
        std::vector<WornItemRecord> wornRecords;
        std::unordered_set<RE::TESBoundObject*> wornObjects;

        for (const auto& [boundObj, entryPair] : fromInv) {
            const auto& entryData = entryPair.second;
            if (!boundObj || !entryData || !entryData->IsWorn()) continue;

            wornObjects.insert(boundObj);
            bool foundHand = false;
            if (entryData->extraLists) {
                for (auto* xList : *entryData->extraLists) {
                    if (!xList) continue;
                    if (xList->HasType(RE::ExtraDataType::kWornLeft)) {
                        wornRecords.push_back({ boundObj, true });
                        foundHand = true;
                    } else if (xList->HasType(RE::ExtraDataType::kWorn)) {
                        wornRecords.push_back({ boundObj, false });
                        foundHand = true;
                    }
                }
            }
            if (!foundHand) {
                wornRecords.push_back({ boundObj, false });
            }
        }

        // 4. Transfer items in two passes (collect and update quest tracking first, then transfer)
        std::vector<ItemRecord> itemsToTransfer;
        for (const auto& [boundObj, entryPair] : fromInv) {
            std::int32_t count = entryPair.first;
            const auto& entryData = entryPair.second;
            if (!boundObj || count <= 0) continue;

            bool isQuest = entryData && entryData->IsQuestObject();
            if (isQuest) {
                if (a_fromActor->IsPlayerRef() && !a_toActor->IsPlayerRef()) {
                    if (a_donorQuestItems.find(boundObj->GetFormID()) == a_donorQuestItems.end()) {
                        continue; // Keep player's own quest items on the player
                    }
                    a_donorQuestItems.erase(boundObj->GetFormID());
                } else if (!a_fromActor->IsPlayerRef() && a_toActor->IsPlayerRef()) {
                    a_donorQuestItems.insert(boundObj->GetFormID());
                }
            }

            itemsToTransfer.push_back({ boundObj, count });
        }

        std::int32_t itemsTransferred = 0;
        for (const auto& entry : itemsToTransfer) {
            a_fromActor->RemoveItem(entry.object, entry.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_toActor);
            itemsTransferred++;
        }

        // 5. Re-equip worn items on target actor
        auto toInvAfter = a_toActor->GetInventory();
        for (const auto& wornRec : wornRecords) {
            auto it = toInvAfter.find(wornRec.object);
            if (it != toInvAfter.end() && it->second.first > 0) {
                auto& toEntry = it->second.second;
                RE::ExtraDataList* xList = nullptr;
                if (toEntry && toEntry->extraLists && !toEntry->extraLists->empty()) {
                    for (auto* x : *toEntry->extraLists) {
                        if (x && !x->HasType(RE::ExtraDataType::kWorn) && !x->HasType(RE::ExtraDataType::kWornLeft)) {
                            xList = x;
                            break;
                        }
                    }
                    if (!xList) {
                        xList = toEntry->extraLists->front();
                    }
                }
                const RE::BGSEquipSlot* slot = wornRec.isLeft ? leftSlot : nullptr;
                std::uint32_t equipCount = wornRec.object->Is(RE::FormType::Ammo) ? static_cast<std::uint32_t>(it->second.first) : 1;
                // Non-playable items (such as equipped NPC skins) require forceEquip on the player
                bool forceEquip = a_forceEquip || !wornRec.object->GetPlayable();
                equipMgr->EquipObject(a_toActor, wornRec.object, xList, equipCount, slot, false, forceEquip, false, true);
            }
        }

        // 6. Unequip any items auto-equipped by AddObjectToContainer on NPCs that were not worn by source
        if (!a_toActor->IsPlayerRef()) {
            for (const auto& [boundObj, entryPair] : a_toActor->GetInventory()) {
                const auto& entryData = entryPair.second;
                if (boundObj && entryData && entryData->IsWorn()) {
                    if (wornObjects.find(boundObj) == wornObjects.end()) {
                        equipMgr->UnequipObject(a_toActor, boundObj, nullptr, 1, nullptr, false, true, false, true);
                    }
                }
            }
        }

        // 7. Re-equip spells if target is the player
        if (a_toActor->IsPlayerRef() && defaultObjMgr) {
            auto* rightSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);
            if (leftSpell && leftSlot) {
                equipMgr->EquipSpell(a_toActor, leftSpell, leftSlot);
            }
            if (rightSpell && rightSlot) {
                equipMgr->EquipSpell(a_toActor, rightSpell, rightSlot);
            }
            if (shout) {
                equipMgr->EquipShout(a_toActor, shout);
            }
        }

        SKSE::log::info("Transferred {} item stacks from {:08X} to {:08X}.",
            itemsTransferred,
            static_cast<std::uint32_t>(a_fromActor->GetFormID()),
            static_cast<std::uint32_t>(a_toActor->GetFormID())
        );

        a_toActor->Update3DModel();
        return true;
    }

}
