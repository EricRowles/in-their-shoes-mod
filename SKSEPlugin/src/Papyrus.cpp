#include "PCH.h"
#include "Papyrus.h"
#include "IdleAIManager.h"
#include "DialogueManager.h"
#include "GameUtil.h"
#include "IdleChatter.h"
#include "ImpersonationManager.h"
#include "Session.h"
#include "Version.h"
#include <RE/C/ControlMap.h>
#include <RE/T/TESRace.h>
#include <RE/F/FunctionArguments.h>

namespace InTheirShoes::Papyrus {

    namespace {
        bool Impersonate(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_puppet) {
            return ImpersonationManager::GetSingleton().Impersonate(a_donor, a_puppet);
        }

        bool Revert(RE::StaticFunctionTag*) {
            return ImpersonationManager::GetSingleton().Revert();
        }

        void SetIdleAIEnabled(RE::StaticFunctionTag*, bool a_enabled) {
            IdleAIManager::GetSingleton().SetIdleAIEnabled(a_enabled);
        }

        void SetIdleAITimeout(RE::StaticFunctionTag*, float a_seconds) {
            IdleAIManager::GetSingleton().SetIdleTimeout(a_seconds);
        }

        void SetIdleChatterTime(RE::StaticFunctionTag*, float a_seconds) {
            IdleChatter::GetSingleton().SetInterval(a_seconds);
        }

        bool OpenSelfDialogue(RE::StaticFunctionTag*, RE::Actor* a_speaker) {
            SKSE::log::info("OpenSelfDialogue called from Papyrus with speaker {:08X}",
                a_speaker ? a_speaker->GetFormID() : 0u);
            return DialogueManager::GetSingleton().OpenSelfDialogue(a_speaker);
        }

        void SetNPCVoiceEnabled(RE::StaticFunctionTag*, bool a_enabled) {
            Session::GetSingleton().SetNPCVoiceEnabled(a_enabled);
        }

        void SetPuppetFollowsPlayer(RE::StaticFunctionTag*, bool a_follows) {
            ImpersonationManager::GetSingleton().SetPuppetFollowsPlayer(a_follows);
        }

        // ValidateImpersonateTarget's result. The values are part of the Papyrus API (documented in ITS_NG.psc,
        // checked in ITS_QuestScript.psc), so never renumber them.
        enum class TargetValidation : std::int32_t {
            kValid = 0,
            kInvalid = 1,
            kPlayer = 2,
            kDead = 3,
            kChild = 4,
            kNotHumanoid = 5,
            kMounted = 6,
            kGhost = 7,
            kPlayerTransformed = 8,
        };

        TargetValidation ValidateTarget(RE::Actor* a_target) {
            if (!a_target) {
                return TargetValidation::kInvalid;
            }
            if (a_target->IsPlayerRef()) {
                return TargetValidation::kPlayer;
            }
            auto* actorState = a_target->AsActorState();
            if (actorState) {
                auto lifeState = actorState->GetLifeState();
                if (lifeState == RE::ACTOR_LIFE_STATE::kDead ||
                    lifeState == RE::ACTOR_LIFE_STATE::kDying ||
                    lifeState == RE::ACTOR_LIFE_STATE::kBleedout ||
                    lifeState == RE::ACTOR_LIFE_STATE::kEssentialDown) {
                    return TargetValidation::kDead;
                }
            }
            if (a_target->IsDead() || a_target->IsInKillMove()) {
                return TargetValidation::kDead;
            }
            if (a_target->IsGhost()) {
                return TargetValidation::kGhost;
            }
            auto* base = a_target->GetActorBase();
            if (!base) {
                return TargetValidation::kInvalid;
            }
            auto* npc = skyrim_cast<RE::TESNPC*>(base);
            if (!npc) {
                return TargetValidation::kNotHumanoid;
            }
            auto* race = npc->GetRace();
            if (!race) {
                race = a_target->GetRace();
            }
            if (!race || !race->data.flags.all(RE::RACE_DATA::Flag::kFaceGenHead)) {
                return TargetValidation::kNotHumanoid;
            }
            if (race->IsChildRace() || race->data.flags.all(RE::RACE_DATA::Flag::kChild)) {
                return TargetValidation::kChild;
            }
            if (a_target->IsOnMount()) {
                return TargetValidation::kMounted;
            }
            if (ImpersonationManager::IsPlayerTransformed()) {
                return TargetValidation::kPlayerTransformed;
            }
            return TargetValidation::kValid;
        }

        std::int32_t ValidateImpersonateTarget(RE::StaticFunctionTag*, RE::Actor* a_target) {
            return std::to_underlying(ValidateTarget(a_target));
        }

