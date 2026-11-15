#include "PCH.h"
#include "Lifecycle.h"
#include "ActorHooks.h"
#include "AliasSwapManager.h"
#include "ConditionHooks.h"
#include "CrimeManager.h"
#include "DialogueManager.h"
#include "FaceManager.h"
#include "FactionManager.h"
#include "Forms.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "ImpersonationManager.h"
#include "NativeRedirect.h"
#include "PackageRetarget.h"
#include "PlayerVoice.h"
#include "Session.h"
#include "Settings.h"

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
        AliasSwapManager::GetSingleton().Initialize();
        ActorHooks::Install();
        ConditionHooks::Install();
        FaceManager::GetSingleton().InstallHook();
        ImpersonationManager::GetSingleton().Initialize();
        NativeRedirect::Install();
    }

    void OnPostLoadGame() {
        ImpersonationManager::GetSingleton().OnPostLoadGame();
        IdleAIManager::GetSingleton().OnPostLoadGame();
        if (auto& session = Session::GetSingleton(); session.IsActive()) {
            // Before FactionManager, which leaves the aliases' factions out of the ones it moves
            AliasSwapManager::GetSingleton().SwapNewDonorAliases();
            FactionManager::GetSingleton().OnPostLoadGame(session.GetDonor(), Session::GetPlayer());
            IdleAIManager::GetSingleton().OnImpersonationChanged();
            PackageRetarget::GetSingleton().RetargetDonorToPlayer(session.GetDonor(), Session::GetPlayer());
        }
    }

    void OnGameRevert() {
        ImpersonationManager::GetSingleton().OnRevert();
        AliasSwapManager::GetSingleton().Clear();
        FactionManager::GetSingleton().Clear();
        CrimeManager::GetSingleton().Clear();
        IdleAIManager::GetSingleton().Clear();
        DialogueManager::GetSingleton().Clear();
        IdleChatter::GetSingleton().Clear();
        PackageRetarget::GetSingleton().RestoreAll();
        // A save without settings (from an older version) gets the MCM's once it pushes them
        Settings::GetSingleton().Reset();
        SKSE::log::info("Reverted all InTheirShoes saved manager states.");
    }

    void OnFrame(float a_delta) {
        IdleAIManager::GetSingleton().Update(a_delta);
        DialogueManager::GetSingleton().Update();
        // After DialogueManager, which adopts aliases a fragment just gave the puppet
        if (Session::GetSingleton().IsActive()) {
            AliasSwapManager::GetSingleton().DropReleasedAliases();
        }
        ImpersonationManager::GetSingleton().Tick();
    }

    void OnSessionBegin() {
        CrimeManager::GetSingleton().OnSessionBegin(RE::PlayerCharacter::GetSingleton());
        IdleAIManager::GetSingleton().ClearPlayerPackages();
        IdleChatter::GetSingleton().InvalidateTopicCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged();
        PackageRetarget::GetSingleton().RetargetDonorToPlayer(Session::GetSingleton().GetDonor(), Session::GetPlayer());
    }

    void OnPuppetChangedInSelfDialogue() {
        if (auto& session = Session::GetSingleton(); session.IsActive()) {
            AliasSwapManager::GetSingleton().AdoptPuppetAliases(session.GetPuppet(), Session::GetPlayer());
            ImpersonationManager::GetSingleton().RestorePuppetName();
        }
    }

    void OnSelfDialogueEnded() {
        // Called from the menu event or the dialogue hook; IdleAIManager handles it in its next update
        IdleAIManager::GetSingleton().OnSelfDialogueEnded();
    }

    void OnSessionEnd() {
        // The player stops voicing the donor's line on revert or a switch to another donor
        PlayerVoice::GetSingleton().Finish();
        CrimeManager::GetSingleton().OnSessionEnd(RE::PlayerCharacter::GetSingleton());
        IdleChatter::GetSingleton().InvalidateTopicCache();
        IdleAIManager::GetSingleton().OnImpersonationChanged();
        PackageRetarget::GetSingleton().RestoreDonorToPlayer();
    }

}
