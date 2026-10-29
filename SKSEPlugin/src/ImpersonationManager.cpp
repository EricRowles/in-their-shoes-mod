#include "PCH.h"
#include "ImpersonationManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "Serialization.h"
#include "IdleAIManager.h"
#include "DialogueManager.h"

#include <unordered_set>
#include <RE/E/ExtraTextDisplayData.h>
#include <RE/C/CommandTable.h>
#include <RE/T/TESCondition.h>
#include <RE/T/TESPackage.h>
#include <RE/B/BGSRefAlias.h>
#include <RE/E/ExtraAliasInstanceArray.h>
#include <RE/T/TESQuest.h>
#include <RE/T/TESDataHandler.h>

namespace InTheirShoes {

    enum class IdentityCondition : std::uint8_t {
        kGetInFaction = 0,
        kGetIsID,
        kGetFactionRank,
        kGetIsReference,
        kGetIsAliasRef,
        kGetIsVoiceType,
        kIsInList,
        kTotal
    };

    static std::array<RE::SCRIPT_FUNCTION::Condition_t*, static_cast<std::size_t>(IdentityCondition::kTotal)> s_originalConditions{ nullptr };

    // --- Category A: Identity Thunk ---
    template <IdentityCondition Cond>
    static bool IdentityThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        constexpr auto idx = static_cast<std::size_t>(Cond);
        auto* orig = s_originalConditions[idx];
        if (!orig) return false;
        if (!mgr.IsActive()) {
            return orig(a_thisObj, a_p1, a_p2, a_result);
        }

        auto* player = mgr.GetPlayer();
        auto* donor = mgr.GetDonor();
        auto* puppet = mgr.GetPuppet();
        bool isSelfDialogue = DialogueManager::GetSingleton().IsInSelfDialogue();

        // 1. Determine whether the subject evaluates as the Donor or the Dragonborn
        bool evalAsDonor = false;
        bool evalAsDragonborn = false;

        if (isSelfDialogue) {
            // In self-dialogue, puppet is the speaker proxying donor; player is the listener (Dragonborn)
            if (puppet && a_thisObj == puppet) {
                evalAsDonor = true;
            } else if (a_thisObj == player) {
                evalAsDragonborn = true;
            }
        } else {
            // In normal gameplay, player represents the donor; puppet represents the Dragonborn
            if (a_thisObj == player) {
                evalAsDonor = true;
            } else if (puppet && a_thisObj == puppet) {
                evalAsDragonborn = true;
            }
        }

