#include "PCH.h"
#include "DialogueManager.h"
#include "Lifecycle.h"
#include "GameUtil.h"
#include "HookUtil.h"
#include "Session.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "PlayerVoice.h"
#include "QuestConditions.h"

namespace InTheirShoes {

    // The puppet stays the engine's speaker this long after self-dialogue's menu closes (see EndSelfDialogue)
    constexpr float kPuppetSpeakerClearSeconds = 1.0f;

    namespace {
        void QueueSubtitle(RE::Actor* a_speaker, const RE::DialogueResponse* a_response) {
            if (a_response->text.empty()) return;
            auto* subMgr = RE::SubtitleManager::GetSingleton();
            if (!subMgr) return;

            RE::BSSpinLockGuard locker(subMgr->lock);
            RE::SubtitleInfo info;
            info.speaker = a_speaker->GetHandle();
            info.subtitle = a_response->text;
            info.targetDistance = 0.0f;
            info.forceDisplay = true;
            subMgr->subtitles.push_back(info);
        }

        // The engine leaves the puppet as MenuTopicManager's speaker after self-dialogue, which blocks
        // activating it again
        void ClearPuppetAsSpeaker() {
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            auto* puppet = Session::GetSingleton()->GetPuppet();
            if (!topicMgr || !puppet) return;
            auto handle = puppet->GetHandle();
            if (topicMgr->speaker == handle) {
                topicMgr->speaker.reset();
            }
            if (topicMgr->lastSpeaker == handle) {
                topicMgr->lastSpeaker.reset();
            }
        }

        // Set while Character::UpdateInDialogue runs on this thread: lines it starts are already routed there
        thread_local bool t_inUpdateInDialogue = false;

        // The puppet in self-dialogue speaks the donor's lines. (Lines scripts give the parked donor go to the
        // player when NativeRedirect is on: it sends their Say() there.)
        bool IsPuppetStandIn(const RE::Actor* a_actor) {
            auto& session = *Session::GetSingleton();
            return a_actor && session.IsActive() && a_actor == session.GetPuppet() && session.IsInSelfDialogue();
        }
    }

    // --- Setup ---

