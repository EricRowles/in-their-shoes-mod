#include "PCH.h"
#include "ConditionHooks.h"
#include "AliasSwapManager.h"
#include "Forms.h"
#include "Session.h"

#include <RE/C/CommandTable.h>
#include <RE/T/TESCondition.h>

namespace InTheirShoes::ConditionHooks {

    namespace {
        using Condition_t = RE::SCRIPT_FUNCTION::Condition_t;

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

        std::array<Condition_t*, static_cast<std::size_t>(IdentityCondition::kTotal)> s_originalConditions{ nullptr };

        // --- Category A: Identity Thunk ---
        template <IdentityCondition Cond>
        bool IdentityThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto& session = Session::GetSingleton();
            constexpr auto idx = static_cast<std::size_t>(Cond);
            auto* orig = s_originalConditions[idx];
            if (!orig) return false;
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }

            auto* player = session.GetPlayer();
            auto* donor = session.GetDonor();
            auto* puppet = session.GetPuppet();
            bool isSelfDialogue = session.IsInSelfDialogue();

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
                    if ((isSelfDialogue || session.IsNPCVoiceEnabled()) && session.GetDonorVoiceType()) {
                        a_result = (targetVoice && targetVoice == session.GetDonorVoiceType()) ? 1.0 : 0.0;
                        return true;
                    }
                    auto* origVoice = session.GetOrigPlayerVoiceType();
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
                    if (auto rank = AliasSwapManager::GetSingleton().GetDonorAliasFactionRank(static_cast<RE::TESFaction*>(a_p1))) {
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
                    if (auto rank = AliasSwapManager::GetSingleton().GetDonorAliasFactionRank(static_cast<RE::TESFaction*>(a_p1))) {
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
                    auto* origVoice = session.GetOrigPlayerVoiceType();
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

        std::array<Condition_t*, static_cast<std::size_t>(SpatialCondition::kTotal)> s_originalSpatialConditions{ nullptr };

        template <SpatialCondition Cond>
        bool SpatialConditionThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto& session = Session::GetSingleton();
            constexpr auto idx = static_cast<std::size_t>(Cond);
            auto* orig = s_originalSpatialConditions[idx];
            if (!orig) return false;
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }

            auto* player = session.GetPlayer();
            auto* evalThis = session.ResolveSpatialRef(a_thisObj);
            void* evalP1 = a_p1;

            if constexpr (Cond == SpatialCondition::kGetDistance ||
                          Cond == SpatialCondition::kGetInSameCell ||
                          Cond == SpatialCondition::kGetHeadingAngle) {
                evalP1 = session.ResolveSpatialRef(static_cast<RE::TESObjectREFR*>(a_p1));

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

        // Swaps in a_thunk as the named condition's function, keeping the original in a_originals
        template <class Cond, std::size_t N>
        bool HookCondition(std::array<Condition_t*, N>& a_originals, Cond a_cond, std::string_view a_name, Condition_t* a_thunk) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(a_name);
            if (!func) {
                SKSE::log::error("Failed to locate script command '{}'!", a_name);
                return false;
            }
            auto idx = static_cast<std::size_t>(a_cond);
            a_originals[idx] = func->conditionFunction;
            REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(a_thunk));
            SKSE::log::info("Hooked condition '{}' via LocateScriptCommand (orig: {:016X})",
                a_name, reinterpret_cast<std::uintptr_t>(a_originals[idx]));
            return true;
        }

        template <IdentityCondition Cond>
        bool HookIdentity(std::string_view a_name) {
            return HookCondition(s_originalConditions, Cond, a_name, &IdentityThunk<Cond>);
        }

        template <SpatialCondition Cond>
        bool HookSpatial(std::string_view a_name) {
            return HookCondition(s_originalSpatialConditions, Cond, a_name, &SpatialConditionThunk<Cond>);
        }
    }

    void Install() {
        std::size_t hookedCount = 0;
        if (HookIdentity<IdentityCondition::kGetInFaction>("GetInFaction"sv)) hookedCount++;
        if (HookIdentity<IdentityCondition::kGetIsID>("GetIsID"sv)) hookedCount++;
        if (HookIdentity<IdentityCondition::kGetFactionRank>("GetFactionRank"sv)) hookedCount++;
        if (HookIdentity<IdentityCondition::kGetIsReference>("GetIsReference"sv)) hookedCount++;
        if (HookIdentity<IdentityCondition::kGetIsAliasRef>("GetIsAliasRef"sv)) hookedCount++;
        if (HookIdentity<IdentityCondition::kGetIsVoiceType>("GetIsVoiceType"sv)) hookedCount++;
        if (HookIdentity<IdentityCondition::kIsInList>("IsInList"sv)) hookedCount++;

        if (HookSpatial<SpatialCondition::kGetInCell>("GetInCell"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetInCurrentLoc>("GetInCurrentLoc"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetDistance>("GetDistance"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kIsInInterior>("IsInInterior"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetInSameCell>("GetInSameCell"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetInWorldspace>("GetInWorldspace"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetPos>("GetPos"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetHeadingAngle>("GetHeadingAngle"sv)) hookedCount++;
        if (HookSpatial<SpatialCondition::kGetAngle>("GetAngle"sv)) hookedCount++;

        constexpr auto kExpectedHooks = static_cast<std::size_t>(IdentityCondition::kTotal) + static_cast<std::size_t>(SpatialCondition::kTotal);
        if (hookedCount == kExpectedHooks) {
            SKSE::log::info("Installed all {} script condition hooks.", hookedCount);
        } else {
            // A missing hook leaves the donor's conditions answered for the wrong actor; the errors above name it
            SKSE::log::error("Installed only {} of {} script condition hooks; impersonation conditions will be wrong.",
                hookedCount, kExpectedHooks);
        }
    }

}
