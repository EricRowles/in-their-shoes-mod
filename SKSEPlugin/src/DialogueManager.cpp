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
#include "RE/U/UserEvents.h"
#include "RE/C/ControlMap.h"

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

        // Shows the puppet's current self-dialogue line as its subtitle (#25). The engine queues no subtitle
        // for the puppet's silenced lines and never removes the first one, so it stayed on screen. Replacing
        // the entry's text isn't enough: SubtitleManager's update (ID 52637) only sends text to the HUD when
        // the front entry's speaker differs from currentSpeaker, which the engine resets when a line ends
        // (52627) but never does for the puppet. Clearing currentSpeaker makes the next update show it.
        void RefreshPuppetSubtitle(RE::Actor* a_puppet, const RE::DialogueResponse* a_response) {
            auto* subMgr = RE::SubtitleManager::GetSingleton();
            if (!subMgr || a_response->text.empty()) return;

            std::string_view text = a_response->text.c_str();
            auto handle = a_puppet->GetHandle();
            RE::BSSpinLockGuard locker(subMgr->lock);
            bool shown = false;
            for (std::uint32_t i = 0; i < subMgr->subtitles.size();) {
                auto& entry = subMgr->subtitles[i];
                if (entry.speaker == handle && (entry.subtitle.c_str() ? entry.subtitle.c_str() : "") != text) {
                    subMgr->subtitles.erase(subMgr->subtitles.begin() + i);
                    continue;
                }
                shown = shown || entry.speaker == handle;
                ++i;
            }
            if (shown) return;

            RE::SubtitleInfo info;
            info.speaker = handle;
            info.subtitle = a_response->text;
            info.targetDistance = 0.0f;
            info.forceDisplay = true;
            subMgr->subtitles.push_back(info);
            if (subMgr->currentSpeaker == handle) {
                subMgr->currentSpeaker = RE::ObjectRefHandle{};
            }
        }

        // Calls a_visit(eventID) for every user event the key is bound to, in any input context
        template <class Visit>
        void ForEachBinding(RE::INPUT_DEVICE a_device, std::uint32_t a_key, Visit a_visit) {
            const auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap || a_device >= RE::INPUT_DEVICE::kTotal) return;
            for (const auto* context : controlMap->controlMap) {
                if (!context) continue;
                for (const auto& mapping : context->deviceMappings[a_device]) {
                    if (mapping.inputKey == a_key) {
                        a_visit(mapping.eventID);
                    }
                }
            }
        }

        // Whether a press is one the Dialogue Menu uses to skip a line: a left click, or a key bound to
        // Click/Accept/Activate. While the menu is open, keyboard presses carry no user event name, and
        // control map mods bind Accept in different input contexts, so every context's bindings are checked.
        bool IsSkipPress(const RE::ButtonEvent* a_button) {
            auto device = a_button->GetDevice();
            auto key = a_button->GetIDCode();
            if (device == RE::INPUT_DEVICE::kMouse && key == 0) {
                return true;
            }

            const auto* userEvents = RE::UserEvents::GetSingleton();
            if (!userEvents) return false;
            auto isSkipEvent = [&](const RE::BSFixedString& a_event) {
                return a_event == userEvents->click || a_event == userEvents->accept || a_event == userEvents->activate;
            };
            if (isSkipEvent(a_button->QUserEvent())) {
                return true;
            }

            bool bound = false;
            ForEachBinding(device, key, [&](const RE::BSFixedString& a_event) { bound = bound || isSkipEvent(a_event); });
            return bound;
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
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink<RE::InputEvent*>(this);
        } else {
            SKSE::log::warn("DialogueManager failed to get BSInputDeviceManager singleton.");
        }
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            // First, so it can stop the engine's late end event for an info we already ended (EndPuppetInfo)
            // before SkyrimVM's sink runs its end fragment again
            events->GetEventSource<RE::TESTopicInfoEvent>()->PrependEventSink(this);
        }
        InstallHooks();
        auto& voice = PlayerVoice::GetSingleton();
        voice.SetEngineCalls({
            [](RE::Actor* a_actor, RE::DialogueResponse* a_response, bool a_unused) { _origCharUpdateInDialogue(a_actor, a_response, a_unused); },
            [](RE::Actor* a_actor, bool a_set) { _origPlayerSetSpeakingDone(a_actor, a_set); } });
        voice.SetStandInDoneHandler([this](RE::Actor* a_standIn, const RE::DialogueResponse* a_response) {
            OnStandInLineDone(a_standIn, a_response);
        });
        _initialized = true;
    }

    void DialogueManager::InstallHooks() {
        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[0] };
        _origSetDialogueWithPlayer = charVtbl.write_vfunc(0x41, &Hook_SetDialogueWithPlayer);
        _origCharUpdateInDialogue = charVtbl.write_vfunc(0x4C, &Hook_Character_UpdateInDialogue);
        _origCharQSpeakingDone = charVtbl.write_vfunc(0x107, &Hook_Character_QSpeakingDone);

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerUpdateInDialogue = playerVtbl.write_vfunc(0x4C, &Hook_Player_UpdateInDialogue);
        _origPlayerQSpeakingDone = playerVtbl.write_vfunc(0x107, &Hook_Player_QSpeakingDone);
        _origPlayerSetSpeakingDone = playerVtbl.write_vfunc(0x108, &Hook_Player_SetSpeakingDone);

        SKSE::log::info("Hooked Character (0x41, 0x4C, 0x107) and PlayerCharacter (0x4C, 0x107, 0x108) dialogue vfuncs");
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
        std::scoped_lock guard(_puppetInfoLock);
        _puppetInfosOpen.clear();
        _puppetInfosEndedByUs.clear();
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

    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        const RE::TESTopicInfoEvent* a_event,
        RE::BSTEventSource<RE::TESTopicInfoEvent>* /*a_eventSource*/) {
        auto& session = Session::GetSingleton();
        if (a_event && session.IsActive() && a_event->speakerRef && a_event->speakerRef.get() == session.GetPuppet()) {
            bool ended = a_event->type == RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd;
            const auto id = a_event->topicInfoFormID;
            std::scoped_lock guard(_puppetInfoLock);
            if (!ended) {
                _puppetInfosOpen.insert(id);
                _puppetInfosEndedByUs.erase(id);
            } else if (_puppetInfosEndedByUs.erase(id)) {
                SKSE::log::info("Puppet topic info {:08X} ended again; already ended, so its fragment doesn't run twice", id);
                return RE::BSEventNotifyControl::kStop;
            } else {
                _puppetInfosOpen.erase(id);
            }
            SKSE::log::info("Puppet topic info {:08X} {}", id, ended ? "ended" : "began");
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    // A skip press during a self-dialogue line skips it, as it would an NPC's line; that includes
    // clicking another topic. The engine's own skip handling never reaches the player's line.
    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event || !PlayerVoice::GetSingleton().IsSkippableStandInLine() || !Session::GetSingleton().IsInSelfDialogue()) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;
            const auto* button = static_cast<const RE::ButtonEvent*>(event);
            if (button->IsDown() && IsSkipPress(button)) {
                SKSE::log::info("Player line skipped");
                PlayerVoice::GetSingleton().Finish();
                break;
            }
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
        // Leaving mid-line ends the line, so the engine sees the puppet finish speaking
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

    // --- Stand-in speakers ---

    void DialogueManager::OnStandInLineDone(RE::Actor* a_standIn, const RE::DialogueResponse* a_response) {
        // In the Dialogue Menu the engine doesn't move to a topic's next response by itself here, and an
        // advance request replays the current response. So step the current response forward first;
        // the advance request then plays the next one.
        //
        // That includes stepping past the last response. MenuTopicManager's advance (ID 35285) speaks
        // currentResponse while it has one; once it is null it ends the info the engine's way: an Invisible
        // Continue info (TESTopicInfo +0x3C bit 6) plays its follow-up topic, a Goodbye info closes the menu,
        // and any other rebuilds and shows the topic list. Leaving the last response current made Invisible
        // Continue lines replay forever, and left a greeting with no topics whenever the engine hadn't
        // already moved past it by itself (#30).
        auto* ui = RE::UI::GetSingleton();
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (!topicMgr || !ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) return;

        if (topicMgr->isGreetingPlayer) {
            // The engine waits for the greeting to end before showing the topic list, and doesn't notice
            // a silenced greeting ending
            topicMgr->isGreetingPlayer = false;
            SKSE::log::info("Greeting finished; showing topics");
        }

        auto* dialogue = topicMgr->lastSelectedDialogue;
        auto* node = dialogue ? dialogue->currentResponse : nullptr;
        if (!node || node->item != a_response) {
            SKSE::log::info("Stand-in's line isn't the selected topic's current response; not advancing");
            return;
        }
        if (!node->next) {
            EndPuppetInfo(a_standIn, dialogue->parentTopicInfo);
        }
        dialogue->currentResponse = node->next;
        // MenuTopicManager's update (ID 35289) runs the advance only while canSkip (+0xBA) is set. Starting a
        // line clears it, and within MenuTopicManager only the advance itself sets it again; for a line the
        // engine voices, its voice-finished callback runs the advance directly. Our lines aren't the engine's,
        // so a greeting or Goodbye line that came after canSkip was cleared never advanced: no topics, or a menu
        // that didn't close (#30).
        topicMgr->canSkip = true;
        topicMgr->advanceRequested = true;
        SKSE::log::info("{}", node->next ? "Advancing to the topic's next response" : "Last response done; ending the info");
    }

    // The engine sends a topic info's end event (which runs its end fragment) for an info the player picked, but
    // not for one it moved to by itself (an Invisible Continue follow-up), and for the last info only when the
    // menu closes. So the end fragment of e.g. There Is No Umbra's hug line (which advances its quest) never ran
    // (#30). An info whose last response is done is ended here unless the engine already did; if the engine
    // sends its end later, ProcessEvent stops it so the fragment doesn't run twice.
    void DialogueManager::EndPuppetInfo(RE::Actor* a_puppet, RE::TESTopicInfo* a_info) {
        if (!a_puppet || !a_info) return;
        const auto id = a_info->GetFormID();
        {
            std::scoped_lock guard(_puppetInfoLock);
            if (!_puppetInfosOpen.contains(id)) return;
        }
        auto* events = RE::ScriptEventSourceHolder::GetSingleton();
        if (!events) return;
        RE::TESTopicInfoEvent event;
        event.speakerRef.reset(a_puppet);
        event.topicInfoFormID = id;
        event.type = RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd;
        event.stage = 0;
        events->SendEvent(&event);  // ProcessEvent sees it and marks the info closed
        std::scoped_lock guard(_puppetInfoLock);
        _puppetInfosEndedByUs.insert(id);
    }

    // --- Hooks ---

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& session = Session::GetSingleton();
        if (!a_this || !session.IsActive()) {
            return _origCharUpdateInDialogue(a_this, a_response, a_unused);
        }
        auto& voice = PlayerVoice::GetSingleton();

        if (!a_response) {
            // The engine ticks the speaker's line every frame; keep the stand-in "speaking" meanwhile
            bool ret = _origCharUpdateInDialogue(a_this, nullptr, a_unused);
            voice.KeepStandInSpeaking(a_this);
            return ret;
        }

        if (IsPuppetStandIn(a_this)) {
            // The player voices the line from its own copy; the puppet runs the engine's response silenced
            // (see PlayerVoice::TakeStandInLine)
            voice.TakeStandInLine(a_response);
            bool ret = _origCharUpdateInDialogue(a_this, a_response, a_unused);
            RefreshPuppetSubtitle(a_this, a_response);
            voice.SpeakForStandIn(a_this, a_response, a_unused);
            return ret;
        }

        if (a_this == session.GetPuppet()) {
            // Outside self-dialogue the puppet is the silent Dragonborn
            SKSE::log::info("Suppressing puppet line outside self-dialogue: '{}'", a_response->text.c_str());
            return false;
        }

        return _origCharUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool DialogueManager::Hook_Character_QSpeakingDone(const RE::Actor* a_this) {
        if (!IsPuppetStandIn(a_this)) {
            return _origCharQSpeakingDone(a_this);
        }
        // The stand-in is done when the player has finished voicing its line
        return !PlayerVoice::GetSingleton().IsPlaying();
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