        if (evalAsDonor) {
            if constexpr (Cond == IdentityCondition::kGetIsID) {
                auto* targetForm = static_cast<RE::TESForm*>(a_p1);
                if (donor && targetForm) {
                    if (targetForm == donor || targetForm == donor->GetBaseObject()) {
                        a_result = 1.0;
                        return true;
                    }
                    if (auto* actorBase = donor->GetActorBase()) {
                        if (targetForm == actorBase || targetForm == actorBase->GetRootFaceNPC()) {
                            a_result = 1.0;
                            return true;
                        }
                    }
                    if (auto* extraLeveled = donor->extraList.GetByType<RE::ExtraLeveledCreature>()) {
                        if (targetForm == extraLeveled->originalBase || targetForm == extraLeveled->templateBase) {
                            a_result = 1.0;
                            return true;
                        }
                    }
                    double donorRes = 0.0;
                    if (orig(donor, a_p1, a_p2, donorRes) && donorRes != 0.0) {
                        a_result = 1.0;
                        return true;
                    }
                }
                // Fall back to player's original identity for player perks, spells, and magic effects
                if (a_thisObj == player) {
                    return orig(player, a_p1, a_p2, a_result);
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetIsVoiceType) {
                auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_p1);
                if ((isSelfDialogue || mgr.IsNPCVoiceEnabled()) && mgr.GetDonorVoiceType()) {
                    a_result = (targetVoice && targetVoice == mgr.GetDonorVoiceType()) ? 1.0 : 0.0;
                    return true;
                }
                auto* origVoice = mgr.GetOrigPlayerVoiceType();
                a_result = (origVoice && targetVoice && targetVoice == origVoice) ? 1.0 : 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetInFaction) {
                if (donor) {
                    double donorRes = 0.0;
                    if (orig(donor, a_p1, a_p2, donorRes) && donorRes != 0.0) {
                        a_result = 1.0;
                        return true;
                    }
                }
                if (auto rank = mgr.GetDonorAliasFactionRank(static_cast<RE::TESFaction*>(a_p1))) {
                    a_result = 1.0;
                    return true;
                }
                if (a_thisObj == player) {
                    return orig(player, a_p1, a_p2, a_result);
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetFactionRank) {
                if (donor) {
                    double donorRes = -1.0;
                    if (orig(donor, a_p1, a_p2, donorRes) && donorRes >= 0.0) {
                        a_result = donorRes;
                        return true;
                    }
                }
                if (auto rank = mgr.GetDonorAliasFactionRank(static_cast<RE::TESFaction*>(a_p1))) {
                    a_result = *rank;
                    return true;
                }
                if (a_thisObj == player) {
                    return orig(player, a_p1, a_p2, a_result);
                }
                a_result = -1.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetIsReference) {
                auto* targetRef = static_cast<RE::TESObjectREFR*>(a_p1);
                if (donor && targetRef && targetRef == donor) {
                    a_result = 1.0;
                    return true;
                }
                return orig(a_thisObj, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetIsAliasRef) {
                double res = 0.0;
                if (orig(player, a_p1, a_p2, res) && res != 0.0) {
                    a_result = res;
                    return true;
                }
                if (donor && orig(donor, a_p1, a_p2, res) && res != 0.0) {
                    a_result = res;
                    return true;
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kIsInList) {
                double res = 0.0;
                if (donor && orig(donor, a_p1, a_p2, res) && res != 0.0) {
                    a_result = res;
                    return true;
                }
                return orig(player, a_p1, a_p2, a_result);
            }
        }

        // 2. If evaluating as Dragonborn (puppet outside self-dialogue, or player during self-dialogue)
        if (evalAsDragonborn) {
            if constexpr (Cond == IdentityCondition::kGetIsID) {
                auto* targetForm = static_cast<RE::TESForm*>(a_p1);
                if (targetForm && (targetForm == player || targetForm == player->GetActorBase() ||
                    targetForm->GetFormID() == Forms::kPlayerBaseID || targetForm->GetFormID() == Forms::kPlayerRefID)) {
                    a_result = 1.0;
                    return true;
                }
                return orig(player, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetIsVoiceType) {
                auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_p1);
                auto* origVoice = mgr.GetOrigPlayerVoiceType();
                a_result = (origVoice && targetVoice && targetVoice == origVoice) ? 1.0 : 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetInFaction) {
                return orig(player, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetFactionRank) {
                return orig(player, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetIsReference) {
                auto* targetRef = static_cast<RE::TESObjectREFR*>(a_p1);
                if (targetRef && targetRef == player) {
                    a_result = 1.0;
                    return true;
                }
                return orig(player, a_p1, a_p2, a_result);
            } else {
                return orig(player, a_p1, a_p2, a_result);
            }
        }

        return orig(a_thisObj, a_p1, a_p2, a_result);
    }

    // --- Category B: Spatial Condition Proxying ---
    enum class SpatialCondition : std::uint8_t {
        kGetInCell = 0,
        kGetInCurrentLoc,
        kGetDistance,
        kIsInInterior,
        kGetInSameCell,
        kGetInWorldspace,
        kGetPos,
        kGetHeadingAngle,
        kGetAngle,
        kTotal
    };

    static std::array<RE::SCRIPT_FUNCTION::Condition_t*, static_cast<std::size_t>(SpatialCondition::kTotal)> s_originalSpatialConditions{ nullptr };

    template <SpatialCondition Cond>
    static bool SpatialConditionThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        constexpr auto idx = static_cast<std::size_t>(Cond);
        auto* orig = s_originalSpatialConditions[idx];
        if (!orig) return false;
        if (!mgr.IsActive()) {
            return orig(a_thisObj, a_p1, a_p2, a_result);
        }

        auto* player = mgr.GetPlayer();
        auto* evalThis = mgr.ResolveSpatialRef(a_thisObj);
        void* evalP1 = a_p1;

        if constexpr (Cond == SpatialCondition::kGetDistance ||
                      Cond == SpatialCondition::kGetInSameCell ||
                      Cond == SpatialCondition::kGetHeadingAngle) {
            evalP1 = mgr.ResolveSpatialRef(static_cast<RE::TESObjectREFR*>(a_p1));

            // Per requirements.md ("Conditions that require the player to be nearby will still target
            // the impersonator"), self-distance / same-cell checks between donor and player resolve
            // against the impersonating player.
            if (evalThis == player && evalP1 == player) {
                if constexpr (Cond == SpatialCondition::kGetDistance) {
                    a_result = 0.0;
                    return true;
                } else if constexpr (Cond == SpatialCondition::kGetInSameCell) {
                    a_result = 1.0;
                    return true;
                } else if constexpr (Cond == SpatialCondition::kGetHeadingAngle) {
                    a_result = 0.0;
                    return true;
                }
            }
        }

        return orig(evalThis, evalP1, a_p2, a_result);
    }

    RE::TESObjectREFR* ImpersonationManager::ResolveSpatialRef(RE::TESObjectREFR* a_ref) const {
        if (!a_ref || !IsActive()) return a_ref;
        auto* donor = GetDonor();
        auto* puppet = GetPuppet();
        if ((donor && a_ref == donor) ||
            (puppet && a_ref == puppet && DialogueManager::GetSingleton().IsInSelfDialogue())) {
            return GetPlayer();
        }
        return a_ref;
    }

    bool ImpersonationManager::IsModInternalQuest(RE::TESQuest* a_quest) {
        return a_quest && a_quest == Forms::itsQuest;
    }

    RE::BGSVoiceType* ImpersonationManager::GetNPCVoiceType(const RE::TESNPC* a_npc) {
        if (!a_npc) return nullptr;
        if (a_npc->voiceType) return a_npc->voiceType;
        if (_origGetObjectVoiceType.address() != 0) {
            return _origGetObjectVoiceType(a_npc);
        }
        if (a_npc->race) {
            std::size_t sexIdx = (a_npc->GetSex() == RE::SEX::kFemale) ? 1 : 0;
            return a_npc->race->defaultVoiceTypes[sexIdx];
        }
        return nullptr;
    }

    RE::BGSVoiceType* ImpersonationManager::Hook_GetObjectVoiceType(const RE::TESNPC* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            auto* donorVoice = mgr.GetDonorVoiceType();
            if (mgr.IsActive() && donorVoice) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && a_this == player->GetActorBase()) {
                    if (mgr.IsNPCVoiceEnabled()) {
                        return donorVoice;
                    }
                    return mgr.GetOrigPlayerVoiceType();
                }

                auto* puppet = mgr.GetPuppet();
                if (puppet && a_this == puppet->GetActorBase()) {
                    auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                    if (topicMgr && topicMgr->forceGoodbye) {
                        return nullptr;
                    }
                    return donorVoice;
                }
                if (a_this->voiceType) {
                    return a_this->voiceType;
                }
            }
        }
        return _origGetObjectVoiceType(a_this);
    }

