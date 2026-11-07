#include "PCH.h"
#include "ConditionHooks.h"
#include "FactionManager.h"
#include "Forms.h"
#include "Session.h"

#include <optional>
#include <utility>

#include <RE/C/CommandTable.h>
#include <RE/T/TESCondition.h>

namespace InTheirShoes::ConditionHooks {

    namespace {
        using Condition_t = RE::SCRIPT_FUNCTION::Condition_t;

        // One evaluation of a hooked condition, with what the answers need
        struct Query {
            Condition_t* orig;
            RE::TESObjectREFR* subject;
            void* param1;
            void* param2;
            const Session& session;
            RE::Actor* player;
            RE::Actor* donor;

            // The original condition, asked of a_ref instead of the subject
            bool Ask(RE::TESObjectREFR* a_ref, double& a_result) const { return orig(a_ref, param1, param2, a_result); }
            // Whether the original condition is true for a_ref
            bool IsTrueFor(RE::TESObjectREFR* a_ref) const {
                double result = 0.0;
                return a_ref && Ask(a_ref, result) && result != 0.0;
            }
        };

        using Answer_t = bool(const Query&, double&);

        // --- Identity: the subject answers as the donor ---

        bool IsIDAsDonor(const Query& a_q, double& a_result) {
            auto* donor = a_q.donor;
            auto* targetForm = static_cast<RE::TESForm*>(a_q.param1);
            if (donor && targetForm) {
                auto* actorBase = donor->GetActorBase();
                auto* leveled = donor->extraList.GetByType<RE::ExtraLeveledCreature>();
                if (targetForm == donor || targetForm == donor->GetBaseObject() ||
                    (actorBase && (targetForm == actorBase || targetForm == actorBase->GetRootFaceNPC())) ||
                    (leveled && (targetForm == leveled->originalBase || targetForm == leveled->templateBase)) ||
                    a_q.IsTrueFor(donor)) {
                    a_result = 1.0;
                    return true;
                }
            }
            // Fall back to the player's own identity for player perks, spells, and magic effects
            if (a_q.subject == a_q.player) {
                return a_q.Ask(a_q.player, a_result);
            }
            a_result = 0.0;
            return true;
        }

        bool IsVoiceTypeAsDonor(const Query& a_q, double& a_result) {
            auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_q.param1);
            auto* donorVoice = a_q.session.GetDonorVoiceType();
            auto* voice = ((a_q.session.IsInSelfDialogue() || a_q.session.IsNPCVoiceEnabled()) && donorVoice) ?
                              donorVoice :
                              a_q.session.GetOrigPlayerVoiceType();
            a_result = (voice && targetVoice == voice) ? 1.0 : 0.0;
            return true;
        }

        bool IsReferenceAsDonor(const Query& a_q, double& a_result) {
            if (a_q.donor && static_cast<RE::TESObjectREFR*>(a_q.param1) == a_q.donor) {
                a_result = 1.0;
                return true;
            }
            return a_q.Ask(a_q.subject, a_result);
        }

        // The player holds the donor's swapped aliases; the donor still holds any that weren't swapped
        bool IsAliasRefAsDonor(const Query& a_q, double& a_result) {
            double result = 0.0;
            if ((a_q.Ask(a_q.player, result) && result != 0.0) ||
                (a_q.donor && a_q.Ask(a_q.donor, result) && result != 0.0)) {
                a_result = result;
                return true;
            }
            a_result = 0.0;
            return true;
        }

        bool IsInListAsDonor(const Query& a_q, double& a_result) {
            double result = 0.0;
            if (a_q.donor && a_q.Ask(a_q.donor, result) && result != 0.0) {
                a_result = result;
                return true;
            }
            return a_q.Ask(a_q.player, a_result);
        }

        // Conditions only the donor's record can answer (its class, its crime faction)
        bool AskDonor(const Query& a_q, double& a_result) {
            return a_q.Ask(a_q.donor ? a_q.donor : a_q.player, a_result);
        }

