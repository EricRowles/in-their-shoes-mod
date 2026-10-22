#include "PCH.h"
#include "Papyrus.h"
#include "IdleAIManager.h"
#include "DialogueManager.h"
#include "ImpersonationManager.h"
#include <RE/C/ControlMap.h>
#include <RE/T/TESRace.h>

namespace InTheirShoes::Papyrus {

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
        DialogueManager::GetSingleton().SetIdleChatterTime(a_seconds);
    }

    bool OpenSelfDialogue(RE::StaticFunctionTag*, RE::Actor* a_speaker) {
        SKSE::log::info("Papyrus::OpenSelfDialogue called with speaker {:08X}",
            a_speaker ? static_cast<std::uint32_t>(a_speaker->GetFormID()) : 0);
        return DialogueManager::GetSingleton().OpenSelfDialogue(a_speaker);
    }

    void SetNPCVoiceEnabled(RE::StaticFunctionTag*, bool a_enabled) {
        ImpersonationManager::GetSingleton().SetNPCVoiceEnabled(a_enabled);
    }

    void SetPuppetFollowsPlayer(RE::StaticFunctionTag*, bool a_follows) {
        ImpersonationManager::GetSingleton().SetPuppetFollowsPlayer(a_follows);
    }

    std::int32_t ValidateImpersonateTarget(RE::StaticFunctionTag*, RE::Actor* a_target) {
        if (!a_target) {
            return 1;
        }
        if (a_target->IsPlayerRef()) {
            return 2;
        }
        auto* actorState = a_target->AsActorState();
        if (actorState) {
            auto lifeState = actorState->GetLifeState();
            if (lifeState == RE::ACTOR_LIFE_STATE::kDead ||
                lifeState == RE::ACTOR_LIFE_STATE::kDying ||
                lifeState == RE::ACTOR_LIFE_STATE::kBleedout ||
                lifeState == RE::ACTOR_LIFE_STATE::kEssentialDown) {
                return 3;
            }
        }
        if (a_target->IsDead() || a_target->IsInKillMove()) {
            return 3;
        }
        if (a_target->IsGhost()) {
            return 7;
        }
        auto* base = a_target->GetActorBase();
        if (!base) {
            return 1;
        }
        auto* npc = skyrim_cast<RE::TESNPC*>(base);
        if (!npc) {
            return 5;
        }
        auto* race = npc->GetRace();
        if (!race) {
            race = a_target->GetRace();
        }
        if (!race || !race->data.flags.all(RE::RACE_DATA::Flag::kFaceGenHead)) {
            return 5;
        }
        if (race->IsChildRace() || race->data.flags.all(RE::RACE_DATA::Flag::kChild)) {
            return 4;
        }
        if (a_target->IsOnMount()) {
            return 6;
        }
        return 0;
    }

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            SKSE::log::error("Papyrus VM is null!");
            return false;
        }

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

        SKSE::log::info("Registered ITS_NG Papyrus functions.");
        return true;
    }

}
