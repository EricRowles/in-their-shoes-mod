#include "PCH.h"
#include "DialogueManager.h"
#include "ImpersonationManager.h"
#include "Forms.h"
#include "Session.h"
#include "IdleAIManager.h"
#include "IdleChatter.h"
#include "QuestConditions.h"

#include <algorithm>
#include <span>

#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/T/TESCondition.h"
#include "RE/S/SubtitleManager.h"
#include "RE/B/BSAudioManager.h"
#include "RE/I/ID.h"
#include "RE/A/AIProcess.h"
#include "RE/H/HighProcessData.h"
#include "RE/B/BSFaceGenAnimationData.h"
#include "RE/U/UserEvents.h"
#include "RE/C/ControlMap.h"

namespace InTheirShoes {

    // Unvoiced lines are shown for roughly this long per character of text
    constexpr float kUnvoicedSecondsPerChar = 0.065f;
    constexpr float kUnvoicedMinSeconds = 1.5f;
    // Failsafe so a sound handle that never reports finishing can't lock up a scene
    constexpr float kMaxLineSeconds = 30.0f;
    // A line's sound handle may not report as playing on the frame it starts
    constexpr float kLineStartGraceSeconds = 0.2f;
    // On a voice file's first play it is still loading, so the engine can't voice the line yet. Keep
    // asking it for this long before falling back to playing the file ourselves, without lip sync.
    constexpr float kEngineVoiceRetrySeconds = 0.5f;
    // A self-dialogue line that starts before the Dialogue Menu is open (the greeting) waits this long
    // for it: the engine won't voice the player's line until the menu is up
    constexpr float kMenuWaitSeconds = 1.0f;
    constexpr float kVoiceRetryIntervalSeconds = 0.1f;
    // A voiced line is done this long after its sound's duration has played
    constexpr float kSoundTailSeconds = 0.25f;
    // The puppet stays the engine's speaker this long after self-dialogue's menu closes (see EndSelfDialogue)
    constexpr float kPuppetSpeakerClearSeconds = 1.0f;