        // The player only joins the donor's scenes through its swapped aliases, which doesn't put the Dragonborn in
        // them. A banter quest's "IsInScene == 0 on the player" dialogue condition failed all of its lines, so a
        // scene's actions finished at once with nothing said (#1). The condition can't tell which aliases put the
        // player in the scene, so scenes it's really in answer no as well.
        bool IsInSceneAsDonor(const Query& a_q, double& a_result) {
            a_result = a_q.IsTrueFor(a_q.donor) ? 1.0 : 0.0;
            return true;
        }

        // --- Identity: the subject answers as the Dragonborn ---

        bool AskPlayer(const Query& a_q, double& a_result) {
            return a_q.Ask(a_q.player, a_result);
        }

        bool AskSubject(const Query& a_q, double& a_result) {
            return a_q.Ask(a_q.subject, a_result);
        }

        bool IsIDAsDragonborn(const Query& a_q, double& a_result) {
            auto* targetForm = static_cast<RE::TESForm*>(a_q.param1);
            if (targetForm && (targetForm == a_q.player || targetForm == a_q.player->GetActorBase() ||
                                  targetForm->GetFormID() == Forms::kPlayerBaseID || targetForm->GetFormID() == Forms::kPlayerRefID)) {
                a_result = 1.0;
                return true;
            }
            return AskPlayer(a_q, a_result);
        }