        // The plugin's version (project VERSION in CMakeLists.txt), shown in the MCM
        RE::BSFixedString GetPluginVersion(RE::StaticFunctionTag*) {
            return fmt::format("{}.{}.{}", ITS_VERSION_MAJOR, ITS_VERSION_MINOR, ITS_VERSION_PATCH);
        }

        bool IsImpersonating(RE::StaticFunctionTag*) {
            return Session::GetSingleton().IsActive();
        }

        bool IsPlayerTransformed(RE::StaticFunctionTag*) {
            return ImpersonationManager::IsPlayerTransformed();
        }

        using VM = RE::BSScript::IVirtualMachine;

        // --- Trigger events ---
        // When the player enters or leaves a trigger with a script property set to the donor,
        // the trigger also gets the event for the donor.
        // Runs on any thread: reads only Session and the VM's locked script table.

        using SendEvent_t = void(VM*, RE::VMHandle, const RE::BSFixedString&, RE::BSScript::IFunctionArguments*);
        REL::Relocation<SendEvent_t> _origSendEvent;

        bool IsPlayerArgument(const RE::BSScript::IFunctionArguments* a_args) {
            RE::BSScrapArray<RE::BSScript::Variable> args;
            (*a_args)(args);
            return !args.empty() && HoldsForm(args[0], RE::PlayerCharacter::GetSingleton());
        }

        // Whether any script on the object has a variable (auto properties included) holding a_donor
        bool HasDonorProperty(VM* a_vm, RE::VMHandle a_handle, const RE::Actor* a_donor) {
            auto* vm = static_cast<RE::BSScript::Internal::VirtualMachine*>(a_vm);
            RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
            auto it = vm->attachedScripts.find(a_handle);
            if (it == vm->attachedScripts.end()) return false;
            for (auto& script : it->second) {
                const auto* object = script.get();
                const auto* type = object ? object->GetTypeInfo() : nullptr;
                if (!type) continue;
                for (std::uint32_t i = 0; i < type->GetTotalNumVariables(); ++i) {
                    if (HoldsForm(object->variables[i], a_donor)) return true;
                }
            }
            return false;
        }

        void SendEvent(VM* a_vm, RE::VMHandle a_handle, const RE::BSFixedString& a_eventName, RE::BSScript::IFunctionArguments* a_args) {
            static const RE::BSFixedString onTriggerEnter{ "OnTriggerEnter" };
            static const RE::BSFixedString onTriggerLeave{ "OnTriggerLeave" };
            auto& session = Session::GetSingleton();
            auto* donor = session.IsActive() ? session.GetDonor() : nullptr;
            // The VM owns a_args once the event is sent, so they're read first
            const bool sendForDonor = donor && a_args && (a_eventName == onTriggerEnter || a_eventName == onTriggerLeave) &&
                                      IsPlayerArgument(a_args) && HasDonorProperty(a_vm, a_handle, donor);
            _origSendEvent(a_vm, a_handle, a_eventName, a_args);
            if (sendForDonor) {
                SKSE::log::info("Sent {} for the donor to handle {:X}", a_eventName.c_str(), a_handle);
                _origSendEvent(a_vm, a_handle, a_eventName, RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(donor)));
            }
        }

        void HookSendEvent() {
            if (_origSendEvent.address()) return;
            REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSScript__Internal__VirtualMachine[0] };
            _origSendEvent = vtbl.write_vfunc(0x24, &SendEvent);
            SKSE::log::info("Hooked VirtualMachine::SendEvent (0x24) to send trigger events for the donor.");
        }
    }

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            SKSE::log::error("Papyrus VM is null!");
            return false;
        }

        HookSendEvent();

        constexpr std::string_view className = "ITS_NG";

        a_vm->RegisterFunction("Impersonate", className, Impersonate);
        a_vm->RegisterFunction("Revert", className, Revert);
        a_vm->RegisterFunction("SetIdleAIEnabled", className, SetIdleAIEnabled);
        a_vm->RegisterFunction("SetIdleAITimeout", className, SetIdleAITimeout);
        a_vm->RegisterFunction("SetIdleChatterTime", className, SetIdleChatterTime);
        a_vm->RegisterFunction("OpenSelfDialogue", className, OpenSelfDialogue);
        a_vm->RegisterFunction("SetNPCVoiceEnabled", className, SetNPCVoiceEnabled);
        a_vm->RegisterFunction("SetPuppetFollowsPlayer", className, SetPuppetFollowsPlayer);
        a_vm->RegisterFunction("ValidateImpersonateTarget", className, ValidateImpersonateTarget);
        a_vm->RegisterFunction("IsPlayerTransformed", className, IsPlayerTransformed);
        a_vm->RegisterFunction("GetPluginVersion", className, GetPluginVersion);
        a_vm->RegisterFunction("IsImpersonating", className, IsImpersonating);

        SKSE::log::info("Registered ITS_NG Papyrus functions.");
        return true;
    }

}
