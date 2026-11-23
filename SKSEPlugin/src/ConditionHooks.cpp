#include "PCH.h"
#include "ConditionHooks.h"
#include "FactionManager.h"
#include "Forms.h"
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

        bool AskSubject(const Query& a_q, double& a_result) {
            return a_q.Ask(a_q.subject, a_result);
        }

        // The puppet's body has the Dragonborn's race and sex, as the player's has the donor's. In self-dialogue
        // the bodies are the wrong way round, so race and sex are asked of the other body.
        bool AskPuppet(const Query& a_q, double& a_result) {
            auto* puppet = a_q.session.GetPuppet();
            return a_q.Ask(puppet ? puppet : a_q.subject, a_result);
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

        // The conditions that ask who an actor is. While impersonating, the player tests as the donor and the
        // puppet as the Dragonborn; in self-dialogue, where the puppet speaks for the donor, it's reversed.
        struct IdentityCondition {
            std::string_view name;
            Answer_t* asDonor;
            Answer_t* asDragonborn;
        };

        // A reference the donor's stand-in is asked about comes to these as the stand-in itself (the condition
        // evaluation hook swaps it), so "is this the donor" is the subject's own answer.
        constexpr IdentityCondition kIdentityConditions[] = {
            // The player is really in the donor's factions and its aliases' (FactionManager)
            { "GetInFaction"sv, &AskPlayer, &InFactionAsDragonborn },
            { "GetIsID"sv, &IsIDAsDonor, &IsIDAsDragonborn },
            { "GetFactionRank"sv, &AskPlayer, &FactionRankAsDragonborn },
            { "GetIsReference"sv, &AskSubject, &IsReferenceAsDragonborn },
            { "GetIsAliasRef"sv, &IsAliasRefAsDonor, &AskPlayer },
            { "GetIsVoiceType"sv, &IsVoiceTypeAsDonor, &IsVoiceTypeAsDragonborn },
            { "IsInList"sv, &IsInListAsDonor, &AskPlayer },
            { "GetIsClass"sv, &AskDonor, &AskPlayer },
            { "GetIsCrimeFaction"sv, &AskDonor, &AskPlayer },
            { "IsInScene"sv, &IsInSceneAsDonor, &AskSubject },
            { "GetIsRace"sv, &AskPlayer, &AskPuppet },
            { "GetIsSex"sv, &AskPlayer, &AskPuppet },
        };
        constexpr std::size_t kIdentityCount = std::size(kIdentityConditions);

        constexpr std::size_t IdentityIndex(std::string_view a_name) {
            for (std::size_t i = 0; i < kIdentityCount; ++i) {
                if (kIdentityConditions[i].name == a_name) return i;
            }
            return kIdentityCount;
        }
        constexpr std::size_t kGetIsRace = IdentityIndex("GetIsRace"sv);
        constexpr std::size_t kGetIsSex = IdentityIndex("GetIsSex"sv);
        static_assert(kGetIsRace < kIdentityCount && kGetIsSex < kIdentityCount);
        std::array<Condition_t*, kIdentityCount> s_originalIdentity{};

        template <std::size_t I>
        bool IdentityThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto* orig = s_originalIdentity[I];
            if (!orig) return false;
            auto& session = *Session::GetSingleton();
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

        // --- Bleedout: the player, as the donor, is down at no health ---

        Condition_t* s_originalIsBleedingOut{ nullptr };

        // The player only enters the bleedout life state when essential, so while it plays the donor it counts as
        // bleeding out at no health too (e.g. a mod that makes the player kneel instead of die). Not when dead.
        // Conditions asked of the donor come here as the player (the condition evaluation hook).
        bool IsBleedingOutThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            if (!s_originalIsBleedingOut) return false;
            const auto& session = *Session::GetSingleton();
            auto* player = session.GetPlayer();
            if (!session.IsActive() || a_thisObj != player) {
                return s_originalIsBleedingOut(a_thisObj, a_p1, a_p2, a_result);
            }
            const auto state = player->AsActorState()->GetLifeState();
            using enum RE::ACTOR_LIFE_STATE;
            const bool alive = state != kDead && state != kDying;
            const bool down = state == kBleedout || state == kEssentialDown ||
                              player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f;
            a_result = alive && down ? 1.0 : 0.0;
            return true;
        }

        // --- Relationships: the Dragonborn's apply first, then the donor's. The puppet keeps the Dragonborn's;
        // in self-dialogue the puppet is the donor and the player the Dragonborn. ---

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
            auto& session = *Session::GetSingleton();
            if (!session.IsActive()) {
                return orig(a_thisObj, a_p1, a_p2, a_result);
            }
            auto* ref = static_cast<RE::TESObjectREFR*>(a_p1);
            auto* evalThis = ResolveRelationshipRef(session, a_thisObj);
            auto* evalRef = ResolveRelationshipRef(session, ref);
            // The Dragonborn's own relationship first: the player's side asked as the player rather than the donor.
            // A rank of 0 (acquaintance) or false means the Dragonborn has none, so the donor's answers.
            auto* player = session.GetPlayer();
            const bool thisIsDonor = a_thisObj == player && evalThis != player;
            const bool refIsDonor = ref == player && evalRef != player;
            if (thisIsDonor != refIsDonor) {
                auto* ownThis = thisIsDonor ? player : evalThis;
                auto* ownRef = refIsDonor ? player : evalRef;
                if (ownThis != ownRef && orig(ownThis, ownRef, a_p2, a_result) && a_result != 0.0) {
                    return true;
                }
            }
            return orig(evalThis, evalRef, a_p2, a_result);
        }

        // --- The PC: the donor's race, sex and factions, the Dragonborn's class; the Dragonborn in self-dialogue ---

        // In self-dialogue the PC is the Dragonborn, whose race and sex the puppet's body has. The PC conditions
        // take the same parameter as GetIsRace and GetIsSex, so those are asked of the puppet.
        template <std::size_t IsCondition>
        bool PCIsAsPuppet(Condition_t* a_orig, RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            auto& session = *Session::GetSingleton();
            auto* puppet = session.GetPuppet();
            auto* isCondition = s_originalIdentity[IsCondition];
            if (session.IsActive() && session.IsInSelfDialogue() && puppet && isCondition) {
                return isCondition(puppet, a_p1, a_p2, a_result);
            }
            return a_orig(a_thisObj, a_p1, a_p2, a_result);
        }

        Condition_t* s_originalPCIsRace{ nullptr };
        Condition_t* s_originalPCIsSex{ nullptr };

        bool PCIsRaceThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            return s_originalPCIsRace && PCIsAsPuppet<kGetIsRace>(s_originalPCIsRace, a_thisObj, a_p1, a_p2, a_result);
        }

        bool PCIsSexThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            return s_originalPCIsSex && PCIsAsPuppet<kGetIsSex>(s_originalPCIsSex, a_thisObj, a_p1, a_p2, a_result);
        }

        Condition_t* s_originalPCIsClass{ nullptr };

        bool PCIsClassThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
            if (!s_originalPCIsClass) return false;
            auto& session = *Session::GetSingleton();
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
            auto& session = *Session::GetSingleton();
            if (session.IsActive() && session.IsInSelfDialogue()) {
                if (auto rank = FactionManager::GetSingleton()->GetDragonbornFactionRank(static_cast<RE::TESFaction*>(a_p1))) {
                    a_result = *rank >= 0 ? 1.0 : 0.0;
                    return true;
                }
            }
            return s_originalPCInFaction(a_thisObj, a_p1, a_p2, a_result);
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
        // - In self-dialogue, the puppet for the identity and relationship conditions, whose own hooks answer as
        //   the donor for it. The puppet as the subject is the player for every other condition.
        // The donor as the subject is swapped only in a quest's context (params.quest): the fill conditions of an
        // alias a quest started during the session fills with the donor, before AliasSwapManager moves it. Without
        // one it's the parked donor's own spells and AI, which stay its own (a self-cast ability has the donor as its
        // target too; letting those through swapped dozens of its idle and ability conditions in testing).
        using IsTrue_t = bool(const RE::TESConditionItem*, RE::ConditionCheckParams&);
        IsTrue_t* g_originalIsTrue{ nullptr };

        constexpr auto kFunctionCount = static_cast<std::size_t>(RE::FUNCTION_DATA::FunctionID::kTotal);
        // The identity and relationship conditions, whose hooks answer as the donor for the puppet in self-dialogue
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
            // The function's start is replaced by a jump to the hook; its first instruction (5 bytes, position
            // independent) runs in a stub that then jumps back. It's checked first, in case another mod hooked it
            // or the game version differs.
            const auto function = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(29090, 29924) }.address();
            constexpr std::uint8_t kFirstInstruction[] = { 0x48, 0x89, 0x4C, 0x24, 0x08 };  // mov [rsp+8], rcx
            if (std::memcmp(reinterpret_cast<const void*>(function), kFirstInstruction, sizeof(kFirstInstruction)) != 0) {
                SKSE::log::error("Condition evaluation starts differently than expected; conditions on the donor's "
                                 "reference won't answer for the player");
                return;
            }

            struct Original : Xbyak::CodeGenerator {
                explicit Original(std::uintptr_t a_resume) {
                    Xbyak::Label resume;
                    mov(ptr[rsp + 8], rcx);
                    jmp(ptr[rip + resume]);
                    L(resume);
                    dq(a_resume);
                }
            } original{ function + sizeof(kFirstInstruction) };
            original.ready();

            auto& trampoline = SKSE::GetTrampoline();
            g_originalIsTrue = static_cast<IsTrue_t*>(trampoline.allocate(original));
            trampoline.write_branch<5>(function, &IsTrueThunk);
            SKSE::log::info("Hooked condition evaluation at SkyrimSE+{:X}", function - REL::Module::get().base());
        }

        // Swaps in a_thunk as the named condition's function, keeping the original in a_original.
        // a_answersForPuppet: the thunk answers as the donor for the puppet in self-dialogue.
        bool HookCondition(Condition_t*& a_original, std::string_view a_name, Condition_t* a_thunk, bool a_answersForPuppet = false) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(a_name);
            if (!func) {
                SKSE::log::error("Failed to locate script command '{}'!", a_name);
                return false;
            }
            if (const auto index = static_cast<std::size_t>(func - RE::SCRIPT_FUNCTION::GetFirstScriptCommand()); index < kFunctionCount) {
                s_answersForPuppet[index] = a_answersForPuppet;
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
            ((hooked += HookCondition(s_originalIdentity[I], kIdentityConditions[I].name, &IdentityThunk<I>, true)), ...);
            return hooked;
        }

        template <std::size_t... I>
        std::size_t HookRelationshipConditions(std::index_sequence<I...>) {
            std::size_t hooked = 0;
            ((hooked += HookCondition(s_originalRelationship[I], kRelationshipConditions[I], &RelationshipThunk<I>, true)), ...);
            return hooked;
        }

    }

    void Install() {
        HookIsTrue();
        const std::size_t hookedCount = HookIdentityConditions(std::make_index_sequence<kIdentityCount>{}) +
                                        HookCondition(s_originalIsBleedingOut, "IsBleedingOut"sv, &IsBleedingOutThunk) +
                                        HookRelationshipConditions(std::make_index_sequence<kRelationshipCount>{}) +
                                        HookCondition(s_originalPCIsClass, "GetPCIsClass"sv, &PCIsClassThunk) +
                                        HookCondition(s_originalPCInFaction, "GetPCInFaction"sv, &PCInFactionThunk) +
                                        HookCondition(s_originalPCIsRace, "GetPCIsRace"sv, &PCIsRaceThunk) +
                                        HookCondition(s_originalPCIsSex, "GetPCIsSex"sv, &PCIsSexThunk);

        constexpr auto kExpectedHooks = kIdentityCount + kRelationshipCount + 5;
        if (hookedCount == kExpectedHooks) {
            SKSE::log::info("Installed all {} script condition hooks.", hookedCount);
        } else {
            // A missing hook leaves the donor's conditions answered for the wrong actor; the errors above name it
            SKSE::log::error("Installed only {} of {} script condition hooks; impersonation conditions will be wrong.",
                hookedCount, kExpectedHooks);
        }
    }

}
