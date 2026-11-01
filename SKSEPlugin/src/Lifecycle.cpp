#include "PCH.h"
#include "Lifecycle.h"
#include "ActorHooks.h"
#include "AliasSwapManager.h"
#include "ConditionHooks.h"
#include "DialogueManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "ImpersonationManager.h"
#include "NativeRedirect.h"
#include "PackageRetarget.h"
#include "Session.h"

namespace InTheirShoes::Lifecycle {

    void OnInputLoaded() {
        IdleAIManager::GetSingleton().Initialize();
    }

    void OnDataLoaded() {
        // Installing a hook twice would make it call itself
        static bool s_loaded = false;
        if (s_loaded) return;
        s_loaded = true;

        Forms::Resolve();
        DialogueManager::GetSingleton().Initialize();
        ActorHooks::Install();
        ConditionHooks::Install();
        FaceManager::GetSingleton().InstallHook();
        ImpersonationManager::GetSingleton().Initialize();
        NativeRedirect::Install();
    }

    void OnPostLoadGame() {
        ImpersonationManager::GetSingleton().OnPostLoadGame();
        if (auto& session = Session::GetSingleton(); session.IsActive()) {
            IdleAIManager::GetSingleton().OnImpersonationChanged(true);
            PackageRetarget::GetSingleton().RetargetDonorToPlayer(session.GetDonor(), Session::GetPlayer());
        }
    }

    void OnGameRevert() {
        ImpersonationManager::GetSingleton().OnRevert();
        AliasSwapManager::GetSingleton().Clear();
        IdleAIManager::GetSingleton().Clear();
        DialogueManager::GetSingleton().Clear();
        IdleChatter::GetSingleton().Clear();
        // After IdleAIManager, which restores the targets it retargeted from the player to the puppet
        PackageRetarget::GetSingleton().Restore(false);
        SKSE::log::info("Reverted all InTheirShoes saved manager states.");
    }

    void OnFrame() {
        IdleAIManager::GetSingleton().Update();
        DialogueManager::GetSingleton().Update();
        // After DialogueManager, which adopts aliases a fragment just gave the puppet
        if (Session::GetSingleton().IsActive()) {
            AliasSwapManager::GetSingleton().DropReleasedAliases();
        }
        ImpersonationManager::GetSingleton().Tick();
    }

    void OnSessionBegin() {
        IdleAIManager::GetSingleton().ClearPlayerPackages();
        IdleChatter::GetSingleton().InvalidateTopicCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged(true);
        PackageRetarget::GetSingleton().RetargetDonorToPlayer(Session::GetSingleton().GetDonor(), Session::GetPlayer());
    }

    void OnPuppetChangedInSelfDialogue() {
        if (auto& session = Session::GetSingleton(); session.IsActive()) {
            AliasSwapManager::GetSingleton().AdoptPuppetAliases(session.GetPuppet(), Session::GetPlayer());
            ImpersonationManager::GetSingleton().RestorePuppetName();
        }
    }

    void OnSessionEnd() {
        IdleChatter::GetSingleton().InvalidateTopicCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged(false);
        // After IdleAIManager, which restores the targets it retargeted from the player to the puppet
        PackageRetarget::GetSingleton().Restore();
    }

}
