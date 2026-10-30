#include "PCH.h"
#include "Lifecycle.h"
#include "ActorHooks.h"
#include "AliasSwapManager.h"
#include "ConditionHooks.h"
#include "DialogueManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "GameUtil.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "ImpersonationManager.h"
#include "OutfitManager.h"
#include "Session.h"

namespace InTheirShoes::Lifecycle {

    void OnInputLoaded() {
        CheckMainThread();
        IdleAIManager::GetSingleton().Initialize();
    }

    void OnDataLoaded() {
        CheckMainThread();
        // Installing a hook twice would make it call itself
        static bool s_loaded = false;
        if (s_loaded) return;
        s_loaded = true;

        Forms::Resolve();
        DialogueManager::GetSingleton().Initialize();
        ActorHooks::Install();
        ConditionHooks::Install();
        OutfitManager::GetSingleton().InstallHook();
        FaceManager::GetSingleton().InstallHook();
        ImpersonationManager::GetSingleton().Initialize();
    }

    void OnPostLoadGame() {
        CheckMainThread();
        ImpersonationManager::GetSingleton().OnPostLoadGame();
        if (Session::GetSingleton().IsActive()) {
            IdleAIManager::GetSingleton().OnImpersonationChanged(true);
        }
    }

    void OnGameRevert() {
        CheckMainThread();
        ImpersonationManager::GetSingleton().OnRevert();
        AliasSwapManager::GetSingleton().Clear();
        OutfitManager::GetSingleton().Clear();
        IdleAIManager::GetSingleton().Clear();
        DialogueManager::GetSingleton().Clear();
        IdleChatter::GetSingleton().Clear();
        SKSE::log::info("Reverted all InTheirShoes saved manager states.");
    }

    void OnFrame() {
        IdleAIManager::GetSingleton().Update();
        DialogueManager::GetSingleton().Update();
        ImpersonationManager::GetSingleton().Tick();
    }

    void OnSessionBegin() {
        CheckMainThread();
        IdleAIManager::GetSingleton().ClearPlayerPackages();
        IdleChatter::GetSingleton().InvalidateTopicCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged(true);
    }

    void OnPuppetChangedInSelfDialogue() {
        CheckMainThread();
        ImpersonationManager::GetSingleton().ForwardPuppetFactionsToDonor();
        if (auto& session = Session::GetSingleton(); session.IsActive()) {
            AliasSwapManager::GetSingleton().AdoptPuppetAliases(session.GetPuppet(), Session::GetPlayer());
        }
    }

    void OnSessionEnd() {
        CheckMainThread();
        IdleChatter::GetSingleton().InvalidateTopicCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged(false);
    }

}
