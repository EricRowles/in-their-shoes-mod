#include "PCH.h"
#include "ConditionHooks.h"
#include "FactionManager.h"
#include "Forms.h"
#include "HookUtil.h"
#include "IdleAIManager.h"
#include "Session.h"
#include "Settings.h"

#include <utility>

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
            RE::Actor* puppet;

            // The original condition, asked of a_ref instead of the subject
            bool Ask(RE::TESObjectREFR* a_ref, double& a_result) const { return orig(a_ref, param1, param2, a_result); }
            // Whether the original condition is true for a_ref
            bool IsTrueFor(RE::TESObjectREFR* a_ref) const {
                double result = 0.0;
                return a_ref && Ask(a_ref, result) && result != 0.0;
            }
        };

        using Answer_t = bool(const Query&, double&);

        // Whether a_form is the Dragonborn: the player's reference or base
        bool IsDragonbornID(const Query& a_q, const RE::TESForm* a_form) {
            return a_form && (a_form == a_q.player || a_form == a_q.player->GetActorBase() || Forms::IsPlayerBase(a_form) ||
                                 a_form->GetFormID() == Forms::kPlayerRefID);
        }

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
            // Fall back to the player's own identity for player perks, spells, and magic effects. Not while idle AI
            // drives the player: the donor's packages exclude the player (DefaultMasterPackage has GetIsID Player == 0).
            if (a_q.subject == a_q.player) {
                if (IdleAIManager::GetSingleton()->IsDrivingPlayer() && IsDragonbornID(a_q, targetForm)) {
                    a_result = 0.0;
                    return true;
                }
                return a_q.Ask(a_q.player, a_result);
            }
            a_result = 0.0;
            return true;
        }

        bool IsVoiceTypeAsDonor(const Query& a_q, double& a_result) {
            auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_q.param1);
            auto* donorVoice = a_q.session.GetDonorVoiceType();
            auto* voice = ((a_q.session.IsInSelfDialogue() || Settings::GetSingleton()->IsNPCVoiceEnabled()) && donorVoice) ?
                              donorVoice :
                              a_q.session.GetOrigPlayerVoiceType();
            a_result = (voice && targetVoice == voice) ? 1.0 : 0.0;
            return true;
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

        // IsInScene on the player answers for the donor, so banter lines requiring "IsInScene == 0" don't fail (#1).
        bool IsInSceneAsDonor(const Query& a_q, double& a_result) {
            a_result = a_q.IsTrueFor(a_q.donor) ? 1.0 : 0.0;
            return true;
        }

        // --- Identity: the subject answers as the Dragonborn ---

        bool AskPlayer(const Query& a_q, double& a_result) {
            return a_q.Ask(a_q.player, a_result);
        }

        // The puppet's body has the Dragonborn's race and sex, as the player's has the donor's. In self-dialogue
        // the bodies are the wrong way round, so race and sex are asked of the other body.
        bool AskPuppet(const Query& a_q, double& a_result) {
            return a_q.Ask(a_q.puppet ? a_q.puppet : a_q.subject, a_result);
        }

        bool IsIDAsDragonborn(const Query& a_q, double& a_result) {
            if (IsDragonbornID(a_q, static_cast<RE::TESForm*>(a_q.param1))) {
                a_result = 1.0;
                return true;
            }
            return AskPlayer(a_q, a_result);
        }

        // The player is in the donor's factions for the session (FactionManager), so the Dragonborn's are
        // answered from what they were when it began
        bool InFactionAsDragonborn(const Query& a_q, double& a_result) {
            if (auto rank = FactionManager::GetSingleton()->GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_q.param1))) {
                a_result = *rank >= 0 ? 1.0 : 0.0;
                return true;
            }
            return AskPlayer(a_q, a_result);
        }

        bool FactionRankAsDragonborn(const Query& a_q, double& a_result) {
            if (auto rank = FactionManager::GetSingleton()->GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_q.param1))) {
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

        // --- Bleedout: the player, as the donor, is down at no health ---

        // The player only enters the bleedout life state when essential, so while it plays the donor it counts as
        // bleeding out at no health too (e.g. a mod that makes the player kneel instead of die). Not when dead.
        // Conditions asked of the donor come here as the player (the condition evaluation hook).
        bool IsBleedingOutAsDonor(const Query& a_q, double& a_result) {
            if (a_q.subject != a_q.player) return a_q.Ask(a_q.subject, a_result);
            const auto state = a_q.player->AsActorState()->GetLifeState();
            using enum RE::ACTOR_LIFE_STATE;
            const bool alive = state != kDead && state != kDying;
            const bool down = state == kBleedout || state == kEssentialDown ||
                              a_q.player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f;
            a_result = alive && down ? 1.0 : 0.0;
            return true;
        }

        // --- Relationships: the Dragonborn's apply first, then the donor's. The puppet keeps the Dragonborn's;
        // in self-dialogue the puppet is the donor and the player the Dragonborn. ---

        RE::TESObjectREFR* ResolveRelationshipRef(const Query& a_q, RE::TESObjectREFR* a_ref) {
            if (!a_ref || !a_q.donor) return a_ref;
            if (a_q.session.IsInSelfDialogue()) {
                return a_ref == a_q.puppet ? a_q.donor : a_ref;
            }
            if (a_ref == a_q.player) return a_q.donor;
            if (a_q.puppet && a_ref == a_q.puppet) return a_q.player;
            return a_ref;
        }

        bool Relationship(const Query& a_q, double& a_result) {
            auto* ref = static_cast<RE::TESObjectREFR*>(a_q.param1);
            auto* evalThis = ResolveRelationshipRef(a_q, a_q.subject);
            auto* evalRef = ResolveRelationshipRef(a_q, ref);
            // The Dragonborn's own relationship first: the player's side asked as the player rather than the donor.
            // A rank of 0 (acquaintance) or false means the Dragonborn has none, so the donor's answers.
            const bool thisIsDonor = a_q.subject == a_q.player && evalThis != a_q.player;
            const bool refIsDonor = ref == a_q.player && evalRef != a_q.player;
            if (thisIsDonor != refIsDonor) {
                auto* ownThis = thisIsDonor ? a_q.player : evalThis;
                auto* ownRef = refIsDonor ? a_q.player : evalRef;
                if (ownThis != ownRef && a_q.orig(ownThis, ownRef, a_q.param2, a_result) && a_result != 0.0) {
                    return true;
                }
            }
            return a_q.orig(evalThis, evalRef, a_q.param2, a_result);
        }

        // --- The PC: the donor's race, sex and factions, the Dragonborn's class; the Dragonborn in self-dialogue ---

        bool PCIsRace(const Query& a_q, double& a_result);
        bool PCIsSex(const Query& a_q, double& a_result);

        bool PCIsClass(const Query& a_q, double& a_result) {
            auto* donorBase = a_q.donor ? a_q.donor->GetActorBase() : nullptr;
            if (a_q.session.IsInSelfDialogue() || !donorBase) return a_q.Ask(a_q.subject, a_result);
            a_result = donorBase->npcClass && donorBase->npcClass == static_cast<RE::TESClass*>(a_q.param1) ? 1.0 : 0.0;
            return true;
        }

        bool PCInFaction(const Query& a_q, double& a_result) {
            if (a_q.session.IsInSelfDialogue()) {
                if (auto rank = FactionManager::GetSingleton()->GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_q.param1))) {
                    a_result = *rank >= 0 ? 1.0 : 0.0;
                    return true;
                }
            }
            return a_q.Ask(a_q.subject, a_result);
        }

        // --- The hooked conditions ---

        // How each hooked condition answers while impersonating; null asks the original. The donor's stand-in is the
        // player, or the puppet in self-dialogue; the Dragonborn's is the puppet, or the player in self-dialogue.
        struct HookedCondition {
            std::string_view name;
            Answer_t* asDonor;       // The subject is the donor's stand-in
            Answer_t* asDragonborn;  // The subject is the Dragonborn's stand-in
            Answer_t* always;        // Whoever the subject is, unless one of the above answers
        };

        // A reference the donor's stand-in is asked about comes to these as the stand-in itself (the condition
        // evaluation hook swaps it), so "is this the donor" is the subject's own answer. The ones that answer for a
        // stand-in (asDonor or asDragonborn) get the puppet in self-dialogue as itself; the rest get the player.
        constexpr HookedCondition kHooked[] = {
            // The player is really in the donor's factions and its aliases' (FactionManager)
            { "GetInFaction"sv, &AskPlayer, &InFactionAsDragonborn, nullptr },
            { "GetFactionRank"sv, &AskPlayer, &FactionRankAsDragonborn, nullptr },
            { "GetIsID"sv, &IsIDAsDonor, &IsIDAsDragonborn, nullptr },
            { "GetIsReference"sv, nullptr, &IsReferenceAsDragonborn, nullptr },
            { "GetIsAliasRef"sv, &IsAliasRefAsDonor, &AskPlayer, nullptr },
            { "GetIsVoiceType"sv, &IsVoiceTypeAsDonor, &IsVoiceTypeAsDragonborn, nullptr },
            { "IsInList"sv, &IsInListAsDonor, &AskPlayer, nullptr },
            { "GetIsClass"sv, &AskDonor, &AskPlayer, nullptr },
            { "GetIsCrimeFaction"sv, &AskDonor, &AskPlayer, nullptr },
            { "IsInScene"sv, &IsInSceneAsDonor, nullptr, nullptr },
            { "GetIsRace"sv, &AskPlayer, &AskPuppet, nullptr },
            { "GetIsSex"sv, &AskPlayer, &AskPuppet, nullptr },
            { "GetRelationshipRank"sv, &Relationship, &Relationship, &Relationship },
            { "HasFamilyRelationship"sv, &Relationship, &Relationship, &Relationship },
            { "HasParentRelationship"sv, &Relationship, &Relationship, &Relationship },
            { "IsBleedingOut"sv, nullptr, nullptr, &IsBleedingOutAsDonor },
            { "GetPCIsClass"sv, nullptr, nullptr, &PCIsClass },
            { "GetPCInFaction"sv, nullptr, nullptr, &PCInFaction },
            { "GetPCIsRace"sv, nullptr, nullptr, &PCIsRace },
            { "GetPCIsSex"sv, nullptr, nullptr, &PCIsSex },
        };
        constexpr std::size_t kHookedCount = std::size(kHooked);

        constexpr std::size_t HookedIndex(std::string_view a_name) {
            for (std::size_t i = 0; i < kHookedCount; ++i) {
                if (kHooked[i].name == a_name) return i;
            }
            return kHookedCount;
        }
        constexpr std::size_t kGetIsRace = HookedIndex("GetIsRace"sv);
        constexpr std::size_t kGetIsSex = HookedIndex("GetIsSex"sv);
        static_assert(kGetIsRace < kHookedCount && kGetIsSex < kHookedCount);

        std::array<Condition_t*, kHookedCount> s_original{};

        // In self-dialogue the PC is the Dragonborn, whose race and sex the puppet's body has. The PC conditions
        // take the same parameter as GetIsRace and GetIsSex, so those are asked of the puppet.
        bool AskOfPuppetInSelfDialogue(const Query& a_q, std::size_t a_isCondition, double& a_result) {
            auto* isCondition = s_original[a_isCondition];
            if (a_q.session.IsInSelfDialogue() && a_q.puppet && isCondition) {
                return isCondition(a_q.puppet, a_q.param1, a_q.param2, a_result);
            }
            return a_q.Ask(a_q.subject, a_result);
        }
        bool PCIsRace(const Query& a_q, double& a_result) { return AskOfPuppetInSelfDialogue(a_q, kGetIsRace, a_result); }
        bool PCIsSex(const Query& a_q, double& a_result) { return AskOfPuppetInSelfDialogue(a_q, kGetIsSex, a_result); }

        template <std::size_t I>
        bool ConditionThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto* orig = s_original[I];
            if (!orig) return false;
            auto& session = *Session::GetSingleton();
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }

            const Query query{ orig, a_thisObj, a_p1, a_p2, session, session.GetPlayer(), session.GetDonor(), session.GetPuppet() };
            const bool isPlayer = a_thisObj == query.player;
            const bool isPuppet = query.puppet && a_thisObj == query.puppet;
            const bool selfDialogue = session.IsInSelfDialogue();
            const auto& hooked = kHooked[I];
            Answer_t* answer = nullptr;
            if (selfDialogue ? isPuppet : isPlayer) {
                answer = hooked.asDonor;
            } else if (selfDialogue ? isPlayer : isPuppet) {
                answer = hooked.asDragonborn;
            }
            if (!answer) answer = hooked.always;
            return answer ? answer(query, a_result) : orig(a_thisObj, a_p1, a_p2, a_result);
        }

        // --- Every condition: one about the donor is asked of its stand-in ---

        // TESConditionItem::IsTrue (29090, 29924) evaluates one condition: it resolves the subject from the item
        // (Subject is params.actionRef, Target params.targetRef, Reference looks up data.runOnRef), takes the
        // parameters as stored in data.functionData (unless they're alias or package data IDs, which reach the
        // player through the alias swap and PackageRetarget) and calls the condition function. Every condition list
        // goes through it (TESCondition::IsTrue -> 29895 -> here), and nothing else calls condition functions from
        // the script function table. So the item is evaluated with the donor's stand-in in its place:
        // - The player, whose body and place are the donor's: where it is, its health, combat, equipment and so on.
        //   "Distance between the donor and the player" is then the player's to itself.
        // - In self-dialogue, the puppet for the hooked conditions that answer for a stand-in, which answer as the
        //   donor for it. The puppet as the subject is the player for every other condition.
        // The donor as the subject is swapped only in a quest's context (params.quest): the fill conditions of an
        // alias a quest started during the session fills with the donor, before AliasSwapManager moves it. Without
        // one it's the parked donor's own spells and AI, which stay its own (a self-cast ability has the donor as its
        // target too; letting those through swapped dozens of its idle and ability conditions in testing).
        using IsTrue_t = bool(const RE::TESConditionItem*, RE::ConditionCheckParams&);
        IsTrue_t* g_originalIsTrue{ nullptr };

        constexpr auto kFunctionCount = static_cast<std::size_t>(RE::FUNCTION_DATA::FunctionID::kTotal);
        // The hooked conditions that answer as the donor for the puppet in self-dialogue
        std::array<bool, kFunctionCount> s_answersForPuppet{};

        // Answers that stay the donor's
        bool KeepsDonor(const RE::FUNCTION_DATA& a_function) {
            using enum RE::FUNCTION_DATA::FunctionID;
            switch (a_function.function.get()) {
            case kGetPlayerTeammate:
                return true;  // Teammate status stays on the donor
            case kGetInFaction:
            case kGetFactionRank:
                // The player keeps PlayerFaction from the Dragonborn; only the donor's own membership answers
                return a_function.params[0] == Forms::playerFaction;
            default:
                return false;
            }
        }

        // The donor's handle, made once per donor. Run-on references are handles, so they're compared by value
        // rather than looked up.
        std::uint32_t DonorHandle(RE::Actor* a_donor) {
            static std::atomic<RE::Actor*> s_donor{ nullptr };
            static std::atomic<std::uint32_t> s_handle{ 0 };
            if (s_donor.load(std::memory_order_acquire) != a_donor) {
                s_handle.store(a_donor->GetHandle().native_handle(), std::memory_order_relaxed);
                s_donor.store(a_donor, std::memory_order_release);
            }
            return s_handle.load(std::memory_order_relaxed);
        }

        // Logs the first swap of each condition function in a session, to show what's asked about the donor
        void LogSwap(RE::Actor* a_donor, std::size_t a_function, std::string_view a_how, const RE::TESObjectREFR* a_subject) {
            static std::array<std::atomic<bool>, kFunctionCount> s_logged{};
            static std::atomic<RE::Actor*> s_loggedFor{ nullptr };
            if (s_loggedFor.exchange(a_donor) != a_donor) {
                for (auto& logged : s_logged) logged.store(false, std::memory_order_relaxed);
            }
            if (s_logged[a_function].exchange(true)) return;
            const auto* command = RE::SCRIPT_FUNCTION::GetFirstScriptCommand();
            SKSE::log::info("Condition {} ({}) about the donor through its {} (subject {:08X} '{}'); asked of its stand-in",
                command ? command[a_function].functionName : "?", a_function, a_how, a_subject ? a_subject->GetFormID() : 0,
                a_subject ? a_subject->GetName() : "");
        }

        bool IsTrueThunk(const RE::TESConditionItem* a_item, RE::ConditionCheckParams& a_params) {
            const auto& session = *Session::GetSingleton();
            auto* donor = session.IsActive() ? session.GetDonor() : nullptr;
            const auto function = a_item ? static_cast<std::size_t>(a_item->data.functionData.function.get()) : kFunctionCount;
            if (!donor || function >= kFunctionCount) return g_originalIsTrue(a_item, a_params);

            auto* subject = a_params.actionRef;
            auto* puppet = session.GetPuppet();
            const bool puppetIsDonor = puppet && session.IsPuppetSpeakingForDonor();
            const bool forPuppet = s_answersForPuppet[function];
            // The parked donor's own spells and AI
            if (subject == donor && !a_params.quest) return g_originalIsTrue(a_item, a_params);

            const auto& data = a_item->data;
            const bool bySubject = subject == donor || (puppetIsDonor && subject == puppet && !forPuppet);
            const bool byReference = data.object == RE::CONDITIONITEMOBJECT::kRef &&
                                     data.runOnRef.native_handle() == DonorHandle(donor);
            const bool byTarget = a_params.targetRef == donor;
            const bool plainParams = !data.flags.usesAliases && !data.flags.usePackData;
            const bool byParam0 = plainParams && data.functionData.params[0] == donor;
            const bool byParam1 = plainParams && data.functionData.params[1] == donor;
            if (!(bySubject || byReference || byTarget || byParam0 || byParam1) || KeepsDonor(data.functionData)) {
                return g_originalIsTrue(a_item, a_params);
            }

            auto* player = session.GetPlayer();
            RE::Actor* standIn = puppetIsDonor && forPuppet ? puppet : player;
            LogSwap(donor, function,
                bySubject     ? "subject"sv :
                byReference   ? "run-on reference"sv :
                byTarget      ? "target"sv :
                                "parameter"sv,
                subject);

            // A copy of the item, which only this call sees
            alignas(RE::TESConditionItem) std::byte buffer[sizeof(RE::TESConditionItem)];
            std::memcpy(buffer, a_item, sizeof(RE::TESConditionItem));
            auto* copy = reinterpret_cast<RE::TESConditionItem*>(buffer);
            copy->next = nullptr;
            if (byReference) copy->data.runOnRef = standIn->GetHandle();
            if (byParam0) copy->data.functionData.params[0] = standIn;
            if (byParam1) copy->data.functionData.params[1] = standIn;

            // The params belong to the caller's evaluation (built on its stack), so the subject and target are
            // swapped in place for the call and put back
            auto* target = a_params.targetRef;
            if (bySubject) a_params.actionRef = standIn;
            if (byTarget) a_params.targetRef = standIn;
            const bool result = g_originalIsTrue(copy, a_params);
            a_params.actionRef = subject;
            a_params.targetRef = target;
            return result;
        }

        void HookIsTrue() {
            constexpr std::uint8_t kPrologue[] = { 0x48, 0x89, 0x4C, 0x24, 0x08 };  // mov [rsp+8], rcx
            g_originalIsTrue = HookUtil::HookFunctionStart(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(29090, 29924) }.address(),
                kPrologue, &IsTrueThunk, "condition evaluation"sv);
            if (!g_originalIsTrue) {
                SKSE::log::error("Conditions about the donor won't answer for the player");
            }
        }

        // Swaps in ConditionThunk<I> as the condition function of kHooked[I]
        template <std::size_t I>
        bool HookCondition() {
            const auto name = kHooked[I].name;
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(name);
            if (!func) {
                SKSE::log::error("Failed to locate script command '{}'!", name);
                return false;
            }
            if (const auto index = static_cast<std::size_t>(func - RE::SCRIPT_FUNCTION::GetFirstScriptCommand()); index < kFunctionCount) {
                s_answersForPuppet[index] = kHooked[I].asDonor || kHooked[I].asDragonborn;
            }
            s_original[I] = func->conditionFunction;
            REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(&ConditionThunk<I>));
            return true;
        }

        template <std::size_t... I>
        std::size_t HookConditions(std::index_sequence<I...>) {
            return (std::size_t{ 0 } + ... + HookCondition<I>());
        }

    }

    void Install() {
        HookIsTrue();
        const std::size_t hookedCount = HookConditions(std::make_index_sequence<kHookedCount>{});
        if (hookedCount == kHookedCount) {
            SKSE::log::info("Installed all {} script condition hooks.", hookedCount);
        } else {
            // A missing hook leaves the donor's conditions answered for the wrong actor; the errors above name it
            SKSE::log::error("Installed only {} of {} script condition hooks; impersonation conditions will be wrong.",
                hookedCount, kHookedCount);
        }
    }

}