    void DialogueManager::Initialize() {
        if (_initialized) return;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
        } else {
            SKSE::log::warn("DialogueManager failed to get UI singleton.");
        }
        InstallHooks();
        _initialized = true;
    }

    void DialogueManager::InstallHooks() {
        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[0] };
        _origSetDialogueWithPlayer = charVtbl.write_vfunc(0x41, &Hook_SetDialogueWithPlayer);
        _origCharUpdateInDialogue = charVtbl.write_vfunc(0x4C, &Hook_Character_UpdateInDialogue);

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerUpdateInDialogue = playerVtbl.write_vfunc(0x4C, &Hook_Player_UpdateInDialogue);
        _origPlayerQSpeakingDone = playerVtbl.write_vfunc(0x107, &Hook_Player_QSpeakingDone);
        _origPlayerSetSpeakingDone = playerVtbl.write_vfunc(0x108, &Hook_Player_SetSpeakingDone);

        SKSE::log::info("Hooked Character (0x41, 0x4C) and PlayerCharacter (0x4C, 0x107, 0x108) dialogue vfuncs");

        // SetupSpecialIdle plays an actor's idles, the talking idle (gestures) among them
        constexpr std::uint8_t kSetupSpecialIdlePrologue[] = { 0x44, 0x89, 0x44, 0x24, 0x18 };  // mov [rsp+18h], r8d
        _origTalkingIdle = HookUtil::HookFunctionStart(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(38290, 39256) }.address(),
            kSetupSpecialIdlePrologue, &Hook_TalkingIdle, "special idle setup"sv);
        // The start-speech function (AE 37542; the SE ID was matched by function layout, not tested on SE)
        constexpr std::uint8_t kStartSpeechPrologue[] = { 0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20 };  // mov rax, rsp / mov [rax+20h], r9d
        _origStartSpeech = HookUtil::HookFunctionStart(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(36541, 37542) }.address(),
            kStartSpeechPrologue, &Hook_StartSpeech, "start speech"sv);
    }

    // One-line greetings open no Dialogue Menu and start their voice here, not in UpdateInDialogue, so the puppet
    // spoke them. Voices them on the player instead; the engine still times the line by the return value. (#43)
    float DialogueManager::Hook_StartSpeech(RE::Actor* a_speaker, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4,
        std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9, std::uint64_t a10,
        std::uint64_t a11, std::uint64_t a12, std::uint64_t a13, std::uint64_t a14) {
        auto& session = *Session::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!t_inUpdateInDialogue && a_speaker && session.IsActive() && a_speaker == session.GetPuppet() &&
            session.IsPuppetSpeakingForDonor() && !(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                SKSE::log::info("Puppet line outside a conversation: voicing it on the player");
                PlayerVoice::GetSingleton()->Finish();
                a_speaker = player;
            }
        }
        return _origStartSpeech(a_speaker, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
    }

    void DialogueManager::Update() {
        if (_pendingSelfDialogue.exchange(false, std::memory_order_acq_rel)) {
            auto* topic = _pendingSelfDialogueTopic.exchange(nullptr, std::memory_order_acq_rel);
            if (auto* puppet = Session::GetSingleton()->GetPuppet()) {
                OpenSelfDialogueDirect(puppet, topic);
            }
        }
        PlayerVoice::GetSingleton()->Update();
        EndSelfDialogueWithoutMenu();
        AdvancePastUnvoicedLine();
        if (Session::GetSingleton()->IsPuppetSpeakingForDonor()) {
            // Topic fragments may put the puppet into quest aliases; move them to the player until it stops being the
            // speaker (the last fragment runs after the menu closes).
            Lifecycle::OnPuppetChangedInSelfDialogue();
        }
        UpdatePuppetSpeakerClear();
        if (!PlayerVoice::GetSingleton()->IsPlaying()) {
            if (auto item = IdleChatter::GetSingleton()->Poll()) {
                PlayerVoice::GetSingleton()->SpeakIdleChatter(std::move(item));
            }
        }
    }

    void DialogueManager::Clear() {
        Session::GetSingleton()->SetInSelfDialogue(false);
        _pendingSelfDialogue.store(false, std::memory_order_release);
        _pendingSelfDialogueTopic.store(nullptr, std::memory_order_release);
        Session::GetSingleton()->SetPuppetSpeakerPending(false);
        _puppetSpeakerClearAt.reset();
        _awaitingMenuSince.reset();
    }

    void DialogueManager::EndSelfDialogueWithoutMenu() {
        // A one-line greeting opens no Dialogue Menu, so no menu close ended self-dialogue, and the player kept
        // testing as the Dragonborn (and idle chatter kept waiting) until the next conversation
        if (!_awaitingMenuSince) return;
        auto* ui = RE::UI::GetSingleton();
        if (!Session::GetSingleton()->IsInSelfDialogue() || (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
            _awaitingMenuSince.reset();
            return;
        }
        if (GameClock::SecondsSince(*_awaitingMenuSince) < kNoMenuSeconds) return;
        _awaitingMenuSince.reset();
        SKSE::log::info("Self-dialogue opened no menu (a one-line greeting)");
        EndSelfDialogue();
    }

    // --- Self-dialogue ---

    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (a_event && !a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME && Session::GetSingleton()->IsInSelfDialogue()) {
            EndSelfDialogue();
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    bool DialogueManager::OpenSelfDialogue(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic) {
        auto* puppet = Session::GetSingleton()->GetPuppet();
        if (!a_speaker) {
            a_speaker = puppet;
        }
        if (!a_speaker || a_speaker != puppet) {
            SKSE::log::warn("Self-dialogue not opened: the speaker must be the puppet");
            return false;
        }

        // Opened from Update(), at a fixed point in the frame
        _pendingSelfDialogueTopic.store(a_forceGreetTopic, std::memory_order_release);
        _pendingSelfDialogue.store(true, std::memory_order_release);
        return true;
    }

    bool DialogueManager::OpenSelfDialogueDirect(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!player || (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsModalMenuOpen()))) {
            SKSE::log::warn("Self-dialogue not opened: a menu is open or the game is paused");
            return false;
        }

        IdleAIManager::GetSingleton()->StopForSelfDialogue();
        PlayerVoice::GetSingleton()->Finish();

        // The conversation ends while the player's dialogue timer (awarePlayerTimer, +0x340) is negative, and it keeps
        // counting down outside dialogue.
        if (auto* process = player->GetActorRuntimeData().currentProcess; process && process->high &&
            process->high->awarePlayerTimer < 0.0f) {
            SKSE::log::info("Reset the player's dialogue timer from {:.1f}", process->high->awarePlayerTimer);
            process->high->awarePlayerTimer = 0.0f;
        }

        // A forceGoodbye (+0xB2) left by BGSProcedureForceGreet makes StartConversation set shutMenu, closing the
        // conversation next frame.
        if (auto* topicMgr = RE::MenuTopicManager::GetSingleton()) {
            topicMgr->forceGoodbye = false;
            topicMgr->shutMenu = false;
            topicMgr->closeInProgress = false;
        }

        // Not yet cleared if the last conversation just ended
        ClearPuppetAsSpeaker();
        Session::GetSingleton()->SetPuppetSpeakerPending(false);

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);

        RE::TESTopicInfo* forceGreetInfo = nullptr;
        if (a_forceGreetTopic && a_forceGreetTopic->numTopicInfos > 0 && a_forceGreetTopic->topicInfos) {
            // Evaluate conditions with self-dialogue active so the puppet tests as the donor
            // and the player tests as the Dragonborn
            Session::GetSingleton()->SetInSelfDialogue(true);
            for (std::uint32_t i = 0; i < a_forceGreetTopic->numTopicInfos; ++i) {
                auto* info = a_forceGreetTopic->topicInfos[i];
                if (!info) continue;
                if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) continue;
                if (QuestConditions::Evaluate(info->objConditions, a_forceGreetTopic->ownerQuest, a_speaker, player)) {
                    forceGreetInfo = info;
                    SKSE::log::info("Resolved ForceGreet topic {:08X} to info {:08X}",
                        a_forceGreetTopic->GetFormID(), info->GetFormID());
                    break;
                }
            }
        }

        if ((forceGreetInfo && a_speaker->SetDialogueWithPlayer(true, true, forceGreetInfo)) ||
            a_speaker->SetDialogueWithPlayer(true, false, nullptr) ||
            a_speaker->SetDialogueWithPlayer(true, true, nullptr)) {
            SKSE::log::info("Opened self-dialogue with puppet {:08X}", a_speaker->GetFormID());
            _awaitingMenuSince = GameClock::Now();
            return true;
        }

        SKSE::log::error("Failed to open dialogue with puppet {:08X}", a_speaker->GetFormID());
        Session::GetSingleton()->SetInSelfDialogue(false);
        return false;
    }

    void DialogueManager::EndSelfDialogue() {
        SKSE::log::info("Self-dialogue ended");
        Session::GetSingleton()->SetInSelfDialogue(false);
        // Leaving mid-line ends the player's line
        PlayerVoice::GetSingleton()->Finish();
        // The engine still needs the puppet as its (last) speaker while the menu closes: a Goodbye info's
        // end fragment runs then, and clearing it at once skipped the fragment. Update() clears it shortly after.
        Session::GetSingleton()->SetPuppetSpeakerPending(true);
        // Moves aliases the last frame's fragments put the puppet into to the player (see Update).
        Lifecycle::OnPuppetChangedInSelfDialogue();
        Lifecycle::OnSelfDialogueEnded();
    }

    bool DialogueManager::Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic) {
        auto& session = *Session::GetSingleton();
        if (a_this && session.IsActive()) {
            auto& self = *DialogueManager::GetSingleton();
            if (a_this == session.GetPuppet()) {
                SKSE::log::info("Puppet dialogue with player: flag={} forceGreet={}", a_flag, a_forceGreet);
                if (a_flag) {
                    session.SetInSelfDialogue(true);
                } else if (session.IsInSelfDialogue()) {
                    self.EndSelfDialogue();
                }
            } else if (a_flag) {
                session.SetInSelfDialogue(false);
            }
        }
        bool ret = _origSetDialogueWithPlayer(a_this, a_flag, a_forceGreet, a_topic);
        if (!a_flag && a_this && session.IsActive() && a_this == session.GetPuppet()) {
            session.SetPuppetSpeakerPending(true);
        }
        return ret;
    }

    void DialogueManager::AdvancePastUnvoicedLine() {
        // A puppet line with no voice file never sets canSkip again, so picking a topic did nothing (#37). Once its
        // text's duration has passed, request the advance the voice-finished callback would (#30).
        auto& session = *Session::GetSingleton();
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        auto* puppet = session.GetPuppet();
        auto* info = topicMgr ? topicMgr->currentTopicInfo : nullptr;
        const bool waiting = session.IsInSelfDialogue() && puppet && info && topicMgr->menuOpen &&
                             !topicMgr->canSkip && !topicMgr->advanceRequested && !topicMgr->advanceInProgress;
        if (!waiting || PlayerVoice::HasVoiceSound(puppet)) {
            _unvoicedLine.reset();
            return;
        }
        if (!_unvoicedLine || _unvoicedLine->info != info) {
            _unvoicedLine = UnvoicedLine{ info, GameClock::Now() };
            return;
        }
        if (_unvoicedLine->advanced) return;

        auto& voice = *PlayerVoice::GetSingleton();
        const float wait = std::max(voice.GetUnvoicedSeconds(), kUnvoicedLineMinSeconds);
        if (GameClock::SecondsSince(_unvoicedLine->since) < wait) return;

        SKSE::log::info("Puppet line {:08X} has no voice file; advancing after {:.1f}s", info->GetFormID(), wait);
        _unvoicedLine->advanced = true;
        voice.Finish();
        topicMgr->canSkip = true;
        topicMgr->advanceRequested = true;
    }

    void DialogueManager::UpdatePuppetSpeakerClear() {
        auto& session = *Session::GetSingleton();
        if (!session.IsPuppetSpeakerPending()) return;
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            _puppetSpeakerClearAt.reset();
            return;
        }
        if (!_puppetSpeakerClearAt) {
            _puppetSpeakerClearAt = GameClock::Now();
        }
        if (GameClock::SecondsSince(*_puppetSpeakerClearAt) < kPuppetSpeakerClearSeconds) return;
        session.SetPuppetSpeakerPending(false);
        _puppetSpeakerClearAt.reset();
        ClearPuppetAsSpeaker();
    }

    // --- Hooks ---

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        struct InUpdate {
            bool was = std::exchange(t_inUpdateInDialogue, true);
            ~InUpdate() { t_inUpdateInDialogue = was; }
        } inUpdate;
        auto& session = *Session::GetSingleton();
        if (!a_this || !session.IsActive()) {
            return _origCharUpdateInDialogue(a_this, a_response, a_unused);
        }
        auto& voice = *PlayerVoice::GetSingleton();

        if (!a_response) {
            // The engine ticks the speaker's line every frame. The puppet's voice may only just have started.
            bool ret = _origCharUpdateInDialogue(a_this, nullptr, a_unused);
            if (a_this == session.GetPuppet()) {
                PlayerVoice::SilenceStandIn(a_this);
            }
            return ret;
        }

        if (IsPuppetStandIn(a_this)) {
            // The puppet runs the engine's line unheard, so the engine paces the conversation; the player voices
            // its own copy (see PlayerVoice::TakeStandInLine)
            voice.TakeStandInLine(a_response);
            bool ret = _origCharUpdateInDialogue(a_this, a_response, a_unused);
            PlayerVoice::SilenceStandIn(a_this);
            voice.SpeakForStandIn(a_this, a_unused);
            return ret;
        }

        if (a_this == session.GetPuppet()) {
            // Outside self-dialogue the puppet is the silent Dragonborn
            SKSE::log::info("Suppressing puppet line outside self-dialogue: '{}'", a_response->text.c_str());
            return false;
        }

        return _origCharUpdateInDialogue(a_this, a_response, a_unused);
    }

    // The puppet's lines are voiced only for timing, so it doesn't gesture. Reads only Session.
    bool DialogueManager::Hook_TalkingIdle(RE::AIProcess* a_process, RE::Actor* a_actor, RE::DEFAULT_OBJECT a_action,
        RE::TESIdleForm* a_idle, bool a_arg5, bool a_arg6, RE::TESObjectREFR* a_target) {
        auto& session = *Session::GetSingleton();
        if (a_action == RE::DEFAULT_OBJECT::kActionTalkingIdle && a_actor && session.IsActive() && a_actor == session.GetPuppet()) {
            return false;
        }
        return _origTalkingIdle(a_process, a_actor, a_action, a_idle, a_arg5, a_arg6, a_target);
    }

    bool DialogueManager::Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& session = *Session::GetSingleton();
        if (a_this && a_response && session.IsActive()) {
            // In a menu conversation the player's own lines are never voiced
            auto* ui = RE::UI::GetSingleton();
            if (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
                SKSE::log::info("Not voicing player line in a conversation: '{}'", a_response->text.c_str());
                return _origPlayerUpdateInDialogue(a_this, nullptr, a_unused);
            }
            // A scene line for one of the donor's swapped aliases
            QueueSubtitle(RE::PlayerCharacter::GetSingleton(), a_response);
            PlayerVoice::GetSingleton()->Speak(a_response, a_unused);
            return true;
        }
        return _origPlayerUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool DialogueManager::Hook_Player_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this && Session::GetSingleton()->IsActive()) {
            return !PlayerVoice::GetSingleton()->IsPlaying();
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void DialogueManager::Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        // While the player voices a line, only PlayerVoice::Finish decides when it's done
        if (a_set && Session::GetSingleton()->IsActive() && PlayerVoice::GetSingleton()->IsPlaying()) {
            return;
        }
        _origPlayerSetSpeakingDone(a_this, a_set);
    }

}
