#include "PCH.h"
#include "DialogueManager.h"
#include "Lifecycle.h"
#include "GameUtil.h"
#include "Session.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "PlayerVoice.h"
#include "QuestConditions.h"

#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/T/TESCondition.h"
#include "RE/S/SubtitleManager.h"
#include "RE/A/AIProcess.h"
#include "RE/H/HighProcessData.h"

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
            auto* puppet = Session::GetSingleton().GetPuppet();
            if (!topicMgr || !puppet) return;
            auto handle = puppet->GetHandle();
            if (topicMgr->speaker == handle) {
                topicMgr->speaker.reset();
            }
            if (topicMgr->lastSpeaker == handle) {
                topicMgr->lastSpeaker.reset();
            }
        }

        // Finds the engine's call that plays a speaker's talking idle: where a line's voice starts, a call to
        // AIProcess::SetupSpecialIdle with kActionTalkingIdle (92), loaded by "lea r8d, [reg+0x5C]" just before
        // it. The game's code is searched for those bytes, so no per-version offset is needed. A call another
        // plugin has already redirected is recognised by the instructions before it (1.6.1170's), and our hook
        // chains to theirs. Returns 0 unless exactly one call matches.
        std::uintptr_t FindTalkingIdleCall() {
            constexpr std::uint8_t kArgSetup[] = { 0x44, 0x8D, 0x46, 0x5C, 0x49, 0x8B, 0xD6, 0x49, 0x8B, 0x8E, 0xF8, 0x00, 0x00, 0x00 };
            constexpr std::size_t kLookBack = 16;
            const auto text = REL::Module::get().segment(REL::Segment::textx);
            const auto setupSpecialIdle = REL::RelocationID(38290, 39256).address();
            const auto* code = reinterpret_cast<const std::uint8_t*>(text.address());

            std::uintptr_t direct = 0, redirected = 0;
            int directCount = 0, redirectedCount = 0;
            for (std::size_t i = kLookBack; i + 5 <= text.size(); ++i) {
                if (code[i] != 0xE8) continue;
                bool loadsAction = false;
                for (std::size_t back = kLookBack; back >= 3 && !loadsAction; --back) {
                    loadsAction = code[i - back] == 0x8D && code[i - back + 2] == 0x5C;
                }
                if (!loadsAction) continue;

                const auto at = text.address() + i;
                std::int32_t rel;
                std::memcpy(&rel, code + i + 1, sizeof(rel));
                if (at + 5 + rel == setupSpecialIdle) {
                    direct = at;
                    ++directCount;
                } else if (std::memcmp(code + i - sizeof(kArgSetup), kArgSetup, sizeof(kArgSetup)) == 0) {
                    redirected = at;
                    ++redirectedCount;
                }
            }
            if (directCount == 1) return direct;
            if (directCount == 0 && redirectedCount == 1) return redirected;
            SKSE::log::warn("Talking idle call search: {} direct, {} redirected by another plugin", directCount, redirectedCount);
            return 0;
        }

        // The puppet in self-dialogue speaks the donor's lines. (Lines scripts give the parked donor go to the
        // player when NativeRedirect is on: it sends their Say() there.)
        bool IsPuppetStandIn(const RE::Actor* a_actor) {
            auto& session = Session::GetSingleton();
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
        auto& voice = PlayerVoice::GetSingleton();
        voice.SetEngineCalls({
            [](RE::Actor* a_actor, RE::DialogueResponse* a_response, bool a_unused) { _origCharUpdateInDialogue(a_actor, a_response, a_unused); },
            [](RE::Actor* a_actor, bool a_set) { _origPlayerSetSpeakingDone(a_actor, a_set); } });
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

        if (auto call = FindTalkingIdleCall()) {
            SKSE::AllocTrampoline(14);
            _origTalkingIdle = SKSE::GetTrampoline().write_call<5>(call, &Hook_TalkingIdle);
            SKSE::log::info("Hooked the speaker's talking idle call");
        } else {
            SKSE::log::warn("Talking idle call not found; the puppet will gesture during self-dialogue");
        }
    }

    void DialogueManager::Update() {
        if (_pendingSelfDialogue.exchange(false, std::memory_order_acq_rel)) {
            auto* topic = _pendingSelfDialogueTopic.exchange(nullptr, std::memory_order_acq_rel);
            if (auto* puppet = Session::GetSingleton().GetPuppet()) {
                OpenSelfDialogueDirect(puppet, topic);
            }
        }
        PlayerVoice::GetSingleton().Update();
        if (Session::GetSingleton().IsPuppetSpeakingForDonor()) {
            // Topic fragments (run by the VM, a frame or more after their line) may put the speaker, the puppet,
            // into quest aliases. The next topics' conditions test the donor, so those are moved as they happen,
            // and until the puppet stops being the speaker: the last line's fragment runs after the menu closes.
            Lifecycle::OnPuppetChangedInSelfDialogue();
        }
        UpdatePuppetSpeakerClear();
        if (!PlayerVoice::GetSingleton().IsPlaying()) {
            if (auto item = IdleChatter::GetSingleton().Poll()) {
                PlayerVoice::GetSingleton().SpeakIdleChatter(std::move(item));
            }
        }
    }

    void DialogueManager::Clear() {
        Session::GetSingleton().SetInSelfDialogue(false);
        _pendingSelfDialogue.store(false, std::memory_order_release);
        _pendingSelfDialogueTopic.store(nullptr, std::memory_order_release);
        Session::GetSingleton().SetPuppetSpeakerPending(false);
        _puppetSpeakerClearAt.reset();
        PlayerVoice::GetSingleton().Clear();
    }

    // --- Self-dialogue ---

    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (a_event && !a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME && Session::GetSingleton().IsInSelfDialogue()) {
            EndSelfDialogue();
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    bool DialogueManager::OpenSelfDialogue(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic) {
        auto* puppet = Session::GetSingleton().GetPuppet();
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

        IdleAIManager::GetSingleton().StopIdleAI();
        PlayerVoice::GetSingleton().Finish();

        // The player's dialogue update ends the conversation while the player's high-process dialogue
        // timer (CommonLib's awarePlayerTimer, +0x340) is negative. Starting a line sets it to 30s only
        // when it is above 1s, and otherwise adds 30s. The timer keeps counting down while the player
        // isn't in dialogue, so after a while the first line left it negative and the conversation
        // closed at once.
        if (auto* process = player->GetActorRuntimeData().currentProcess; process && process->high &&
            process->high->awarePlayerTimer < 0.0f) {
            SKSE::log::info("Reset the player's dialogue timer from {:.1f}", process->high->awarePlayerTimer);
            process->high->awarePlayerTimer = 0.0f;
        }

        // BGSProcedureForceGreet sets MenuTopicManager::forceGoodbye (+0xB2) while approaching its target.
        // If forceGoodbye is still true when StartConversation runs, MenuTopicManager immediately sets
        // shutMenu (+0xB3 = true), which causes Character::UpdateInDialogue to close the conversation on
        // the very next frame.
        if (auto* topicMgr = RE::MenuTopicManager::GetSingleton()) {
            topicMgr->forceGoodbye = false;
            topicMgr->shutMenu = false;
            topicMgr->closeInProgress = false;
        }

        // Not yet cleared if the last conversation just ended
        ClearPuppetAsSpeaker();
        Session::GetSingleton().SetPuppetSpeakerPending(false);

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);

        RE::TESTopicInfo* forceGreetInfo = nullptr;
        if (a_forceGreetTopic && a_forceGreetTopic->numTopicInfos > 0 && a_forceGreetTopic->topicInfos) {
            // Evaluate conditions with self-dialogue active so the puppet tests as the donor
            // and the player tests as the Dragonborn
            Session::GetSingleton().SetInSelfDialogue(true);
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
            return true;
        }

        SKSE::log::error("Failed to open dialogue with puppet {:08X}", a_speaker->GetFormID());
        Session::GetSingleton().SetInSelfDialogue(false);
        return false;
    }

    void DialogueManager::EndSelfDialogue() {
        SKSE::log::info("Self-dialogue ended");
        Session::GetSingleton().SetInSelfDialogue(false);
        // Leaving mid-line ends the player's line
        PlayerVoice::GetSingleton().Finish();
        // The engine still needs the puppet as its (last) speaker while the menu closes: a Goodbye info's
        // end fragment runs then, and clearing it at once skipped the fragment. Update() clears it shortly after.
        Session::GetSingleton().SetPuppetSpeakerPending(true);
        // Quest aliases topic fragments put the speaker (the puppet) into (e.g. a follower alias) go to the
        // player standing in for the donor. Update() moves them every frame during self-dialogue; this catches
        // the last frame's.
        Lifecycle::OnPuppetChangedInSelfDialogue();
    }

    bool DialogueManager::Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic) {
        auto& session = Session::GetSingleton();
        if (a_this && session.IsActive()) {
            auto& self = DialogueManager::GetSingleton();
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

    void DialogueManager::UpdatePuppetSpeakerClear() {
        auto& session = Session::GetSingleton();
        if (!session.IsPuppetSpeakerPending()) return;
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            _puppetSpeakerClearAt.reset();
            return;
        }
        if (!_puppetSpeakerClearAt) {
            _puppetSpeakerClearAt = std::chrono::steady_clock::now();
        }
        if (SecondsSince(*_puppetSpeakerClearAt) < kPuppetSpeakerClearSeconds) return;
        session.SetPuppetSpeakerPending(false);
        _puppetSpeakerClearAt.reset();
        ClearPuppetAsSpeaker();
    }

    // --- Hooks ---

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& session = Session::GetSingleton();
        if (!a_this || !session.IsActive()) {
            return _origCharUpdateInDialogue(a_this, a_response, a_unused);
        }
        auto& voice = PlayerVoice::GetSingleton();

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

    // The engine plays a talking idle (gestures) on whoever starts a voiced line. The puppet's line is voiced
    // only so the engine can time it, so the puppet doesn't gesture. Runs wherever lines start: reads only
    // Session.
    bool DialogueManager::Hook_TalkingIdle(RE::AIProcess* a_process, RE::Actor* a_actor, RE::DEFAULT_OBJECT a_action,
        RE::TESIdleForm* a_idle, bool a_arg5, bool a_arg6, RE::TESObjectREFR* a_target) {
        auto& session = Session::GetSingleton();
        if (a_actor && session.IsActive() && a_actor == session.GetPuppet()) {
            return false;
        }
        return _origTalkingIdle(a_process, a_actor, a_action, a_idle, a_arg5, a_arg6, a_target);
    }

    bool DialogueManager::Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& session = Session::GetSingleton();
        if (a_this && a_response && session.IsActive()) {
            // In a menu conversation the player's own lines are never voiced
            auto* ui = RE::UI::GetSingleton();
            if (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
                SKSE::log::info("Not voicing player line in a conversation: '{}'", a_response->text.c_str());
                return _origPlayerUpdateInDialogue(a_this, nullptr, a_unused);
            }
            // A scene line for one of the donor's swapped aliases
            QueueSubtitle(RE::PlayerCharacter::GetSingleton(), a_response);
            PlayerVoice::GetSingleton().Speak(a_response, a_unused);
            return true;
        }
        return _origPlayerUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool DialogueManager::Hook_Player_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this && Session::GetSingleton().IsActive()) {
            return !PlayerVoice::GetSingleton().IsPlaying();
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void DialogueManager::Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        // While the player voices a line, only PlayerVoice::Finish decides when it's done
        if (a_set && Session::GetSingleton().IsActive() && PlayerVoice::GetSingleton().IsPlaying()) {
            return;
        }
        _origPlayerSetSpeakingDone(a_this, a_set);
    }

}