    void ImpersonationManager::ForwardPuppetFactionsToDonor() {
        auto* puppet = GetPuppet();
        auto* donor = GetDonor();
        if (!puppet || !donor) return;

        auto* extraFaction = puppet->extraList.GetByType<RE::ExtraFactionChanges>();
        if (!extraFaction || extraFaction->factionChanges.empty()) return;

        for (const auto& fRank : extraFaction->factionChanges) {
            if (!fRank.faction) continue;
            donor->AddToFaction(fRank.faction, fRank.rank);
            SKSE::log::info("Forwarded faction {:08X} rank {} from puppet to donor {:08X}.",
                fRank.faction->GetFormID(), fRank.rank, donor->GetFormID());

            if (fRank.rank >= 0 &&
                (fRank.faction == Forms::currentFollowerFaction || fRank.faction == Forms::playerFollowerFaction)) {
                donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            }
        }
        extraFaction->factionChanges.clear();
    }

    void ImpersonationManager::Hook_Player_Update(RE::Actor* a_this, float a_delta) {
        _origPlayerUpdate(a_this, a_delta);

        if (a_this && IsMainThread()) {
            ImpersonationManager::GetSingleton().Tick();
        }
    }

    RE::BGSLocation* ImpersonationManager::Hook_Player_GetEditorLocation1(const RE::TESObjectREFR* a_this) {
        if (a_this && ImpersonationManager::GetSingleton().IsActive()) {
            if (auto* loc = a_this->GetCurrentLocation()) {
                return loc;
            }
        }
        return _origPlayerGetEditorLocation1(a_this);
    }

    bool ImpersonationManager::Hook_Player_GetEditorLocation2(
        RE::TESObjectREFR* a_this,
        RE::NiPoint3& a_outPos,
        RE::NiPoint3& a_outRot,
        RE::TESForm*& a_outWorldOrCell,
        RE::TESObjectCELL* a_fallback)
    {
        if (a_this && ImpersonationManager::GetSingleton().IsActive()) {
            a_outPos = a_this->GetPosition();
            a_outRot = a_this->GetAngle();
            auto* cell = a_this->GetParentCell();
            auto* world = a_this->GetWorldspace();
            a_outWorldOrCell = world ? static_cast<RE::TESForm*>(world) : static_cast<RE::TESForm*>(cell);
            return true;
        }
        return _origPlayerGetEditorLocation2(a_this, a_outPos, a_outRot, a_outWorldOrCell, a_fallback);
    }

    void ImpersonationManager::InstallHooks() {
        if (_hooksInstalled) return;

        // Hook TESNPC::GetObjectVoiceType on primary vtable (index 0x49)
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESNPC[0] };
        _origGetObjectVoiceType = vtbl.write_vfunc(0x49, &Hook_GetObjectVoiceType);
        SKSE::log::info("Hooked TESNPC::GetObjectVoiceType at VTABLE_TESNPC[0] idx 0x49 (orig: {:016X})",
            _origGetObjectVoiceType.address());

        // Hook PlayerCharacter vtable (0x3C GetEditorLocation1, 0x3D GetEditorLocation2, 0xAD Update)
        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerGetEditorLocation1 = playerVtbl.write_vfunc(0x3C, &Hook_Player_GetEditorLocation1);
        _origPlayerGetEditorLocation2 = playerVtbl.write_vfunc(0x3D, &Hook_Player_GetEditorLocation2);
        _origPlayerUpdate = playerVtbl.write_vfunc(0xAD, &Hook_Player_Update);
        SKSE::log::info("Hooked PlayerCharacter editor location & update vfuncs (0x3C, 0x3D, 0xAD)");

