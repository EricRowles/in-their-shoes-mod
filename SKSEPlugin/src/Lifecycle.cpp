#include "PCH.h"
#include "Lifecycle.h"
#include "ActorHooks.h"
#include "AliasSwapManager.h"
#include "ConditionHooks.h"
#include "DialogueManager.h"
#include "Forms.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "OutfitManager.h"
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
        OutfitManager::GetSingleton().InstallHook();
        ImpersonationManager::GetSingleton().Initialize();
    }

    void OnPostLoadGame() {
        ImpersonationManager::GetSingleton().OnPostLoadGame();
        if (Session::GetSingleton().IsActive()) {
            IdleAIManager::GetSingleton().OnImpersonationChanged(true);
        }
    }

    void OnGameRevert() {
        ImpersonationManager::GetSingleton().OnRevert();
        AliasSwapManager::GetSingleton().Clear();
        OutfitManager::GetSingleton().Clear();
        IdleAIManager::GetSingleton().Clear();
        DialogueManager::GetSingleton().Clear();
        SKSE::log::info("Reverted all InTheirShoes saved manager states.");
    }

    void OnFrame() {
        IdleAIManager::GetSingleton().Update();
        DialogueManager::GetSingleton().Update();
        ImpersonationManager::GetSingleton().Tick();
    }

    void OnSessionBegin() {
        IdleAIManager::GetSingleton().ClearPlayerPackages();
        DialogueManager::GetSingleton().InvalidateIdleQuestCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged(true);
    }

    void OnSessionEnd() {
        DialogueManager::GetSingleton().InvalidateIdleQuestCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged(false);
    }

}
