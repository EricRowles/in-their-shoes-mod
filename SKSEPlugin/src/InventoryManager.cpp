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

    bool InventoryManager::StashPlayerDynamicSpells(RE::Actor* a_player) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        _playerSavedSpells.clear();
        std::vector<RE::SpellItem*> toRemove;

        auto& runtime = a_player->GetActorRuntimeData();
        for (auto* spell : runtime.addedSpells) {
            if (!spell || IsModSystemSpell(spell)) continue;

            if (IsCastableSpell(spell)) {
                _playerSavedSpells.push_back(spell->GetFormID());
                toRemove.push_back(spell);
            }
        }

        for (auto* spell : toRemove) {
            a_player->RemoveSpell(spell);
        }

        SKSE::log::info("Stashed {} player dynamic castable spells.", _playerSavedSpells.size());
        return true;
    }

    bool InventoryManager::RestorePlayerDynamicSpells(RE::Actor* a_player) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        std::int32_t restoredCount = 0;
        for (auto formID : _playerSavedSpells) {
            if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID)) {
                if (a_player->AddSpell(spell)) {
                    restoredCount++;
                }
            }
        }

        SKSE::log::info("Restored {} player dynamic spells.", restoredCount);
        _playerSavedSpells.clear();
        return true;
    }

    bool InventoryManager::CopyDynamicSpells(RE::Actor* a_fromActor, RE::Actor* a_toActor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_fromActor || !a_toActor) return false;

        std::int32_t copiedCount = 0;

        auto grantSpell = [&](RE::SpellItem* spell) {
            if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                if (a_toActor->AddSpell(spell)) {
                    _transferredSpells.push_back(spell->GetFormID());
                    copiedCount++;
                }
            }
        };

        // 1. Copy dynamic spells from donor
        for (auto* spell : a_fromActor->GetActorRuntimeData().addedSpells) {
            grantSpell(spell);
        }

        // 2. Copy base spells and shout variation spells from donor's TESNPC actorEffects
        if (auto* donorBase = a_fromActor->GetActorBase(); donorBase && donorBase->actorEffects) {
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

        SKSE::log::info("Transferred {} total castable spells from {:08X} to {:08X}.",
            copiedCount,
            static_cast<std::uint32_t>(a_fromActor->GetFormID()),
            static_cast<std::uint32_t>(a_toActor->GetFormID())
        );

        return true;
    }

    bool InventoryManager::RemoveCopiedSpells(RE::Actor* a_targetActor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_targetActor) return false;

        std::int32_t removedCount = 0;
        for (auto formID : _transferredSpells) {
            if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID)) {
                if (a_targetActor->RemoveSpell(spell)) {
                    removedCount++;
                }
            }
        }
        SKSE::log::info("Removed {} copied donor spells from {:08X}.",
            removedCount,
            static_cast<std::uint32_t>(a_targetActor->GetFormID())
        );
        _transferredSpells.clear();
        return true;
    }

    bool InventoryManager::TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor, bool a_forceEquip) {
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
                    if (_transferredDonorQuestItems.find(boundObj->GetFormID()) == _transferredDonorQuestItems.end()) {
                        continue; // Keep player's own quest items on the player
                    }
                    _transferredDonorQuestItems.erase(boundObj->GetFormID());
                } else if (!a_fromActor->IsPlayerRef() && a_toActor->IsPlayerRef()) {
                    _transferredDonorQuestItems.insert(boundObj->GetFormID());
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
                equipMgr->EquipObject(a_toActor, wornRec.object, xList, equipCount, slot, false, a_forceEquip, false, true);
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

    bool InventoryManager::StashPlayerToPuppet(RE::Actor* a_player, RE::Actor* a_puppet) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        if (!_hasDragonbornBaseline) {
            auto& pRuntime = a_player->GetActorRuntimeData();
            _savedPlayerLeftSpell = skyrim_cast<RE::SpellItem*>(pRuntime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]);
            _savedPlayerRightSpell = skyrim_cast<RE::SpellItem*>(pRuntime.selectedSpells[RE::Actor::SlotTypes::kRightHand]);
            _savedPlayerShout = skyrim_cast<RE::TESShout*>(pRuntime.selectedPower);

            StashPlayerDynamicSpells(a_player);
            _hasDragonbornBaseline = true;
        }

        if (a_puppet) {
            TransferAllItems(a_player, a_puppet, true);
            SKSE::log::info("Stashed player inventory onto puppet {:08X}.",
                static_cast<std::uint32_t>(a_puppet->GetFormID()));
        }
        return true;
    }

    bool InventoryManager::TransferDonorToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_donor || !a_player) return false;

        CopyDynamicSpells(a_donor, a_player);
        TransferAllItems(a_donor, a_player, false);
        return true;
    }

    bool InventoryManager::TransferPlayerToDonor(RE::Actor* a_player, RE::Actor* a_donor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        if (a_donor) {
            TransferAllItems(a_player, a_donor, true);
        }

        // Everything castable still on the player is either a donor copy or was learned while impersonating.
        std::vector<RE::SpellItem*> impersonationSpells;
        for (auto* spell : a_player->GetActorRuntimeData().addedSpells) {
            if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                impersonationSpells.push_back(spell);
            }
        }
        for (auto* spell : impersonationSpells) {
            if (a_donor) {
                a_donor->AddSpell(spell);
            }
            a_player->RemoveSpell(spell);
        }
        RemoveCopiedSpells(a_player);
        _transferredDonorQuestItems.clear();
        return true;
    }

    bool InventoryManager::RestorePuppetToPlayer(RE::Actor* a_puppet, RE::Actor* a_player) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player || !a_puppet) return false;

        RestorePlayerDynamicSpells(a_player);
        TransferAllItems(a_puppet, a_player, false);

        auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        if (defaultObjMgr && equipMgr) {
            auto* leftSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip);
            auto* rightSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);
            if (_savedPlayerLeftSpell && leftSlot) {
                equipMgr->EquipSpell(a_player, _savedPlayerLeftSpell, leftSlot);
            }
            if (_savedPlayerRightSpell && rightSlot) {
                equipMgr->EquipSpell(a_player, _savedPlayerRightSpell, rightSlot);
            }
            if (_savedPlayerShout) {
                equipMgr->EquipShout(a_player, _savedPlayerShout);
            }
        }
        _savedPlayerLeftSpell = nullptr;
        _savedPlayerRightSpell = nullptr;
        _savedPlayerShout = nullptr;
        _hasDragonbornBaseline = false;
        return true;
    }

    bool InventoryManager::StoreAndTransferToPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player || !a_donor) return false;

        if (a_storageFollower || !_hasDragonbornBaseline) {
            StashPlayerToPuppet(a_player, a_storageFollower);
        }
        return TransferDonorToPlayer(a_donor, a_player);
    }

    bool InventoryManager::RestoreAndTransferFromPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        TransferPlayerToDonor(a_player, a_donor);
        if (a_storageFollower) {
            RestorePuppetToPlayer(a_storageFollower, a_player);
        }
        return true;
    }

}