        // The player is in the donor's factions for the session (FactionManager), so the Dragonborn's are
        // answered from what they were when it began
        bool InFactionAsDragonborn(const Query& a_q, double& a_result) {
            if (auto rank = FactionManager::GetSingleton().GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_q.param1))) {
                a_result = *rank >= 0 ? 1.0 : 0.0;
                return true;
            }
            return AskPlayer(a_q, a_result);
        }

        bool FactionRankAsDragonborn(const Query& a_q, double& a_result) {
            if (auto rank = FactionManager::GetSingleton().GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_q.param1))) {
                a_result = *rank;
                return true;
            }
            return AskPlayer(a_q, a_result);
        }

        bool IsVoiceTypeAsDragonborn(const Query& a_q, double& a_result) {
            auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_q.param1);
            auto* origVoice = a_q.session.GetOrigPlayerVoiceType();
            a_result = (origVoice && targetVoice == origVoice) ? 1.0 : 0.0;
            return true;
        }

        bool IsReferenceAsDragonborn(const Query& a_q, double& a_result) {
            if (static_cast<RE::TESObjectREFR*>(a_q.param1) == a_q.player) {
                a_result = 1.0;
                return true;
            }
            return AskPlayer(a_q, a_result);
        }

        // The conditions that ask who an actor is. While impersonating, the player tests as the donor and the
        // puppet as the Dragonborn; in self-dialogue, where the puppet speaks for the donor, it's reversed.
        struct IdentityCondition {
            std::string_view name;
            Answer_t* asDonor;
            Answer_t* asDragonborn;
        };

        constexpr IdentityCondition kIdentityConditions[] = {
            // The player is really in the donor's factions and its aliases' (FactionManager)
            { "GetInFaction"sv, &AskPlayer, &InFactionAsDragonborn },
            { "GetIsID"sv, &IsIDAsDonor, &IsIDAsDragonborn },
            { "GetFactionRank"sv, &AskPlayer, &FactionRankAsDragonborn },
            { "GetIsReference"sv, &IsReferenceAsDonor, &IsReferenceAsDragonborn },
            { "GetIsAliasRef"sv, &IsAliasRefAsDonor, &AskPlayer },
            { "GetIsVoiceType"sv, &IsVoiceTypeAsDonor, &IsVoiceTypeAsDragonborn },
            { "IsInList"sv, &IsInListAsDonor, &AskPlayer },
            { "GetIsClass"sv, &AskDonor, &AskPlayer },
            { "GetIsCrimeFaction"sv, &AskDonor, &AskPlayer },
            { "IsInScene"sv, &IsInSceneAsDonor, &AskSubject },
        };
        constexpr std::size_t kIdentityCount = std::size(kIdentityConditions);
        std::array<Condition_t*, kIdentityCount> s_originalIdentity{};

        template <std::size_t I>
        bool IdentityThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto* orig = s_originalIdentity[I];
            if (!orig) return false;
            auto& session = Session::GetSingleton();
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }

            const Query query{ orig, a_thisObj, a_p1, a_p2, session, session.GetPlayer(), session.GetDonor() };
            auto* puppet = session.GetPuppet();
            const bool isPlayer = a_thisObj == query.player;
            const bool isPuppet = puppet && a_thisObj == puppet;
            const bool selfDialogue = session.IsInSelfDialogue();

            if (selfDialogue ? isPuppet : isPlayer) {
                return kIdentityConditions[I].asDonor(query, a_result);
            }
            if (selfDialogue ? isPlayer : isPuppet) {
                return kIdentityConditions[I].asDragonborn(query, a_result);
            }
            return orig(a_thisObj, a_p1, a_p2, a_result);
        }

        // --- Spatial: the parked donor is where the player is (Session::ResolveSpatialRef) ---

        struct SpatialCondition {
            std::string_view name;
            // The parameter is a reference, resolved like the subject. When both resolve to the player, the
            // answer is this: per requirements.md ("Conditions that require the player to be nearby will still
            // target the impersonator"), checks between the donor and the player pass.
            std::optional<double> resultWhenBothPlayer;
        };

        constexpr SpatialCondition kSpatialConditions[] = {
            { "GetInCell"sv, std::nullopt },
            { "GetInCurrentLoc"sv, std::nullopt },
            { "GetDistance"sv, 0.0 },
            { "IsInInterior"sv, std::nullopt },
            { "GetInSameCell"sv, 1.0 },
            { "GetInWorldspace"sv, std::nullopt },
            { "GetPos"sv, std::nullopt },
            { "GetHeadingAngle"sv, 0.0 },
            { "GetAngle"sv, std::nullopt },
        };
        constexpr std::size_t kSpatialCount = std::size(kSpatialConditions);
        std::array<Condition_t*, kSpatialCount> s_originalSpatial{};

        template <std::size_t I>
        bool SpatialThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto* orig = s_originalSpatial[I];
            if (!orig) return false;
            auto& session = Session::GetSingleton();
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }

            auto* evalThis = session.ResolveSpatialRef(a_thisObj);
            void* evalP1 = a_p1;
            if constexpr (constexpr auto kBothPlayer = kSpatialConditions[I].resultWhenBothPlayer; kBothPlayer.has_value()) {
                auto* evalRef = session.ResolveSpatialRef(static_cast<RE::TESObjectREFR*>(a_p1));
                if (evalThis == session.GetPlayer() && evalRef == session.GetPlayer()) {
                    a_result = *kBothPlayer;
                    return true;
                }
                evalP1 = evalRef;
            }
            return orig(evalThis, evalP1, a_p2, a_result);
        }

        // --- Relationships: who an actor is to another ---
        // Relationships are between base records, so the player's are the donor's: the donor's spouse, friends
        // and enemies treat the player as the donor. The puppet keeps the Dragonborn's (marriage, followers).
        // In self-dialogue the puppet is the donor and the player the Dragonborn.

        constexpr std::string_view kRelationshipConditions[] = {
            "GetRelationshipRank"sv,
            "HasFamilyRelationship"sv,
            "HasParentRelationship"sv,
        };
        constexpr std::size_t kRelationshipCount = std::size(kRelationshipConditions);
        std::array<Condition_t*, kRelationshipCount> s_originalRelationship{};

        RE::TESObjectREFR* ResolveRelationshipRef(const Session& a_session, RE::TESObjectREFR* a_ref) {
            auto* player = a_session.GetPlayer();
            auto* puppet = a_session.GetPuppet();
            auto* donor = a_session.GetDonor();
            if (!a_ref || !donor) return a_ref;
            if (a_session.IsInSelfDialogue()) {
                return a_ref == puppet ? donor : a_ref;
            }
            if (a_ref == player) return donor;
            if (puppet && a_ref == puppet) return player;
            return a_ref;
        }

        template <std::size_t I>
        bool RelationshipThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto* orig = s_originalRelationship[I];
            if (!orig) return false;
            auto& session = Session::GetSingleton();
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }
            return orig(ResolveRelationshipRef(session, a_thisObj),
                ResolveRelationshipRef(session, static_cast<RE::TESObjectREFR*>(a_p1)), a_p2, a_result);
        }

        // --- The PC: outside self-dialogue the PC is the donor. GetPCIsRace and GetPCIsSex follow the player's
        // record, which has the donor's race and sex, but the class stays the Dragonborn's. GetPCInFaction follows
        // the player's real factions, which are the donor's; in self-dialogue the PC is the Dragonborn. ---

        Condition_t* s_originalPCIsClass{ nullptr };

        bool PCIsClassThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            if (!s_originalPCIsClass) return false;
            auto& session = Session::GetSingleton();
            auto* donor = session.GetDonor();
            auto* donorBase = donor ? donor->GetActorBase() : nullptr;
            if (!session.IsActive() || session.IsInSelfDialogue() || !donorBase) {
                return s_originalPCIsClass(a_thisObj, a_p1, a_p2, a_result);
            }
            a_result = donorBase->npcClass && donorBase->npcClass == static_cast<RE::TESClass*>(a_p1) ? 1.0 : 0.0;
            return true;
        }

        Condition_t* s_originalPCInFaction{ nullptr };

        bool PCInFactionThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            if (!s_originalPCInFaction) return false;
            auto& session = Session::GetSingleton();
            if (session.IsActive() && session.IsInSelfDialogue()) {
                if (auto rank = FactionManager::GetSingleton().GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_p1))) {
                    a_result = *rank >= 0 ? 1.0 : 0.0;
                    return true;
                }
            }
            return s_originalPCInFaction(a_thisObj, a_p1, a_p2, a_result);
        }

        // Swaps in a_thunk as the named condition's function, keeping the original in a_original
        bool HookCondition(Condition_t*& a_original, std::string_view a_name, Condition_t* a_thunk) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(a_name);
            if (!func) {
                SKSE::log::error("Failed to locate script command '{}'!", a_name);
                return false;
            }
            a_original = func->conditionFunction;
            REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(a_thunk));
            SKSE::log::info("Hooked condition '{}' via LocateScriptCommand (orig: {:016X})",
                a_name, reinterpret_cast<std::uintptr_t>(a_original));
            return true;
        }

        template <std::size_t... I>
        std::size_t HookIdentityConditions(std::index_sequence<I...>) {
            std::size_t hooked = 0;
            ((hooked += HookCondition(s_originalIdentity[I], kIdentityConditions[I].name, &IdentityThunk<I>)), ...);
            return hooked;
        }

        template <std::size_t... I>
        std::size_t HookRelationshipConditions(std::index_sequence<I...>) {
            std::size_t hooked = 0;
            ((hooked += HookCondition(s_originalRelationship[I], kRelationshipConditions[I], &RelationshipThunk<I>)), ...);
            return hooked;
        }

        template <std::size_t... I>
        std::size_t HookSpatialConditions(std::index_sequence<I...>) {
            std::size_t hooked = 0;
            ((hooked += HookCondition(s_originalSpatial[I], kSpatialConditions[I].name, &SpatialThunk<I>)), ...);
            return hooked;
        }
    }

    void Install() {
        const std::size_t hookedCount = HookIdentityConditions(std::make_index_sequence<kIdentityCount>{}) +
                                        HookSpatialConditions(std::make_index_sequence<kSpatialCount>{}) +
                                        HookRelationshipConditions(std::make_index_sequence<kRelationshipCount>{}) +
                                        HookCondition(s_originalPCIsClass, "GetPCIsClass"sv, &PCIsClassThunk) +
                                        HookCondition(s_originalPCInFaction, "GetPCInFaction"sv, &PCInFactionThunk);

        constexpr auto kExpectedHooks = kIdentityCount + kSpatialCount + kRelationshipCount + 2;
        if (hookedCount == kExpectedHooks) {
            SKSE::log::info("Installed all {} script condition hooks.", hookedCount);
        } else {
            // A missing hook leaves the donor's conditions answered for the wrong actor; the errors above name it
            SKSE::log::error("Installed only {} of {} script condition hooks; impersonation conditions will be wrong.",
                hookedCount, kExpectedHooks);
        }
    }

}
