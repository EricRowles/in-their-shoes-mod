#include "PCH.h"
#include "Lifecycle.h"
#include "ActorHooks.h"
#include "AliasSwapManager.h"
#include "ConditionHooks.h"
#include "DialogueManager.h"
#include "FaceManager.h"
#include "GameClock.h"
#include "Forms.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "ImpersonationManager.h"
#include "NativeRedirect.h"
#include "PackageRetarget.h"
#include "PlayerMoveSpeed.h"
#include "PlayerVoice.h"
#include "Session.h"
#include "SessionStash.h"
#include "Settings.h"

namespace InTheirShoes::Lifecycle {

    void OnInputLoaded() {
        IdleAIManager::GetSingleton()->Initialize();
        PlayerMoveSpeed::Install();
    }

    void OnDataLoaded() {
        // Installing a hook twice would make it call itself
        static bool s_loaded = false;
        if (s_loaded) return;
        s_loaded = true;

        Forms::Resolve();
        DialogueManager::GetSingleton()->Initialize();
        AliasSwapManager::GetSingleton()->Initialize();
        ActorHooks::Install();
        ConditionHooks::Install();
        FaceManager::GetSingleton()->InstallHook();
        ImpersonationManager::GetSingleton()->Initialize();
        NativeRedirect::Install();
    }

    void OnPostLoadGame() {
        ImpersonationManager::GetSingleton()->OnPostLoadGame();
        IdleAIManager::GetSingleton()->OnPostLoadGame();
        if (auto& session = *Session::GetSingleton(); session.IsActive()) {
            AliasSwapManager::GetSingleton()->SwapNewDonorAliases();
            IdleAIManager::GetSingleton()->OnImpersonationChanged();
            PackageRetarget::GetSingleton()->RetargetDonorToPlayer(session.GetDonor(), Session::GetPlayer());
        }
    }

    void OnGameRevert() {
        ImpersonationManager::GetSingleton()->OnRevert();
        AliasSwapManager::GetSingleton()->Clear();
        for (auto* stash : SessionStash::All()) {
            stash->Clear();
        }
        IdleAIManager::GetSingleton()->Clear();
        DialogueManager::GetSingleton()->Clear();
        PlayerVoice::GetSingleton()->Clear();
        IdleChatter::GetSingleton()->Clear();
        PackageRetarget::GetSingleton()->RestoreAll();
        // A save without settings (a new game) gets the defaults
        Settings::GetSingleton()->Reset();
        SKSE::log::info("Reverted all InTheirShoes saved manager states.");
    }

    void OnFrame() {
        GameClock::Tick();
        IdleAIManager::GetSingleton()->Update();
        DialogueManager::GetSingleton()->Update();
        // After DialogueManager, which adopts aliases a fragment just gave the puppet
        if (Session::GetSingleton()->IsActive()) {
            AliasSwapManager::GetSingleton()->DropReleasedAliases();
        }
        ImpersonationManager::GetSingleton()->Tick();
    }

    void OnSessionBegin() {
        // Before idle AI re-evaluates the player's package, whose target may be a linked ref
        for (auto* stash : SessionStash::All()) {
            stash->Begin(Session::GetSingleton()->GetDonor(), RE::PlayerCharacter::GetSingleton());
        }
        IdleChatter::GetSingleton()->InvalidateTopicCache();
        IdleAIManager::GetSingleton()->OnImpersonationChanged();
        PackageRetarget::GetSingleton()->RetargetDonorToPlayer(Session::GetSingleton()->GetDonor(), Session::GetPlayer());
    }

    void OnPuppetChangedInSelfDialogue() {
        if (auto& session = *Session::GetSingleton(); session.IsActive()) {
            AliasSwapManager::GetSingleton()->AdoptPuppetAliases(session.GetPuppet(), Session::GetPlayer());
            ImpersonationManager::GetSingleton()->RestorePuppetName();
        }
    }

    void OnSelfDialogueEnded() {
        // Called from the menu event or the dialogue hook; IdleAIManager handles it in its next update
        IdleAIManager::GetSingleton()->OnSelfDialogueEnded();
    }

    void OnSessionEnd() {
        // The player stops voicing the donor's line on revert or a switch to another donor
        PlayerVoice::GetSingleton()->Finish();
        for (auto* stash : SessionStash::All()) {
            stash->End(Session::GetSingleton()->GetDonor(), RE::PlayerCharacter::GetSingleton());
        }
        IdleChatter::GetSingleton()->InvalidateTopicCache();
        IdleAIManager::GetSingleton()->OnImpersonationChanged();
        PackageRetarget::GetSingleton()->RestoreDonorToPlayer();
    }

}