        auto hookEntry = [](IdentityCondition cond, std::string_view name, RE::SCRIPT_FUNCTION::Condition_t* thunk) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(name);
            if (func) {
                auto idx = static_cast<std::size_t>(cond);
                s_originalConditions[idx] = func->conditionFunction;
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(thunk));
                SKSE::log::info("Hooked condition '{}' via LocateScriptCommand (orig: {:016X})",
                    name, reinterpret_cast<std::uintptr_t>(s_originalConditions[idx]));
                return true;
            } else {
                SKSE::log::error("Failed to locate script command '{}'!", name);
                return false;
            }
        };

        std::size_t hookedCount = 0;
        if (hookEntry(IdentityCondition::kGetInFaction, "GetInFaction"sv, &IdentityThunk<IdentityCondition::kGetInFaction>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsID, "GetIsID"sv, &IdentityThunk<IdentityCondition::kGetIsID>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetFactionRank, "GetFactionRank"sv, &IdentityThunk<IdentityCondition::kGetFactionRank>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsReference, "GetIsReference"sv, &IdentityThunk<IdentityCondition::kGetIsReference>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsAliasRef, "GetIsAliasRef"sv, &IdentityThunk<IdentityCondition::kGetIsAliasRef>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsVoiceType, "GetIsVoiceType"sv, &IdentityThunk<IdentityCondition::kGetIsVoiceType>)) hookedCount++;
        if (hookEntry(IdentityCondition::kIsInList, "IsInList"sv, &IdentityThunk<IdentityCondition::kIsInList>)) hookedCount++;

        auto hookSpatialEntry = [](SpatialCondition cond, std::string_view name, RE::SCRIPT_FUNCTION::Condition_t* thunk) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(name);
            if (func) {
                auto idx = static_cast<std::size_t>(cond);
                s_originalSpatialConditions[idx] = func->conditionFunction;
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(thunk));
                SKSE::log::info("Hooked spatial condition '{}' via LocateScriptCommand (orig: {:016X})",
                    name, reinterpret_cast<std::uintptr_t>(s_originalSpatialConditions[idx]));
                return true;
            } else {
                SKSE::log::error("Failed to locate script command '{}'!", name);
                return false;
            }
        };

        if (hookSpatialEntry(SpatialCondition::kGetInCell, "GetInCell"sv, &SpatialConditionThunk<SpatialCondition::kGetInCell>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetInCurrentLoc, "GetInCurrentLoc"sv, &SpatialConditionThunk<SpatialCondition::kGetInCurrentLoc>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetDistance, "GetDistance"sv, &SpatialConditionThunk<SpatialCondition::kGetDistance>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kIsInInterior, "IsInInterior"sv, &SpatialConditionThunk<SpatialCondition::kIsInInterior>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetInSameCell, "GetInSameCell"sv, &SpatialConditionThunk<SpatialCondition::kGetInSameCell>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetInWorldspace, "GetInWorldspace"sv, &SpatialConditionThunk<SpatialCondition::kGetInWorldspace>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetPos, "GetPos"sv, &SpatialConditionThunk<SpatialCondition::kGetPos>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetHeadingAngle, "GetHeadingAngle"sv, &SpatialConditionThunk<SpatialCondition::kGetHeadingAngle>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetAngle, "GetAngle"sv, &SpatialConditionThunk<SpatialCondition::kGetAngle>)) hookedCount++;

        constexpr auto kExpectedHooks = static_cast<std::size_t>(IdentityCondition::kTotal) + static_cast<std::size_t>(SpatialCondition::kTotal);
        if (hookedCount == kExpectedHooks) {
            SKSE::log::info("Installed all {} script condition hooks.", hookedCount);
        } else {
            // A missing hook leaves the donor's conditions answered for the wrong actor; the errors above name it
            SKSE::log::error("Installed only {} of {} script condition hooks; impersonation conditions will be wrong.",
                hookedCount, kExpectedHooks);
        }

        InstallOutfitHook();

        _hooksInstalled = true;
    }

    void ImpersonationManager::InstallOutfitHook() {
        // Actor::AddWornOutfit isn't virtual. Its callers (inventory init when an actor loads, and Papyrus
        // SetOutfit) are found by scanning the code for direct calls to it, so no per-version offsets are needed.
        const auto target = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(19266, 19692) }.address();
        const auto text = REL::Module::get().segment(REL::Segment::textx);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.address());

        std::vector<std::uintptr_t> sites;
        for (std::size_t i = 0; i + 5 <= text.size(); ++i) {
            if (bytes[i] != 0xE8) continue;
            std::int32_t rel = 0;
            std::memcpy(&rel, bytes + i + 1, sizeof(rel));
            if (text.address() + i + 5 + rel == target) {
                sites.push_back(text.address() + i);
            }
        }

        auto& trampoline = SKSE::GetTrampoline();
        for (auto site : sites) {
            _origAddWornOutfit = trampoline.write_call<5>(site, &Hook_AddWornOutfit);
        }
        if (sites.empty()) {
            SKSE::log::warn("Found no calls to Actor::AddWornOutfit; donors' outfits will not be disabled");
        } else {
            SKSE::log::info("Hooked {} call sites of Actor::AddWornOutfit", sites.size());
        }
    }

    void ImpersonationManager::Hook_AddWornOutfit(RE::Actor* a_this, RE::BGSOutfit* a_outfit, bool a_forceUpdate) {
        if (a_this && GetSingleton().IsOutfitDisabled(a_this)) {
            return;
        }
        _origAddWornOutfit(a_this, a_outfit, a_forceUpdate);
    }

    bool ImpersonationManager::IsOutfitDisabled(const RE::Actor* a_actor) const {
        std::scoped_lock lock(_outfitDisabledLock);
        return _outfitDisabled.contains(a_actor->GetFormID());
    }

    void ImpersonationManager::SaveOutfitDisabled(SKSE::SerializationInterface* a_serde) const {
        std::scoped_lock lock(_outfitDisabledLock);
        if (_outfitDisabled.empty()) return;
        if (!a_serde->OpenRecord(Serialization::kOutfitDisabledRecord, Serialization::kOutfitDisabledVersion)) return;
        const auto count = static_cast<std::uint32_t>(_outfitDisabled.size());
        a_serde->WriteRecordData(&count, sizeof(count));
        for (auto id : _outfitDisabled) {
            a_serde->WriteRecordData(&id, sizeof(id));
        }
    }

    void ImpersonationManager::LoadOutfitDisabled(SKSE::SerializationInterface* a_serde) {
        std::scoped_lock lock(_outfitDisabledLock);
        std::uint32_t count = 0;
        if (!a_serde->ReadRecordData(&count, sizeof(count))) return;
        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID id = 0;
            if (!a_serde->ReadRecordData(&id, sizeof(id))) break;
            if (a_serde->ResolveFormID(id, id)) {
                _outfitDisabled.insert(id);
            }
        }
        SKSE::log::info("Loaded {} actors with their outfit disabled.", _outfitDisabled.size());
    }

    void ImpersonationManager::Initialize() {
        InstallHooks();

        if (Forms::storageMarker) {
            _storageMarker = Forms::storageMarker->GetHandle();
        }
    }

    void ImpersonationManager::AttachDonor(RE::Actor* a_donor) {
        _donorKeepAlive.reset(a_donor);
        _donorActor.store(a_donor, std::memory_order_release);
        auto* base = a_donor ? a_donor->GetActorBase() : nullptr;
        _donorVoiceType.store(base ? GetNPCVoiceType(base) : nullptr, std::memory_order_release);
    }

    void ImpersonationManager::DetachDonor() {
        _donorActor.store(nullptr, std::memory_order_release);
        _donorVoiceType.store(nullptr, std::memory_order_release);
        _donorKeepAlive.reset();
    }

    void ImpersonationManager::AttachPuppet(RE::Actor* a_puppet) {
        _puppetKeepAlive.reset(a_puppet);
        _puppetActor.store(a_puppet, std::memory_order_release);
    }

    void ImpersonationManager::SetBaseline(Baseline a_baseline) {
        _baseline = std::move(a_baseline);
        _origVoiceType.store(_baseline->face.voiceType, std::memory_order_release);
    }

    void ImpersonationManager::CaptureBaseline(RE::Actor* a_player) {
        Baseline baseline;
        FaceManager::GetSingleton().CaptureSnapshot(a_player, baseline.face);
        StatManager::CaptureSnapshot(a_player, baseline.stats);
        InventoryManager::StashPlayerSpells(a_player, baseline.spells);
        SetBaseline(std::move(baseline));
        SKSE::log::info("Captured Dragonborn baseline (name '{}').", _baseline->face.fullName.c_str());
    }

    bool ImpersonationManager::IsPlayerTransformed() {
        // The Werewolf and Vampire Lord quests run for the whole transformation, including the moments
        // around the race swap, and restore the race they saved when it began. (Confirmed for Vampire Lord: the
        // quest runs from the start of the transformation until ~2s after changing back.)
        return IsQuestRunning(Forms::playerWerewolfQuest) || IsQuestRunning(Forms::dlc1PlayerVampireQuest);
    }

    bool ImpersonationManager::Impersonate(RE::Actor* a_donor, RE::Actor* a_puppet) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_donor || !a_puppet || a_donor == player || a_donor == a_puppet) {
            SKSE::log::warn("Impersonate called with invalid actors.");
            return false;
        }
        // The baseline (or the session being handed back) would capture the beast race
        if (IsPlayerTransformed()) {
            SKSE::log::warn("Impersonate refused: the player is transformed.");
            return false;
        }

        auto& faces = FaceManager::GetSingleton();

        if (IsActive()) {
            // Donor-to-donor switch: hand the current state back to the old donor, who takes the player's place
            // (the quest script then moves the player to the new donor's spot)
            auto* oldDonor = GetDonor();
            if (oldDonor == a_donor) return false;
            EndSession();
            if (oldDonor) {
                ReturnStateToDonor(oldDonor);
                oldDonor->MoveTo(player);
                oldDonor->StopCombat();
                oldDonor->EvaluatePackage(false, false);
            }
        } else {
            // First impersonation: the puppet takes on the Dragonborn's look, stats and gear
            CaptureBaseline(player);
            AttachPuppet(a_puppet);
            ConfigurePuppet(a_puppet);
            StatManager::CopyStats(player, a_puppet);
            InventoryManager::TransferAllItems(player, a_puppet, true, _donorQuestItems);
        }

        // Kept for the session: the donor's rendered skin and hair tints can only be read while its 3D is
        // loaded, and on a later load it is parked out of sight
        FaceSnapshot donorFace;
        if (faces.CaptureSnapshot(a_donor, donorFace)) {
            faces.ApplySnapshot(player, donorFace);
            _donorFace = std::move(donorFace);
        } else {
            SKSE::log::error("Failed to capture the appearance of donor {:08X}.", a_donor->GetFormID());
            _donorFace.reset();
        }
        StatManager::CopyStats(a_donor, player);
        InventoryManager::CopyDonorSpells(a_donor, player);
        InventoryManager::TransferAllItems(a_donor, player, false, _donorQuestItems);

        if (auto marker = _storageMarker.get()) {
            a_donor->MoveTo(marker.get());
        }

        BeginSession(a_donor);
        return true;
    }

    bool ImpersonationManager::Revert() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !IsActive()) return false;
        // Restoring the baseline race mid-transformation would be undone (to the donor's race) when it ends
        if (IsPlayerTransformed()) {
            SKSE::log::warn("Revert refused: the player is transformed.");
            return false;
        }

        auto* donor = GetDonor();
        auto* puppet = GetPuppet();
        EndSession();
        if (donor) {
            ReturnStateToDonor(donor);
        }

        if (_baseline) {
            FaceManager::GetSingleton().ApplySnapshot(player, _baseline->face);
            StatManager::ApplySnapshot(player, _baseline->stats);
        }
        if (puppet) {
            InventoryManager::TransferAllItems(puppet, player, false, _donorQuestItems);
        }
        if (_baseline) {
            InventoryManager::RestorePlayerSpells(player, _baseline->spells);
        }

        ReleasePuppet();
        _baseline.reset();
        _donorFace.reset();
        _origVoiceType.store(nullptr, std::memory_order_release);
        _donorQuestItems.clear();
        SKSE::log::info("Reverted to the Dragonborn.");
        return true;
    }

    void ImpersonationManager::ReturnStateToDonor(RE::Actor* a_donor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        StatManager::CopyStats(player, a_donor);
        InventoryManager::TransferAllItems(player, a_donor, true, _donorQuestItems);
        InventoryManager::ReturnSpellsToDonor(player, a_donor);
        _donorQuestItems.clear();
    }

    void ImpersonationManager::BeginSession(RE::Actor* a_donor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        AttachDonor(a_donor);
        _active.store(true, std::memory_order_release);

        SKSE::log::info("Beginning impersonation: Donor {:08X} ({})", a_donor->GetFormID(), a_donor->GetName());

        {
            std::scoped_lock lock(_outfitDisabledLock);
            _outfitDisabled.insert(a_donor->GetFormID());
        }

        IdleAIManager::GetSingleton().ClearPlayerPackages();
        SwapDonorAliasesToPlayer(a_donor, player);
        DialogueManager::GetSingleton().InvalidateIdleQuestCache();

        if (auto* puppet = GetPuppet()) {
            ApplyPuppetFollowState(puppet);
            puppet->EvaluatePackage(false, false);
        }

        IdleAIManager::GetSingleton().OnImpersonationChanged(true);
    }

    void ImpersonationManager::EndSession() {
        auto* donor = GetDonor();
        SKSE::log::info("Ending impersonation of donor {:08X}", donor ? donor->GetFormID() : 0u);

        ForwardPuppetFactionsToDonor();
        RestoreSwappedAliases(donor);
        DialogueManager::GetSingleton().InvalidateIdleQuestCache();

        IdleAIManager::GetSingleton().OnImpersonationChanged(false);

        _active.store(false, std::memory_order_release);
        DetachDonor();
    }

    void ImpersonationManager::ApplyPuppetFollowState(RE::Actor* a_puppet) {
        // The follow package itself lives on the PlayerPuppet alias, gated by the ITS_PuppetFollows global.
        // Teammate status lets the puppet travel through load doors with the player.
        if (_puppetFollows) {
            a_puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
        } else {
            a_puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
        }
    }

    void ImpersonationManager::ConfigurePuppet(RE::Actor* a_puppet) {
        if (_baseline) {
            FaceManager::GetSingleton().ApplySnapshot(a_puppet, _baseline->face, true, true);
            // The puppet has usually just been moved in, and its 3D may still be loading from the old
            // base data; a 3D reset before it attaches is lost. Reapply once the 3D is in place.
            _puppetRefreshPending = true;
        }
        ApplyPuppetFollowState(a_puppet);
        a_puppet->AllowPCDialogue(true);
        a_puppet->AllowBleedoutDialogue(true);
        a_puppet->StopCombat();
        a_puppet->EvaluatePackage(true, true);
        SKSE::log::info("Configured puppet {:08X} (followsPlayer={}).", a_puppet->GetFormID(), _puppetFollows);
    }

    void ImpersonationManager::ReleasePuppet() {
        if (auto* puppet = GetPuppet()) {
            puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
            puppet->EvaluatePackage(false, false);
        }
        AttachPuppet(nullptr);
    }

    void ImpersonationManager::SetPuppetFollowsPlayer(bool a_follows) {
        _puppetFollows = a_follows;
        auto* puppet = GetPuppet();
        if (!puppet || !IsActive()) return;

        ApplyPuppetFollowState(puppet);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (a_follows && player &&
            (puppet->GetParentCell() != player->GetParentCell() || puppet->GetPosition().GetDistance(player->GetPosition()) > 1000.0f)) {
            puppet->MoveTo(player);
            SKSE::log::info("SetPuppetFollowsPlayer: Teleported puppet to player.");
        }
        puppet->EvaluatePackage(true, true);
        SKSE::log::info("Updated puppet follows player: {}", a_follows);
    }

    void ImpersonationManager::RefreshPuppetAppearanceWhenLoaded() {
        auto* puppet = GetPuppet();
        if (!puppet || !_baseline) {
            _puppetRefreshPending = false;
            return;
        }
        if (!puppet->Is3DLoaded()) return;

        _puppetRefreshPending = false;
        FaceManager::GetSingleton().ApplySnapshot(puppet, _baseline->face, true, true);
        SKSE::log::info("Reapplied Dragonborn appearance to puppet {:08X} after its 3D loaded.", puppet->GetFormID());
    }

    void ImpersonationManager::Tick() {
        IdleAIManager::GetSingleton().Update();
        DialogueManager::GetSingleton().Update();

        if (_puppetRefreshPending) {
            RefreshPuppetAppearanceWhenLoaded();
        }

        if (!IsActive() || _leashQueued) return;

        auto* donor = GetDonor();
        auto marker = _storageMarker.get();
        if (!donor || !marker) return;

        bool needsLeash = donor->GetParentCell() != marker->GetParentCell() ||
                          donor->GetPosition().GetDistance(marker->GetPosition()) > 500.0f;
        if (!needsLeash) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        auto* cell = player->GetParentCell();
        bool worldReady = cell && cell->IsAttached() && player->Get3D() &&
                          !(ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
        if (!worldReady) return;

        SKSE::log::info("Tick: Donor {:08X} moved away from holding marker; leashing back.", donor->GetFormID());
        _leashQueued = true;
        SKSE::GetTaskInterface()->AddTask([donorHandle = donor->GetHandle(), markerHandle = _storageMarker]() {
            ImpersonationManager::GetSingleton()._leashQueued = false;
            auto d = donorHandle.get();
            auto m = markerHandle.get();
            if (d && m) {
                d->MoveTo(m.get());
            }
        });
    }

    static std::unordered_map<RE::TESFaction*, std::int8_t> GetFactionRanks(RE::Actor* a_actor) {
        std::unordered_map<RE::TESFaction*, std::int8_t> ranks;
        a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
            if (a_faction && a_rank >= 0) {
                ranks[a_faction] = a_rank;
            }
            return false;
        });
        return ranks;
    }

    void ImpersonationManager::SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        _swappedAliases.clear();
        {
            std::scoped_lock lock(_donorAliasFactionsLock);
            _donorAliasFactions.clear();
        }

        auto* aliasArray = a_donor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (!aliasArray) return;

        std::vector<std::pair<RE::TESQuest*, std::uint32_t>> toSwap;
        {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (!inst || !inst->quest || !inst->alias) continue;
                if (!IsQuestRunning(inst->quest) || IsModInternalQuest(inst->quest)) continue;
                if (inst->alias->QType() != "Ref"sv) continue;

                auto* refAlias = static_cast<const RE::BGSRefAlias*>(inst->alias);
                if (refAlias->GetReference() == a_donor) {
                    toSwap.emplace_back(inst->quest, refAlias->aliasID);
                }
            }
        }

        // Alias factions leave the donor with the aliases: whatever factions the swap takes away are those
        auto ranksBefore = GetFactionRanks(a_donor);
        for (const auto& [quest, aid] : toSwap) {
            quest->ForceRefIntoAlias(aid, a_player);
            _swappedAliases.push_back({ quest->GetFormID(), aid });
            SKSE::log::info("SwapDonorAliasesToPlayer: Swapped donor {:08X} to player in quest {:08X} alias ID {}",
                a_donor->GetFormID(), quest->GetFormID(), aid);
        }

        auto ranksAfter = GetFactionRanks(a_donor);
        std::scoped_lock lock(_donorAliasFactionsLock);
        for (const auto& [faction, rank] : ranksBefore) {
            if (!ranksAfter.contains(faction)) {
                _donorAliasFactions.emplace_back(faction, rank);
                SKSE::log::info("SwapDonorAliasesToPlayer: Donor {:08X} lost alias faction {:08X} (rank {})",
                    a_donor->GetFormID(), faction->GetFormID(), rank);
            }
        }
    }

    std::optional<std::int8_t> ImpersonationManager::GetDonorAliasFactionRank(const RE::TESFaction* a_faction) const {
        std::scoped_lock lock(_donorAliasFactionsLock);
        for (const auto& [faction, rank] : _donorAliasFactions) {
            if (faction == a_faction) return rank;
        }
        return std::nullopt;
    }

    void ImpersonationManager::SaveDonorAliasFactions(SKSE::SerializationInterface* a_serde) const {
        std::scoped_lock lock(_donorAliasFactionsLock);
        if (!IsActive() || _donorAliasFactions.empty()) return;
        if (!a_serde->OpenRecord(Serialization::kDonorAliasFactionsRecord, Serialization::kDonorAliasFactionsVersion)) return;
        const auto count = static_cast<std::uint32_t>(_donorAliasFactions.size());
        a_serde->WriteRecordData(&count, sizeof(count));
        for (const auto& [faction, rank] : _donorAliasFactions) {
            const RE::FormID id = faction->GetFormID();
            a_serde->WriteRecordData(&id, sizeof(id));
            a_serde->WriteRecordData(&rank, sizeof(rank));
        }
    }

    void ImpersonationManager::LoadDonorAliasFactions(SKSE::SerializationInterface* a_serde) {
        std::scoped_lock lock(_donorAliasFactionsLock);
        std::uint32_t count = 0;
        if (!a_serde->ReadRecordData(&count, sizeof(count))) return;
        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID id = 0;
            std::int8_t rank = 0;
            if (!a_serde->ReadRecordData(&id, sizeof(id)) || !a_serde->ReadRecordData(&rank, sizeof(rank))) break;
            if (!a_serde->ResolveFormID(id, id)) continue;
            if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(id)) {
                _donorAliasFactions.emplace_back(faction, rank);
            }
        }
        SKSE::log::info("Loaded {} donor alias factions.", _donorAliasFactions.size());
    }

    void ImpersonationManager::AdoptPuppetAliases() {
        auto* puppet = GetPuppet();
        auto* player = GetPlayer();
        if (!IsActive() || !puppet || !player) return;

        auto* aliasArray = puppet->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (!aliasArray) return;

        std::vector<std::pair<RE::TESQuest*, std::uint32_t>> toAdopt;
        {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (!inst || !inst->quest || !inst->alias) continue;
                if (!IsQuestRunning(inst->quest) || IsModInternalQuest(inst->quest)) continue;
                if (inst->alias->QType() != "Ref"sv) continue;
                if (static_cast<const RE::BGSRefAlias*>(inst->alias)->GetReference() == puppet) {
                    toAdopt.emplace_back(inst->quest, inst->alias->aliasID);
                }
            }
        }

        for (const auto& [quest, aid] : toAdopt) {
            quest->ForceRefIntoAlias(aid, player);
            if (!IsSwappedAlias(quest->GetFormID(), aid)) {
                _swappedAliases.push_back({ quest->GetFormID(), aid });
            }
            SKSE::log::info("AdoptPuppetAliases: Moved puppet's alias ID {} in quest {:08X} to the player", aid, quest->GetFormID());
        }
        if (!toAdopt.empty()) {
            puppet->EvaluatePackage(false, false);
        }
    }

    void ImpersonationManager::RestoreSwappedAliases(RE::Actor* a_donor) {
        auto* player = GetPlayer();
        for (const auto& entry : _swappedAliases) {
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(entry.questFormID);
            if (!IsQuestRunning(quest) || !a_donor) {
                SKSE::log::info("RestoreSwappedAliases: Skipping quest {:08X} alias ID {} (quest stopped or no donor)",
                    entry.questFormID, entry.aliasID);
                continue;
            }

            RE::BGSBaseAlias* baseAlias = nullptr;
            for (auto* a : quest->aliases) {
                if (a && a->aliasID == entry.aliasID) {
                    baseAlias = a;
                    break;
                }
            }

            if (baseAlias && baseAlias->QType() == "Ref"sv) {
                auto* currentRef = static_cast<RE::BGSRefAlias*>(baseAlias)->GetReference();
                if (currentRef && currentRef != player && currentRef != a_donor) {
                    SKSE::log::info("RestoreSwappedAliases: Alias ID {} in quest {:08X} now holds {:08X}, leaving it alone.",
                        entry.aliasID, entry.questFormID, currentRef->GetFormID());
                    continue;
                }
            }

            quest->ForceRefIntoAlias(entry.aliasID, a_donor);
            SKSE::log::info("RestoreSwappedAliases: Restored donor {:08X} into quest {:08X} alias ID {}",
                a_donor->GetFormID(), quest->GetFormID(), entry.aliasID);
        }
        _swappedAliases.clear();
        // The aliases gave their factions back
        std::scoped_lock lock(_donorAliasFactionsLock);
        _donorAliasFactions.clear();
    }

    void ImpersonationManager::SaveSession(SKSE::SerializationInterface* a_serde) const {
        auto* donor = GetDonor();
        auto* puppet = GetPuppet();
        if (!IsActive() || !donor) return;
        if (!a_serde->OpenRecord(Serialization::kSessionRecord, Serialization::kSessionVersion)) return;

        const RE::FormID donorID = donor->GetFormID();
        const RE::FormID puppetID = puppet ? puppet->GetFormID() : 0;
        a_serde->WriteRecordData(&donorID, sizeof(donorID));
        a_serde->WriteRecordData(&puppetID, sizeof(puppetID));

        const auto aliasCount = static_cast<std::uint32_t>(_swappedAliases.size());
        a_serde->WriteRecordData(&aliasCount, sizeof(aliasCount));
        for (const auto& entry : _swappedAliases) {
            a_serde->WriteRecordData(&entry.questFormID, sizeof(entry.questFormID));
            a_serde->WriteRecordData(&entry.aliasID, sizeof(entry.aliasID));
        }

        const auto itemCount = static_cast<std::uint32_t>(_donorQuestItems.size());
        a_serde->WriteRecordData(&itemCount, sizeof(itemCount));
        for (auto id : _donorQuestItems) {
            a_serde->WriteRecordData(&id, sizeof(id));
        }
        SKSE::log::info("Saved impersonation session ({} swapped aliases, {} donor quest items).", aliasCount, itemCount);
    }

    void ImpersonationManager::LoadSession(SKSE::SerializationInterface* a_serde) {
        RE::FormID donorID = 0;
        RE::FormID puppetID = 0;
        std::uint32_t aliasCount = 0;
        if (!a_serde->ReadRecordData(&donorID, sizeof(donorID)) ||
            !a_serde->ReadRecordData(&puppetID, sizeof(puppetID)) ||
            !a_serde->ReadRecordData(&aliasCount, sizeof(aliasCount))) {
            SKSE::log::warn("Impersonation session record is truncated; ignoring it.");
            return;
        }

        std::vector<SwappedAlias> aliases;
        for (std::uint32_t i = 0; i < aliasCount; ++i) {
            SwappedAlias entry;
            if (!a_serde->ReadRecordData(&entry.questFormID, sizeof(entry.questFormID)) ||
                !a_serde->ReadRecordData(&entry.aliasID, sizeof(entry.aliasID))) {
                break;
            }
            if (a_serde->ResolveFormID(entry.questFormID, entry.questFormID)) {
                aliases.push_back(entry);
            }
        }

        std::uint32_t itemCount = 0;
        if (a_serde->ReadRecordData(&itemCount, sizeof(itemCount))) {
            for (std::uint32_t i = 0; i < itemCount; ++i) {
                RE::FormID id = 0;
                if (a_serde->ReadRecordData(&id, sizeof(id)) && a_serde->ResolveFormID(id, id)) {
                    _donorQuestItems.insert(id);
                }
            }
        }

        if (puppetID != 0 && a_serde->ResolveFormID(puppetID, puppetID)) {
            AttachPuppet(RE::TESForm::LookupByID<RE::Actor>(puppetID));
        }

        RE::Actor* donor = nullptr;
        if (a_serde->ResolveFormID(donorID, donorID)) {
            donor = RE::TESForm::LookupByID<RE::Actor>(donorID);
        }
        if (!donor) {
            SKSE::log::warn("Impersonation session donor {:08X} no longer exists; session not restored.", donorID);
            return;
        }

        AttachDonor(donor);
        _swappedAliases = std::move(aliases);
        _active.store(true, std::memory_order_release);
        SKSE::log::info("Restored impersonation session: Donor {:08X} ({} swapped aliases)", donorID, _swappedAliases.size());
    }

    void ImpersonationManager::OnRevert() {
        // Put the in-memory player record back before another save loads over it
        if (_baseline) {
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetActorBase()) {
                FaceManager::GetSingleton().ApplySnapshot(player, _baseline->face, false);
            }
        }

        IdleAIManager::GetSingleton().ClearPlayerPackages();
        _active.store(false, std::memory_order_release);
        DetachDonor();
        AttachPuppet(nullptr);
        _swappedAliases.clear();
        _donorQuestItems.clear();
        {
            std::scoped_lock lock(_outfitDisabledLock);
            _outfitDisabled.clear();
        }
        {
            std::scoped_lock lock(_donorAliasFactionsLock);
            _donorAliasFactions.clear();
        }
        _baseline.reset();
        _donorFace.reset();
        _origVoiceType.store(nullptr, std::memory_order_release);
        _leashQueued = false;
        _puppetRefreshPending = false;
    }

    void ImpersonationManager::OnPostLoadGame() {
        if (!IsActive()) return;

        if (auto* donor = GetDonor()) {
            // The player's record may have reloaded with stale appearance data; reapply the donor's look as
            // captured when impersonation began. Re-capturing now would miss the donor's rendered skin and hair
            // tints, since the parked donor has no 3D. (If the snapshot couldn't be captured, re-capture anyway.)
            if (_donorFace) {
                SKSE::GetTaskInterface()->AddTask([face = *_donorFace]() {
                    FaceManager::GetSingleton().ApplySnapshot(RE::PlayerCharacter::GetSingleton(), face);
                });
            } else {
                SKSE::GetTaskInterface()->AddTask([donorHandle = donor->GetHandle()]() {
                    if (auto d = donorHandle.get()) {
                        FaceManager::GetSingleton().CopyAppearance(d.get(), RE::PlayerCharacter::GetSingleton());
                    }
                });
            }
        }

        // The puppet's base record is not saved, so its appearance must be reapplied every load
        if (auto* puppet = GetPuppet()) {
            ConfigurePuppet(puppet);
        }

        IdleAIManager::GetSingleton().OnImpersonationChanged(true);
    }

}