    static float SecondsSince(std::chrono::steady_clock::time_point a_time) {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_time).count();
    }

    static std::span<RE::BSSoundHandle> GetSoundHandles(RE::Actor* a_actor) {
        auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
        if (!process || !process->high) return {};
        return process->high->soundHandles;
    }

    // A stopped handle keeps reporting "playing" for a moment, so clear it as well
    static void StopSounds(RE::Actor* a_actor) {
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (handle.IsValid()) {
                handle.Stop();
            }
            handle = RE::BSSoundHandle();
        }
    }

    static bool IsSoundActive(RE::Actor* a_actor) {
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (!handle.IsValid()) continue;
            auto state = handle.state.get();
            if (handle.IsPlaying() || state == RE::BSSoundHandle::AssumedState::kPlaying ||
                state == RE::BSSoundHandle::AssumedState::kInitialized) {
                return true;
            }
        }
        return false;
    }

    // Length of the actor's voice sound in seconds, or 0 while unknown
    static float GetSoundSeconds(RE::Actor* a_actor) {
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (handle.IsValid() && handle.GetDuration() > 0) {
                return static_cast<float>(handle.GetDuration()) / 1000.0f;
            }
        }
        return 0.0f;
    }

    static bool HasVoice(const RE::DialogueResponse* a_response) {
        return a_response->voiceSound || !a_response->voice.empty();
    }

    static RE::NiAVObject* GetHeadNode(RE::Actor* a_actor) {
        auto* root = a_actor->Get3D();
        RE::NiAVObject* head = root ? root->GetObjectByName("NPC Head [Head]"sv) : nullptr;
        return head ? head : root;
    }

    // Makes the actor's playing sounds come from, and follow, its head
    static void AttachSoundsToHead(RE::Actor* a_actor) {
        auto* head = GetHeadNode(a_actor);
        if (!head) return;
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (handle.IsValid()) {
                handle.SetPosition(head->world.translate);
                handle.SetObjectToFollow(head);
            }
        }
    }

    static void ResetFace(RE::Actor* a_actor) {
        if (auto* faceData = a_actor->GetFaceGenAnimationData()) {
            RE::BSSpinLockGuard locker(faceData->lock);
            faceData->dialogueData = nullptr;
            faceData->Reset(0.0f, true, true, true, false);
            faceData->ClearExpressionOverride();
        }
        a_actor->SetEmotionType(RE::EmotionType::kNeutral);
        a_actor->SetEmotionValue(0);
    }

    static void QueueSubtitle(RE::Actor* a_speaker, const RE::DialogueResponse* a_response) {
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

    // Plays the response's voice file on the actor, for when the engine didn't start it
    // (see SpeakAsPlayer). It has no lip sync.
    static bool StartVoiceSound(RE::Actor* a_actor, const RE::DialogueResponse* a_response) {
        auto* process = a_actor->GetActorRuntimeData().currentProcess;
        auto* audioMgr = RE::BSAudioManager::GetSingleton();
        if (!process || !process->high || !audioMgr) return false;

        RE::BSSoundHandle handle;
        if (a_response->voiceSound) {
            audioMgr->GetSoundHandle(handle, a_response->voiceSound);
        } else if (!a_response->voice.empty()) {
            std::string path = a_response->voice.c_str();
            std::replace(path.begin(), path.end(), '/', '\\');
            if (path.size() >= 5 && _strnicmp(path.c_str(), "data\\", 5) == 0) {
                path = path.substr(5);
            }
            auto dot = path.rfind('.');
            std::string base = dot != std::string::npos ? path.substr(0, dot) : path;
            // Voice files ship as .fuz in the archives even though the response names the .wav
            for (const auto& candidate : { base + ".fuz", base + ".wav" }) {
                RE::BSResource::ID resID;
                resID.GenerateFromPath(candidate.c_str());
                audioMgr->GetSoundHandleByFile(handle, resID, 0x10, 64);
                if (handle.IsValid()) break;
            }
        }

        if (!handle.IsValid()) {
            return false;
        }
        if (auto* head = GetHeadNode(a_actor)) {
            handle.SetPosition(head->world.translate);
            handle.SetObjectToFollow(head);
        }
        handle.SetVolume(1.0f);
        handle.Play();
        process->high->soundHandles[0] = handle;
        return true;
    }

    // The engine reads these to know whether a speaker's line is still playing
    static void SetLinePlaying(RE::Actor* a_actor, bool a_playing) {
        auto& bits = a_actor->GetActorRuntimeData().boolBits;
        if (a_playing) {
            bits.reset(RE::Actor::BOOL_BITS::kSpeakingDone, RE::Actor::BOOL_BITS::kSoundFileDone, RE::Actor::BOOL_BITS::kVoiceFileDone);
        } else {
            bits.set(RE::Actor::BOOL_BITS::kSpeakingDone, RE::Actor::BOOL_BITS::kSoundFileDone, RE::Actor::BOOL_BITS::kVoiceFileDone);
        }
    }

    static void CopyResponse(const RE::DialogueResponse* a_from, RE::DialogueResponse& a_to) {
        a_to.text = a_from->text;
        a_to.animFaceArchType = a_from->animFaceArchType;
        a_to.percent = a_from->percent;
        a_to.voice = a_from->voice;
        a_to.voiceSound = a_from->voiceSound;
        a_to.soundLip = a_from->soundLip;
        a_to.speakerIdle = a_from->speakerIdle;
        a_to.listenIdle = a_from->listenIdle;
        a_to.useEmotion = a_from->useEmotion;
    }

    // Leaves the response's text (for subtitles and timing) but nothing to hear or mouth
    static void Silence(RE::DialogueResponse* a_response) {
        a_response->voice = ""sv;
        a_response->voiceSound = nullptr;
        a_response->soundLip = false;
        a_response->useEmotion = false;
        a_response->animFaceArchType = RE::EmotionType::kNeutral;
        a_response->percent = 0;
        a_response->speakerIdle = nullptr;
        a_response->listenIdle = nullptr;
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
            events->AddEventSink<RE::TESTopicInfoEvent>(this);
        }
        InstallHooks();
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
        if (_line) {
            TickPlayerLine();
        }
        UpdatePuppetSpeakerClear();
        if (Session::GetSingleton().IsInSelfDialogue()) {
            // Topic fragments (run by the VM, a frame or more after their line) change the speaker, the
            // puppet. The next topics' conditions test the donor, so the changes are forwarded as they happen.
            auto& impMgr = ImpersonationManager::GetSingleton();
            impMgr.ForwardPuppetFactionsToDonor();
            impMgr.AdoptPuppetAliases();
        }
        if (!_line) {
            if (auto item = IdleChatter::GetSingleton().Poll()) {
                SpeakAsPlayer(item->responses.front(), false, true);
                _line->item = std::move(item);  // the response must outlive the line
            }
        }
    }

    void DialogueManager::Clear() {
        Session::GetSingleton().SetInSelfDialogue(false);
        _pendingSelfDialogue.store(false, std::memory_order_release);
        _pendingSelfDialogueTopic.store(nullptr, std::memory_order_release);
        _puppetSpeakerClearPending.store(false, std::memory_order_release);
        _puppetSpeakerClearAt = {};
        _line.reset();
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
            SKSE::log::info("Puppet topic info {:08X} {}", a_event->topicInfoFormID, ended ? "ended" : "began");
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    // Calls a_visit(eventID) for every user event the key is bound to, in any input context
    template <class Visit>
    static void ForEachBinding(RE::INPUT_DEVICE a_device, std::uint32_t a_key, Visit a_visit) {
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
    static bool IsSkipPress(const RE::ButtonEvent* a_button) {
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

    // A skip press during a self-dialogue line skips it, as it would an NPC's line; that includes
    // clicking another topic. The engine's own skip handling never reaches the player's line.
    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event || !_line || !_line->standIn || !Session::GetSingleton().IsInSelfDialogue() ||
            SecondsSince(_line->start) < kLineStartGraceSeconds) {
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
                FinishPlayerLine();
                break;
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    static void ClearPuppetAsSpeaker();

    bool DialogueManager::OpenSelfDialogue(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic) {
        auto* puppet = Session::GetSingleton().GetPuppet();
        if (!a_speaker) {
            a_speaker = puppet;
        }
        if (!a_speaker || a_speaker != puppet) {
            SKSE::log::warn("OpenSelfDialogue: speaker must be the puppet");
            return false;
        }

        // Queue for DialogueManager::Update() on the main thread rather than SKSE's task interface,
        // which dispatches on worker threads concurrently with the main loop.
        _pendingSelfDialogueTopic.store(a_forceGreetTopic, std::memory_order_release);
        _pendingSelfDialogue.store(true, std::memory_order_release);
        return true;
    }

    bool DialogueManager::OpenSelfDialogueDirect(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!player || (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsModalMenuOpen()))) {
            SKSE::log::warn("OpenSelfDialogueDirect: Menu or pause state blocking dialogue initiation");
            return false;
        }

        if (IdleAIManager::GetSingleton().IsAIActive()) {
            IdleAIManager::GetSingleton().StopIdleAI();
        }
        if (_line) {
            FinishPlayerLine();
        }

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
        _puppetSpeakerClearPending.store(false, std::memory_order_release);

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
                    SKSE::log::info("OpenSelfDialogueDirect: resolved ForceGreet topic {:08X} -> info {:08X}",
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

    // The engine leaves the puppet as MenuTopicManager's speaker after self-dialogue, which blocks
    // activating it again
    static void ClearPuppetAsSpeaker() {
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

    void DialogueManager::EndSelfDialogue() {
        SKSE::log::info("Self-dialogue ended");
        Session::GetSingleton().SetInSelfDialogue(false);
        // Leaving mid-line ends the line, so the engine sees the puppet finish speaking
        if (_line) {
            FinishPlayerLine();
        }
        // The engine still needs the puppet as its (last) speaker while the menu closes: a Goodbye info's
        // end fragment runs then, and clearing it at once skipped the fragment. Update() clears it shortly after.
        _puppetSpeakerClearPending.store(true, std::memory_order_release);
        // What topic fragments did to the speaker (the puppet) belongs to the donor: factions go to the
        // donor, and quest aliases (e.g. a follower alias) go to the player standing in for the donor.
        // Update() forwards them every frame during self-dialogue; this catches the last frame's.
        auto& impMgr = ImpersonationManager::GetSingleton();
        impMgr.ForwardPuppetFactionsToDonor();
        impMgr.AdoptPuppetAliases();
    }

    bool DialogueManager::Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic) {
        auto& session = Session::GetSingleton();
        if (a_this && session.IsActive()) {
            auto& self = DialogueManager::GetSingleton();
            if (a_this == session.GetPuppet()) {
                SKSE::log::info("Hook_SetDialogueWithPlayer: puppet flag={} forceGreet={}", a_flag, a_forceGreet);
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
            GetSingleton()._puppetSpeakerClearPending.store(true, std::memory_order_release);
        }
        return ret;
    }

    void DialogueManager::UpdatePuppetSpeakerClear() {
        if (!_puppetSpeakerClearPending.load(std::memory_order_acquire)) return;
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            _puppetSpeakerClearAt = {};
            return;
        }
        if (_puppetSpeakerClearAt == std::chrono::steady_clock::time_point{}) {
            _puppetSpeakerClearAt = std::chrono::steady_clock::now();
        }
        if (SecondsSince(_puppetSpeakerClearAt) < kPuppetSpeakerClearSeconds) return;
        _puppetSpeakerClearPending.store(false, std::memory_order_release);
        _puppetSpeakerClearAt = {};
        ClearPuppetAsSpeaker();
    }

    // --- Player voice ---

    void DialogueManager::SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        StopSounds(player);

        PlayerLine line;
        if (_line) {
            // A line that replaces another keeps the other's stand-in held until this one ends
            line.standIn = _line->standIn;
            line.standInResponse = _line->standInResponse;
        }

        if (a_response->useEmotion) {
            player->SetEmotionType(a_response->animFaceArchType.get());
            player->SetEmotionValue(a_response->percent);
        }

        auto& runtime = player->GetActorRuntimeData();
        runtime.boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
        runtime.boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
        runtime.boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
        runtime.voiceTimer = 0.0f;
        _origPlayerSetSpeakingDone(player, false);

        // PlayerCharacter's own UpdateInDialogue doesn't run lines; the Character version does, and voices
        // the line itself (with lip sync) when the voice file is loaded. TickPlayerLine retries it while
        // the file loads, and falls back to playing the file without lip sync.
        auto* ui = RE::UI::GetSingleton();
        line.waitsForMenu = Session::GetSingleton().IsInSelfDialogue() && !(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME));
        if (!line.waitsForMenu) {
            _origCharUpdateInDialogue(player, a_response, a_unused);
            runtime.soundCallBackSet = 1;
        }

        line.start = std::chrono::steady_clock::now();
        line.hadSound = IsSoundActive(player);
        line.soundStart = line.start;
        line.pendingVoice = (!line.hadSound && HasVoice(a_response)) ? a_response : nullptr;
        if (line.hadSound) {
            AttachSoundsToHead(player);
        }
        SKSE::log::info("Player line: '{}' (engine voiced={}, idleChatter={})",
            a_response->text.c_str(), line.hadSound, a_isIdleChatter);

        float textLen = static_cast<float>(a_response->text.length());
        line.unvoicedDuration = std::max(kUnvoicedMinSeconds, textLen * kUnvoicedSecondsPerChar);
        line.isIdleChatter = a_isIdleChatter;
        _line = std::move(line);
    }

    void DialogueManager::StartPlayerVoiceLate() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto& line = *_line;
        if (line.waitsForMenu) {
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
            if (!menuOpen && SecondsSince(line.start) < kMenuWaitSeconds) return;
            line.waitsForMenu = false;
            line.start = std::chrono::steady_clock::now();  // the line starts now
            player->GetActorRuntimeData().soundCallBackSet = 1;
        }

        float elapsed = SecondsSince(line.start);

        if (elapsed < kEngineVoiceRetrySeconds) {
            if (SecondsSince(line.lastVoiceRetry) < kVoiceRetryIntervalSeconds) return;
            line.lastVoiceRetry = std::chrono::steady_clock::now();
            _origCharUpdateInDialogue(player, line.pendingVoice, false);
            if (!IsSoundActive(player)) return;
            SKSE::log::info("Engine voiced the player line on retry after {:.2f}s", elapsed);
        } else if (StartVoiceSound(player, line.pendingVoice)) {
            SKSE::log::info("Engine didn't voice the player line; playing the voice file without lip sync");
        } else {
            SKSE::log::warn("No voice sound could be started for '{}'", line.pendingVoice->voice.c_str());
            line.pendingVoice = nullptr;
            return;
        }

        AttachSoundsToHead(player);
        line.pendingVoice = nullptr;
        line.hadSound = true;
        line.soundStart = std::chrono::steady_clock::now();
    }

    void DialogueManager::TickPlayerLine() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto& line = *_line;

        // The player's line isn't ticked while the Dialogue Menu is open; doing so stalls its lip sync
        auto* ui = RE::UI::GetSingleton();
        if (!(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
            _origCharUpdateInDialogue(player, nullptr, false);
        }

        if (line.pendingVoice) {
            StartPlayerVoiceLate();
        }

        if (line.hadSound) {
            // Timed from the sound's duration: an engine-started sound keeps reporting "playing" while the
            // Dialogue Menu is open, so its state can't be relied on
            if (line.soundSeconds <= 0.0f) {
                line.soundSeconds = GetSoundSeconds(player);
            }
            float playing = SecondsSince(line.soundStart);
            bool done = line.soundSeconds > 0.0f ? playing >= line.soundSeconds + kSoundTailSeconds
                                                 : playing >= kLineStartGraceSeconds && !IsSoundActive(player);
            if (!done) {
                if (playing < kMaxLineSeconds) return;
                SKSE::log::warn("Player line still playing after {:.0f}s; finishing it", playing);
            }
        } else if (line.pendingVoice || SecondsSince(line.start) < line.unvoicedDuration) {
            return;
        }

        SKSE::log::info("Player line finished after {:.2f}s (voiced={}, sound length {:.2f}s)",
            SecondsSince(line.start), line.hadSound, line.soundSeconds);
        FinishPlayerLine();
    }

    void DialogueManager::FinishPlayerLine() {
        if (!_line) return;
        // Dropped first: the speaking hooks must see that no line is playing
        const auto line = std::move(*_line);
        _line.reset();

        auto* player = RE::PlayerCharacter::GetSingleton();
        ReleaseStandIn(line.standIn, line.standInResponse);
        StopSounds(player);
        player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
        player->GetActorRuntimeData().voiceTimer = 0.0f;
        _origPlayerSetSpeakingDone(player, true);
        ResetFace(player);

        if (line.isIdleChatter) {
            IdleChatter::GetSingleton().OnLineFinished();
        }
    }

    // --- Stand-in speakers ---

    void DialogueManager::HoldStandIn(RE::Actor* a_standIn, RE::DialogueResponse* a_response) {
        // The stand-in's silenced line would finish at once; the engine has to see it still speaking until
        // the player's line ends
        _line->standIn = a_standIn->GetHandle();
        _line->standInResponse = a_response;
        SetLinePlaying(a_standIn, true);
        a_standIn->GetActorRuntimeData().voiceTimer = 0.0f;
    }

    void DialogueManager::ReleaseStandIn(RE::ActorHandle a_standIn, const RE::DialogueResponse* a_response) {
        auto standIn = a_standIn.get();
        if (!standIn) return;

        SetLinePlaying(standIn.get(), false);
        standIn->GetActorRuntimeData().voiceTimer = 0.0f;
        standIn->SetSpeakingDone(true);

        // In the Dialogue Menu the engine doesn't move to a topic's next response by itself here, and an
        // advance request replays the current response. So step the current response forward first;
        // the advance request then plays the next one.
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
        if (node->next) {
            dialogue->currentResponse = node->next;
            topicMgr->advanceRequested = true;
            SKSE::log::info("Advancing to the topic's next response");
        }
    }

    // --- Hooks ---

    static bool IsDonorStandIn(const RE::Actor* a_actor) {
        auto& session = Session::GetSingleton();
        return a_actor && session.IsActive() &&
               ((a_actor == session.GetPuppet() && session.IsInSelfDialogue()) ||
                   a_actor == session.GetDonor());
    }

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& session = Session::GetSingleton();
        if (!a_this || !session.IsActive()) {
            return _origCharUpdateInDialogue(a_this, a_response, a_unused);
        }
        auto& self = DialogueManager::GetSingleton();

        if (!a_response) {
            // The engine ticks the speaker's line every frame; keep the stand-in "speaking" meanwhile
            bool ret = _origCharUpdateInDialogue(a_this, nullptr, a_unused);
            if (self._line && self._line->standIn.get().get() == a_this) {
                SetLinePlaying(a_this, true);
            }
            return ret;
        }

        if (IsDonorStandIn(a_this)) {
            // The player voices the line from its own copy. The stand-in (the puppet in self-dialogue, or
            // the hidden donor) still runs the engine's response object, silenced: the engine tracks which
            // response the speaker is on by that object, and only moves to the next one if it matches.
            // The response belongs to this one playing of the topic, so silencing it is safe.
            if (a_this != session.GetPuppet()) {
                // The donor is out of sight, so its subtitle is shown for the player
                QueueSubtitle(RE::PlayerCharacter::GetSingleton(), a_response);
            }
            CopyResponse(a_response, self._standInLine);
            Silence(a_response);
            bool ret = _origCharUpdateInDialogue(a_this, a_response, a_unused);
            self.SpeakAsPlayer(&self._standInLine, a_unused, false);
            self.HoldStandIn(a_this, a_response);
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
        if (!IsDonorStandIn(a_this)) {
            return _origCharQSpeakingDone(a_this);
        }
        // The stand-in is done when the player has finished voicing its line
        return !DialogueManager::GetSingleton()._line;
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
            auto& self = DialogueManager::GetSingleton();
            QueueSubtitle(RE::PlayerCharacter::GetSingleton(), a_response);
            self.SpeakAsPlayer(a_response, a_unused, false);
            return true;
        }
        return _origPlayerUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool DialogueManager::Hook_Player_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this && Session::GetSingleton().IsActive()) {
            return !DialogueManager::GetSingleton()._line;
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void DialogueManager::Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        // While the player voices a line, only FinishPlayerLine decides when it's done
        if (a_set && Session::GetSingleton().IsActive() && DialogueManager::GetSingleton()._line) {
            return;
        }
        _origPlayerSetSpeakingDone(a_this, a_set);
    }

}
