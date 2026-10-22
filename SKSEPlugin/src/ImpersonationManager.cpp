#include "PCH.h"
#include "ImpersonationManager.h"
#include "FaceManager.h"
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
        if (!player) {
            player = RE::PlayerCharacter::GetSingleton();
        }
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
                if (player && orig(player, a_p1, a_p2, res) && res != 0.0) {
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
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            }
        }

        // 2. If evaluating as Dragonborn (puppet outside self-dialogue, or player during self-dialogue)
        if (evalAsDragonborn) {
            if constexpr (Cond == IdentityCondition::kGetIsID) {
                auto* targetForm = static_cast<RE::TESForm*>(a_p1);
                if (targetForm && (targetForm == player || (player && targetForm == player->GetActorBase()) ||
                    targetForm->GetFormID() == 0x07 || targetForm->GetFormID() == 0x14)) {
                    a_result = 1.0;
                    return true;
                }
                if (player) {
                    return orig(player, a_p1, a_p2, a_result);
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetIsVoiceType) {
                auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_p1);
                auto* origVoice = mgr.GetOrigPlayerVoiceType();
                a_result = (origVoice && targetVoice && targetVoice == origVoice) ? 1.0 : 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetInFaction) {
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetFactionRank) {
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetIsReference) {
                auto* targetRef = static_cast<RE::TESObjectREFR*>(a_p1);
                if (player && targetRef && targetRef == player) {
                    a_result = 1.0;
                    return true;
                }
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            } else {
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
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
        if (!player) {
            player = RE::PlayerCharacter::GetSingleton();
        }
        auto* donor = mgr.GetDonor();
        auto* puppet = mgr.GetPuppet();
        bool isSelfDialogue = DialogueManager::GetSingleton().IsInSelfDialogue();

        auto* evalThis = a_thisObj;
        auto* evalP1 = a_p1;

        if (donor && evalThis == donor) {
            evalThis = player;
        }
        if (isSelfDialogue && puppet && evalThis == puppet) {
            evalThis = player;
        }

        if constexpr (Cond == SpatialCondition::kGetDistance ||
                      Cond == SpatialCondition::kGetInSameCell ||
                      Cond == SpatialCondition::kGetHeadingAngle) {
            if (donor && evalP1 == donor) {
                evalP1 = player;
            }
            if (isSelfDialogue && puppet && evalP1 == puppet) {
                evalP1 = player;
            }

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

    bool ImpersonationManager::IsModInternalQuest(RE::TESQuest* a_quest) {
        if (!a_quest) return false;

        static RE::TESQuest* s_modQuest = nullptr;
        if (!s_modQuest) {
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                s_modQuest = dataHandler->LookupForm<RE::TESQuest>(0x00000D92, "InTheirShoes.esp"sv);
            }
        }
        return a_quest == s_modQuest;
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
            auto* donorVoice = mgr._donorVoiceType.load(std::memory_order_acquire);
            if (mgr.IsActive() && donorVoice) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && a_this == player->GetActorBase()) {
                    if (mgr.IsNPCVoiceEnabled()) {
                        return donorVoice;
                    }
                    return mgr.GetOrigPlayerVoiceType();
                }

                auto* puppet = mgr.GetPuppet();
                bool isPuppet = (puppet && a_this == puppet->GetActorBase()) || mgr.IsPuppetBase(a_this);
                if (isPuppet) {
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

            if (fRank.rank >= 0) {
                auto fid = fRank.faction->GetFormID();
                if (fid == 0x0005C84E /*CurrentFollowerFaction*/ || fid == 0x000BD738 /*PlayerFollowerFaction*/) {
                    donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
                }
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

        SKSE::log::info("Successfully installed {} script condition hooks via CommonLibSSE-NG.", hookedCount);

        _hooksInstalled = true;
    }

    void ImpersonationManager::Initialize() {
        InstallHooks();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (dataHandler) {
            auto* puppetNPC = dataHandler->LookupForm<RE::TESNPC>(0x00000D93, "InTheirShoes.esp"sv);
            _puppetBase.store(puppetNPC, std::memory_order_release);

            auto* marker = dataHandler->LookupForm<RE::TESObjectREFR>(0x00000807, "InTheirShoes.esp"sv);
            if (marker) {
                _storageMarker = marker->GetHandle();
                SKSE::log::info("Resolved storage marker: {:08X}", marker->GetFormID());
            } else {
                SKSE::log::warn("Failed to resolve storage marker 0x00000807 from InTheirShoes.esp!");
            }
        }
    }

    std::string ImpersonationManager::GetOrigPlayerName() const {
        auto name = FaceManager::GetSingleton().GetSavedAppearanceName();
        if (!name.empty()) return name;

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            const char* dispName = player->GetDisplayFullName();
            if (dispName && dispName[0] != '\0') return dispName;
            if (const char* baseName = player->GetName()) return baseName;
        }
        return {};
    }

    RE::BGSVoiceType* ImpersonationManager::GetOrigPlayerVoiceType() const {
        return FaceManager::GetSingleton().GetOriginalVoiceType();
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
        _puppetConfigured = false;
    }

    std::int32_t ImpersonationManager::Begin(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) {
            SKSE::log::warn("ImpersonationManager::Begin called with null actor!");
            return 0;
        }

        if (IsActive()) {
            End();
        }

        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            AttachDonor(a_donor);
            _active.store(true, std::memory_order_release);

            SKSE::log::info("Beginning impersonation: Donor {:08X} ({}) -> Player {:08X}",
                static_cast<std::uint32_t>(a_donor->GetFormID()),
                a_donor->GetName(),
                static_cast<std::uint32_t>(a_player->GetFormID())
            );

            IdleAIManager::GetSingleton().ClearPlayerPackages();
            SwapDonorAliasesToPlayer(a_donor, a_player);
            DialogueManager::GetSingleton().InvalidateIdleQuestCache();

            if (auto* puppet = GetPuppet()) {
                ApplyPuppetFollowState(puppet);
                puppet->EvaluatePackage(false, false);
            }
        }

        IdleAIManager::GetSingleton().OnImpersonationChanged(true);
        return 1;
    }

    std::int32_t ImpersonationManager::End() {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            if (!IsActive()) {
                return 0;
            }

            auto* donor = GetDonor();
            SKSE::log::info("Ending impersonation of donor {:08X}",
                donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0u);

            ForwardPuppetFactionsToDonor();
            RestoreSwappedAliases(donor);
            DialogueManager::GetSingleton().InvalidateIdleQuestCache();

            IdleAIManager::GetSingleton().ClearPlayerPackages();
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
                pc->SetAIDriven(false);
                pc->EvaluatePackage(false, false);
            }

            _active.store(false, std::memory_order_release);
            DetachDonor();
        }

        IdleAIManager::GetSingleton().OnImpersonationChanged(false);
        return 1;
    }

    void ImpersonationManager::ApplyPuppetFollowState(RE::Actor* a_puppet) {
        if (auto* puppetBase = skyrim_cast<RE::TESNPC*>(a_puppet->GetActorBase())) {
            puppetBase->aiPackages.packages.clear();
            if (_puppetFollows.load(std::memory_order_acquire)) {
                auto* followPkg = RE::TESForm::LookupByID<RE::TESPackage>(0x0005C84B);
                if (!followPkg) {
                    followPkg = RE::TESForm::LookupByID<RE::TESPackage>(0x0004B037);
                }
                if (followPkg) {
                    puppetBase->aiPackages.packages.push_front(followPkg);
                    SKSE::log::info("ApplyPuppetFollowState: Added follow package {:08X} to puppet base {:08X}",
                        followPkg->GetFormID(), puppetBase->GetFormID());
                }
            }
        }

        if (_puppetFollows.load(std::memory_order_acquire)) {
            a_puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
        } else {
            a_puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
        }
    }

    void ImpersonationManager::SetPuppet(RE::Actor* a_puppet) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_puppet) {
            if (auto* oldPuppet = GetPuppet()) {
                if (auto* base = skyrim_cast<RE::TESNPC*>(oldPuppet->GetActorBase())) {
                    base->aiPackages.packages.clear();
                }
                oldPuppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
                oldPuppet->EvaluatePackage(false, false);
            }
            AttachPuppet(nullptr);
            return;
        }

        if (GetPuppet() == a_puppet && _puppetConfigured) {
            return;
        }

        AttachPuppet(a_puppet);
        ConfigurePuppet(a_puppet);
        _puppetConfigured = true;
    }

    void ImpersonationManager::ConfigurePuppet(RE::Actor* a_puppet) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            FaceManager::GetSingleton().SaveBaselineAppearance(player);
        }

        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            FaceManager::GetSingleton().RestoreOriginalPuppetAppearance(a_puppet);
        }

        ApplyPuppetFollowState(a_puppet);

        a_puppet->AllowPCDialogue(true);
        a_puppet->AllowBleedoutDialogue(true);
        a_puppet->StopCombat();
        a_puppet->EvaluatePackage(true, true);

        SKSE::log::info("Configured puppet {:08X} (followsPlayer={}).",
            static_cast<std::uint32_t>(a_puppet->GetFormID()), _puppetFollows.load(std::memory_order_acquire));
    }

    void ImpersonationManager::SetPuppetFollowsPlayer(bool a_follows) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _puppetFollows.store(a_follows, std::memory_order_release);
        if (auto* puppet = GetPuppet()) {
            ApplyPuppetFollowState(puppet);

            if (a_follows && _active.load(std::memory_order_acquire)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    if (puppet->GetParentCell() != player->GetParentCell() || puppet->GetPosition().GetDistance(player->GetPosition()) > 1000.0f) {
                        puppet->MoveTo(player);
                        SKSE::log::info("SetPuppetFollowsPlayer: Teleported puppet to player.");
                    }
                }
            }
            puppet->EvaluatePackage(true, true);
            SKSE::log::info("Updated puppet follows player: {}", a_follows);
        }
    }

    void ImpersonationManager::Tick() {
        IdleAIManager::GetSingleton().Update();

        auto& dialMgr = DialogueManager::GetSingleton();
        if (dialMgr.IsPlayerSpeaking()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = ui && ui->IsMenuOpen("Dialogue Menu"sv);
            dialMgr.TickSpeech(player, menuOpen);
        }

        dialMgr.UpdateIdleChatter();

        auto worldReady = []() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            return cell && cell->IsAttached() && player->Get3D() &&
                   !(ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
        };

        if (_active.load(std::memory_order_acquire)) {
            auto* donor = GetDonor();
            auto markerHandle = _storageMarker;
            auto marker = markerHandle.get();
            if (donor && marker) {
                bool needsLeash = false;
                if (donor->GetParentCell() != marker->GetParentCell()) {
                    needsLeash = true;
                } else if (donor->GetPosition().GetDistance(marker->GetPosition()) > 500.0f) {
                    needsLeash = true;
                }

                static std::atomic<bool> s_leashQueued{ false };
                if (needsLeash && worldReady() && !s_leashQueued.exchange(true)) {
                    SKSE::log::info("Tick: Donor {:08X} moved away from holding marker; leashing back.",
                        donor->GetFormID());
                    auto donorHandle = donor->GetHandle();
                    SKSE::GetTaskInterface()->AddTask([donorHandle, markerHandle]() {
                        s_leashQueued.store(false);
                        auto d = donorHandle.get();
                        auto m = markerHandle.get();
                        if (!d || !m) return;
                        d->MoveTo(m.get());
                        if (IdleAIManager::GetSingleton().IsAIActive()) {
                            IdleAIManager::GetSingleton().SyncDonorUpdatesToPlayer();
                        }
                    });
                }
            }
        }
    }

    void ImpersonationManager::SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) return;
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _swappedAliases.clear();

        auto* aliasArray = a_donor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (!aliasArray) return;

        std::vector<std::pair<RE::TESQuest*, std::uint32_t>> toSwap;
        {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (!inst || !inst->quest || !inst->alias) continue;
                if (!inst->quest->IsRunning()) continue;

                if (IsModInternalQuest(inst->quest)) continue;

                if (inst->alias->QType() != "Ref"sv) continue;
                auto* refAlias = static_cast<const RE::BGSRefAlias*>(inst->alias);

                if (refAlias->GetReference() == a_donor) {
                    toSwap.emplace_back(inst->quest, refAlias->aliasID);
                }
            }
        }

        for (const auto& [quest, aid] : toSwap) {
            quest->ForceRefIntoAlias(aid, a_player);
            _swappedAliases.push_back({ quest->GetFormID(), aid });
            SKSE::log::info("SwapDonorAliasesToPlayer: Swapped donor {:08X} to player {:08X} in quest {:08X} alias ID {}",
                a_donor->GetFormID(), a_player->GetFormID(), quest->GetFormID(), aid);
        }
    }

    void ImpersonationManager::RestoreSwappedAliases(RE::Actor* a_donor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_swappedAliases.empty()) return;

        auto* donor = a_donor ? a_donor : GetDonor();
        auto* player = GetPlayer();

        for (const auto& entry : _swappedAliases) {
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(entry.questFormID);
            if (!quest || !quest->IsRunning()) {
                SKSE::log::info("RestoreSwappedAliases: Quest {:08X} is stopped or missing, skipping alias ID {}",
                    entry.questFormID, entry.aliasID);
                continue;
            }

            auto* targetDonor = donor;
            if (!targetDonor) {
                SKSE::log::warn("RestoreSwappedAliases: No donor to restore into quest {:08X} alias ID {}",
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
                auto* refAlias = static_cast<RE::BGSRefAlias*>(baseAlias);
                auto* currentRef = refAlias->GetReference();
                if (player && currentRef && currentRef != player && currentRef != targetDonor) {
                    SKSE::log::info("RestoreSwappedAliases: Alias ID {} in quest {:08X} no longer holds player (holds {:08X}), skipping restoration.",
                        entry.aliasID, entry.questFormID, currentRef->GetFormID());
                    continue;
                }
            }

            quest->ForceRefIntoAlias(entry.aliasID, targetDonor);
            SKSE::log::info("RestoreSwappedAliases: Restored donor {:08X} into quest {:08X} alias ID {}",
                targetDonor->GetFormID(), quest->GetFormID(), entry.aliasID);
        }
        _swappedAliases.clear();
    }

    void ImpersonationManager::Save(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        const bool active = IsActive();
        const RE::FormID donorID = (active && GetDonor()) ? GetDonor()->GetFormID() : 0;
        const RE::FormID puppetID = GetPuppet() ? GetPuppet()->GetFormID() : 0;

        if (!active && puppetID == 0) {
            return;
        }

        if (a_serde->OpenRecord(Serialization::kImpersonationDataRecord, Serialization::kSerializationVersion)) {
            a_serde->WriteRecordData(&active, sizeof(active));
            a_serde->WriteRecordData(&donorID, sizeof(donorID));
            a_serde->WriteRecordData(&puppetID, sizeof(puppetID));

            const std::uint32_t count = static_cast<std::uint32_t>(_swappedAliases.size());
            a_serde->WriteRecordData(&count, sizeof(count));
            for (const auto& entry : _swappedAliases) {
                a_serde->WriteRecordData(&entry.questFormID, sizeof(entry.questFormID));
                a_serde->WriteRecordData(&entry.aliasID, sizeof(entry.aliasID));
            }
            SKSE::log::info("Saved impersonation session (active={}, {} swapped aliases).", active, count);
        }
    }

    void ImpersonationManager::Load(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        bool active = false;
        RE::FormID donorID = 0;
        RE::FormID puppetID = 0;
        std::uint32_t aliasCount = 0;

        if (!a_serde->ReadRecordData(&active, sizeof(active)) ||
            !a_serde->ReadRecordData(&donorID, sizeof(donorID)) ||
            !a_serde->ReadRecordData(&puppetID, sizeof(puppetID)) ||
            !a_serde->ReadRecordData(&aliasCount, sizeof(aliasCount))) {
            SKSE::log::warn("Impersonation record is truncated; ignoring it.");
            return;
        }

        _swappedAliases.clear();
        for (std::uint32_t i = 0; i < aliasCount; ++i) {
            RE::FormID questID = 0;
            std::uint32_t aliasID = 0;
            if (!a_serde->ReadRecordData(&questID, sizeof(questID)) ||
                !a_serde->ReadRecordData(&aliasID, sizeof(aliasID))) {
                break;
            }
            if (a_serde->ResolveFormID(questID, questID)) {
                _swappedAliases.push_back({ questID, aliasID });
            }
        }

        if (puppetID != 0 && a_serde->ResolveFormID(puppetID, puppetID)) {
            if (auto* puppet = RE::TESForm::LookupByID<RE::Actor>(puppetID)) {
                AttachPuppet(puppet);
                SKSE::log::info("Restored puppet {:08X} from co-save.", static_cast<std::uint32_t>(puppetID));
            }
        }

        if (active && donorID != 0 && a_serde->ResolveFormID(donorID, donorID)) {
            auto* donor = RE::TESForm::LookupByID<RE::Actor>(donorID);
            if (donor && RE::PlayerCharacter::GetSingleton()) {
                AttachDonor(donor);
                _active.store(true, std::memory_order_release);
                SKSE::log::info("Restored impersonation state from co-save: Donor {:08X} ({} swapped aliases)",
                    static_cast<std::uint32_t>(donorID), _swappedAliases.size());
            } else {
                _swappedAliases.clear();
            }
        }
    }

    void ImpersonationManager::OnRevert() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        IdleAIManager::GetSingleton().ClearPlayerPackages();
        DialogueManager::GetSingleton().InvalidateIdleQuestCache();
        _swappedAliases.clear();
        _active.store(false, std::memory_order_release);
        DetachDonor();
        SetPuppet(nullptr);
    }

    void ImpersonationManager::OnPostLoadGame() {
        bool wasActive = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            wasActive = IsActive();
            if (wasActive) {
                auto* donor = GetDonor();
                auto* player = GetPlayer();

                if (donor && player) {
                    SKSE::log::info("OnPostLoadGame: Queuing donor appearance restoration (donor: {:08X}, player: {:08X})",
                        static_cast<std::uint32_t>(donor->GetFormID()),
                        static_cast<std::uint32_t>(player->GetFormID()));

                    if (auto* task = SKSE::GetTaskInterface()) {
                        auto donorHandle = donor->GetHandle();
                        auto playerHandle = player->GetHandle();
                        task->AddTask([donorHandle, playerHandle]() {
                            auto d = donorHandle.get();
                            auto p = playerHandle.get();
                            if (d && p) {
                                FaceManager::GetSingleton().RestoreDonorAppearance(d.get(), p.get());
                            }
                        });
                    }
                }

                if (auto* puppet = GetPuppet()) {
                    ConfigurePuppet(puppet);
                    _puppetConfigured = true;
                }

                if (donor && player) {
                    if (_swappedAliases.empty()) {
                        auto* playerAliasArray = player->extraList.GetByType<RE::ExtraAliasInstanceArray>();
                        if (playerAliasArray) {
                            RE::BSReadLockGuard locker(playerAliasArray->lock);
                            for (auto* inst : playerAliasArray->aliases) {
                                if (!inst || !inst->quest || !inst->alias) continue;
                                if (!inst->quest->IsRunning() || IsModInternalQuest(inst->quest)) continue;
                                if (inst->alias->QType() != "Ref"sv) continue;
                                auto* refAlias = static_cast<const RE::BGSRefAlias*>(inst->alias);
                                if (refAlias->GetReference() == player) {
                                    if (refAlias->fillType.get() == RE::BGSBaseAlias::FILL_TYPE::kUniqueActor &&
                                        refAlias->fillData.uniqueActor.uniqueActor == donor->GetActorBase()) {
                                        _swappedAliases.push_back({ inst->quest->GetFormID(), refAlias->aliasID });
                                        SKSE::log::info("OnPostLoadGame: Reconstructed swapped alias ID {} in quest {:08X} for donor {:08X}",
                                            refAlias->aliasID, inst->quest->GetFormID(), donor->GetFormID());
                                    }
                                }
                            }
                        }
                    }
                    SKSE::log::info("OnPostLoadGame: Verified {} active swapped aliases for donor {:08X}",
                        _swappedAliases.size(), donor->GetFormID());
                }
            }
        }

        if (wasActive) {
            IdleAIManager::GetSingleton().OnImpersonationChanged(true);
        }
    }

}
