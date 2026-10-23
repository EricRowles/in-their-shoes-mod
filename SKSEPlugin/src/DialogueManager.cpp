#include "PCH.h"
#include "DialogueManager.h"
#include "ImpersonationManager.h"
#include "IdleAIManager.h"

#include <random>
#include <algorithm>
#include <span>

#include "RE/T/TESDataHandler.h"
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
        if (_playerLineActive) {
            TickPlayerLine();
        }
        UpdateIdleChatter();
    }

    void DialogueManager::Clear() {
        _inSelfDialogue.store(false, std::memory_order_release);
        _playerLineActive = false;
        _playerLineIsIdleChatter = false;
        _idleTopicsCached = false;
        _idleTopics.clear();
        _playerLineResponse = nullptr;
        _playerLineItem.reset();
        _lastIdleEvalTime = {};
        _lastIdleChatterTime = std::chrono::steady_clock::now();
    }

    // --- Self-dialogue ---

    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (a_event && !a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME && IsInSelfDialogue()) {
            EndSelfDialogue();
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    // A click (or accept/activate) during a self-dialogue line skips it, as it would an NPC's line; that
    // includes clicking another topic. The engine's own skip handling never reaches the player's line.
    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event || !_playerLineActive || !_standIn || !IsInSelfDialogue() ||
            SecondsSince(_playerLineStart) < kLineStartGraceSeconds) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto* userEvents = RE::UserEvents::GetSingleton();
        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;
            const auto* button = static_cast<const RE::ButtonEvent*>(event);
            if (!button->IsDown()) continue;

            const auto& userEvent = button->QUserEvent();
            bool isLeftMouse = button->GetDevice() == RE::INPUT_DEVICE::kMouse && button->GetIDCode() == 0;
            if (isLeftMouse || (userEvents && (userEvent == userEvents->click || userEvent == userEvents->accept ||
                                                  userEvent == userEvents->activate))) {
                SKSE::log::info("Player line skipped");
                FinishPlayerLine();
                break;
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    bool DialogueManager::OpenSelfDialogue(RE::Actor* a_speaker) {
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        if (!a_speaker) {
            a_speaker = puppet;
        }
        if (!a_speaker || a_speaker != puppet) {
            SKSE::log::warn("OpenSelfDialogue: speaker must be the puppet");
            return false;
        }

        SKSE::GetTaskInterface()->AddTask([speakerHandle = a_speaker->GetHandle()]() {
            if (auto speaker = speakerHandle.get()) {
                DialogueManager::GetSingleton().OpenSelfDialogueDirect(speaker.get());
            }
        });
        return true;
    }

    bool DialogueManager::OpenSelfDialogueDirect(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!player || (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsModalMenuOpen()))) {
            SKSE::log::warn("OpenSelfDialogueDirect: Menu or pause state blocking dialogue initiation");
            return false;
        }

        if (IdleAIManager::GetSingleton().IsAIActive()) {
            IdleAIManager::GetSingleton().StopIdleAI();
        }
        if (_playerLineActive) {
            FinishPlayerLine();
        }

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);

        if (a_speaker->SetDialogueWithPlayer(true, false, nullptr) || a_speaker->SetDialogueWithPlayer(true, true, nullptr)) {
            SKSE::log::info("Opened self-dialogue with puppet {:08X}", a_speaker->GetFormID());
            return true;
        }

        SKSE::log::error("Failed to open dialogue with puppet {:08X}", a_speaker->GetFormID());
        _inSelfDialogue.store(false, std::memory_order_release);
        return false;
    }

    // The engine leaves the puppet as MenuTopicManager's speaker after self-dialogue, which blocks
    // activating it again
    static void ClearPuppetAsSpeaker() {
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
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
        _inSelfDialogue.store(false, std::memory_order_release);
        // Leaving mid-line ends the line, so the engine sees the puppet finish speaking
        if (_playerLineActive) {
            FinishPlayerLine();
        }
        ClearPuppetAsSpeaker();
        // What topic fragments did to the speaker (the puppet) belongs to the donor: factions go to the
        // donor, and quest aliases (e.g. a follower alias) go to the player standing in for the donor
        auto& impMgr = ImpersonationManager::GetSingleton();
        impMgr.ForwardPuppetFactionsToDonor();
        impMgr.AdoptPuppetAliases();
    }

    bool DialogueManager::Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic) {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (a_this && impMgr.IsActive()) {
            auto& self = DialogueManager::GetSingleton();
            if (a_this == impMgr.GetPuppet()) {
                SKSE::log::info("Hook_SetDialogueWithPlayer: puppet flag={} forceGreet={}", a_flag, a_forceGreet);
                if (a_flag) {
                    self._inSelfDialogue.store(true, std::memory_order_release);
                } else if (self.IsInSelfDialogue()) {
                    self.EndSelfDialogue();
                }
            } else if (a_flag) {
                self._inSelfDialogue.store(false, std::memory_order_release);
            }
        }
        bool ret = _origSetDialogueWithPlayer(a_this, a_flag, a_forceGreet, a_topic);
        if (!a_flag && a_this && impMgr.IsActive() && a_this == impMgr.GetPuppet()) {
            ClearPuppetAsSpeaker();
        }
        return ret;
    }

    // --- Player voice ---

    void DialogueManager::SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        StopSounds(player);
        _playerLineItem.reset();

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
        _playerLineWaitsForMenu = IsInSelfDialogue() && !(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME));
        if (!_playerLineWaitsForMenu) {
            _origCharUpdateInDialogue(player, a_response, a_unused);
            runtime.soundCallBackSet = 1;
        }

        _playerLineStart = std::chrono::steady_clock::now();
        _playerLineHadSound = IsSoundActive(player);
        _playerLineSoundStart = _playerLineStart;
        _playerLineSoundSeconds = 0.0f;
        _playerLineResponse = (!_playerLineHadSound && HasVoice(a_response)) ? a_response : nullptr;
        if (_playerLineHadSound) {
            AttachSoundsToHead(player);
        }
        SKSE::log::info("Player line: '{}' (engine voiced={}, idleChatter={})",
            a_response->text.c_str(), _playerLineHadSound, a_isIdleChatter);

        float textLen = static_cast<float>(a_response->text.length());
        _playerLineUnvoicedDuration = std::max(kUnvoicedMinSeconds, textLen * kUnvoicedSecondsPerChar);
        _playerLineIsIdleChatter = a_isIdleChatter;
        _playerLineActive = true;
    }

    void DialogueManager::StartPlayerVoiceLate() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (_playerLineWaitsForMenu) {
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
            if (!menuOpen && SecondsSince(_playerLineStart) < kMenuWaitSeconds) return;
            _playerLineWaitsForMenu = false;
            _playerLineStart = std::chrono::steady_clock::now();  // the line starts now
            player->GetActorRuntimeData().soundCallBackSet = 1;
        }

        float elapsed = SecondsSince(_playerLineStart);

        if (elapsed < kEngineVoiceRetrySeconds) {
            if (SecondsSince(_lastVoiceRetry) < kVoiceRetryIntervalSeconds) return;
            _lastVoiceRetry = std::chrono::steady_clock::now();
            _origCharUpdateInDialogue(player, _playerLineResponse, false);
            if (!IsSoundActive(player)) return;
            SKSE::log::info("Engine voiced the player line on retry after {:.2f}s", elapsed);
        } else if (StartVoiceSound(player, _playerLineResponse)) {
            SKSE::log::info("Engine didn't voice the player line; playing the voice file without lip sync");
        } else {
            SKSE::log::warn("No voice sound could be started for '{}'", _playerLineResponse->voice.c_str());
            _playerLineResponse = nullptr;
            return;
        }

        AttachSoundsToHead(player);
        _playerLineResponse = nullptr;
        _playerLineHadSound = true;
        _playerLineSoundStart = std::chrono::steady_clock::now();
    }

    void DialogueManager::TickPlayerLine() {
        auto* player = RE::PlayerCharacter::GetSingleton();

        // The player's line isn't ticked while the Dialogue Menu is open; doing so stalls its lip sync
        auto* ui = RE::UI::GetSingleton();
        if (!(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
            _origCharUpdateInDialogue(player, nullptr, false);
        }

        if (_playerLineResponse) {
            StartPlayerVoiceLate();
        }

        if (_playerLineHadSound) {
            // Timed from the sound's duration: an engine-started sound keeps reporting "playing" while the
            // Dialogue Menu is open, so its state can't be relied on
            if (_playerLineSoundSeconds <= 0.0f) {
                _playerLineSoundSeconds = GetSoundSeconds(player);
            }
            float playing = SecondsSince(_playerLineSoundStart);
            bool done = _playerLineSoundSeconds > 0.0f ? playing >= _playerLineSoundSeconds + kSoundTailSeconds
                                                       : playing >= kLineStartGraceSeconds && !IsSoundActive(player);
            if (!done) {
                if (playing < kMaxLineSeconds) return;
                SKSE::log::warn("Player line still playing after {:.0f}s; finishing it", playing);
            }
        } else if (_playerLineResponse || SecondsSince(_playerLineStart) < _playerLineUnvoicedDuration) {
            return;
        }

        SKSE::log::info("Player line finished after {:.2f}s (voiced={}, sound length {:.2f}s)",
            SecondsSince(_playerLineStart), _playerLineHadSound, _playerLineSoundSeconds);
        FinishPlayerLine();
    }

    void DialogueManager::FinishPlayerLine() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        _playerLineActive = false;
        _playerLineResponse = nullptr;
        _playerLineItem.reset();
        ReleaseStandIn();
        StopSounds(player);
        player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
        player->GetActorRuntimeData().voiceTimer = 0.0f;
        _origPlayerSetSpeakingDone(player, true);
        ResetFace(player);

        if (_playerLineIsIdleChatter) {
            _playerLineIsIdleChatter = false;
            _lastIdleChatterTime = std::chrono::steady_clock::now();
        }
    }

    // --- Stand-in speakers ---

    void DialogueManager::HoldStandIn(RE::Actor* a_standIn) {
        // The stand-in's silenced line would finish at once; the engine has to see it still speaking until
        // the player's line ends
        _standIn = a_standIn->GetHandle();
        SetLinePlaying(a_standIn, true);
        a_standIn->GetActorRuntimeData().voiceTimer = 0.0f;
    }

    void DialogueManager::ReleaseStandIn() {
        auto standIn = _standIn.get();
        auto* response = _standInResponse;
        _standIn.reset();
        _standInResponse = nullptr;
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
        if (!node || node->item != response) {
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
        auto& impMgr = ImpersonationManager::GetSingleton();
        return a_actor && impMgr.IsActive() &&
               ((a_actor == impMgr.GetPuppet() && DialogueManager::GetSingleton().IsInSelfDialogue()) ||
                   a_actor == impMgr.GetDonor());
    }

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!a_this || !impMgr.IsActive()) {
            return _origCharUpdateInDialogue(a_this, a_response, a_unused);
        }
        auto& self = DialogueManager::GetSingleton();

        if (!a_response) {
            // The engine ticks the speaker's line every frame; keep the stand-in "speaking" meanwhile
            bool ret = _origCharUpdateInDialogue(a_this, nullptr, a_unused);
            if (self._playerLineActive && self._standIn.get().get() == a_this) {
                SetLinePlaying(a_this, true);
            }
            return ret;
        }

        if (IsDonorStandIn(a_this)) {
            // The player voices the line from its own copy. The stand-in (the puppet in self-dialogue, or
            // the hidden donor) still runs the engine's response object, silenced: the engine tracks which
            // response the speaker is on by that object, and only moves to the next one if it matches.
            // The response belongs to this one playing of the topic, so silencing it is safe.
            if (a_this != impMgr.GetPuppet()) {
                // The donor is out of sight, so its subtitle is shown for the player
                QueueSubtitle(RE::PlayerCharacter::GetSingleton(), a_response);
            }
            CopyResponse(a_response, self._standInLine);
            self._standInResponse = a_response;
            Silence(a_response);
            bool ret = _origCharUpdateInDialogue(a_this, a_response, a_unused);
            self.SpeakAsPlayer(&self._standInLine, a_unused, false);
            self.HoldStandIn(a_this);
            return ret;
        }

        if (a_this == impMgr.GetPuppet()) {
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
        return !DialogueManager::GetSingleton()._playerLineActive;
    }

    bool DialogueManager::Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (a_this && a_response && impMgr.IsActive()) {
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
        if (a_this && ImpersonationManager::GetSingleton().IsActive()) {
            return !DialogueManager::GetSingleton()._playerLineActive;
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void DialogueManager::Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        // While the player voices a line, only FinishPlayerLine decides when it's done
        if (a_set && ImpersonationManager::GetSingleton().IsActive() && DialogueManager::GetSingleton()._playerLineActive) {
            return;
        }
        _origPlayerSetSpeakingDone(a_this, a_set);
    }

    // --- Idle chatter ---

    static bool IsIdleTopic(const RE::TESTopic* a_topic) {
        return a_topic->data.subtype.get() == RE::DIALOGUE_DATA::Subtype::kIdle;
    }

    // Evaluates a condition list in the context of its quest, so conditions whose parameters or
    // run-on target are quest aliases resolve. (TESCondition::IsTrue(action, target) passes no quest,
    // and the engine then hands the raw alias index to the condition function as a pointer.)
    // Skyrim groups OR'd items first: A AND B OR C AND D == A AND (B OR C) AND D.
    static bool EvaluateInQuest(const RE::TESCondition& a_conditions, RE::TESQuest* a_quest,
        RE::TESObjectREFR* a_subject, RE::TESObjectREFR* a_target)
    {
        RE::ConditionCheckParams params(a_subject, a_target);
        params.quest = a_quest;

        bool groupResult = false;
        bool groupOpen = false;
        for (auto* item = a_conditions.head; item; item = item->next) {
            if (item->data.flags.usePackData) {
                return false;  // needs a running package's data; can't be evaluated here
            }
            if (!groupResult) {
                groupResult = item->IsTrue(params);
            }
            groupOpen = item->data.flags.isOR;
            if (!groupOpen) {
                if (!groupResult) return false;
                groupResult = false;
            }
        }
        // A trailing OR item closes its group at the end of the list
        return !groupOpen || groupResult;
    }

    void DialogueManager::RebuildIdleTopicCache() {
        _idleTopicsCached = true;
        _idleTopics.clear();
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
        for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
            if (!quest || ImpersonationManager::IsModInternalQuest(quest)) continue;
            for (auto* topic : quest->topics[kMiscIndex]) {
                if (topic && topic->numTopicInfos > 0 && topic->topicInfos && IsIdleTopic(topic)) {
                    _idleTopics.push_back({ quest, topic });
                }
            }
        }
        SKSE::log::info("Cached {} idle dialogue topics.", _idleTopics.size());
    }

    RE::BSTSmartPointer<RE::DialogueItem> DialogueManager::PickIdleLine(RE::Actor* a_player, RE::Actor* a_donor) {
        if (!_idleTopicsCached) {
            RebuildIdleTopicCache();
        }

        // Conditions run with the player as subject (condition hooks make it test as the donor)
        // and the puppet, standing in for the Dragonborn, as target
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(a_player);

        struct Candidate {
            RE::TESQuest* quest;
            RE::TESTopic* topic;
            RE::TESTopicInfo* info;
        };
        std::vector<Candidate> candidates;

        for (const auto& [quest, topic] : _idleTopics) {
            if (!quest->IsRunning()) continue;
            if (!EvaluateInQuest(quest->objConditions, quest, a_player, targetRef)) continue;

            for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                auto* info = topic->topicInfos[i];
                if (!info) continue;
                if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRequiresPlayerActivation)) continue;
                if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) continue;
                if (!EvaluateInQuest(info->objConditions, quest, a_player, targetRef)) continue;
                candidates.push_back({ quest, topic, info });
            }
        }

        if (candidates.empty()) return nullptr;

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
        const auto& chosen = candidates[dist(rng)];

        // Build the line as the donor would say it, so the voice path resolves to the donor's voice type
        auto item = RE::make_smart<RE::DialogueItem>(chosen.quest, chosen.topic, chosen.info, a_donor);
        if (!item || item->responses.empty() || !item->responses.front()) return nullptr;

        SKSE::log::info("Idle chatter: picked topic {:08X}, info {:08X}", chosen.topic->GetFormID(), chosen.info->GetFormID());
        if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
            chosen.info->saidOnce = true;
        }
        return item;
    }

    void DialogueManager::UpdateIdleChatter() {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!impMgr.IsActive() || !impMgr.IsNPCVoiceEnabled() || _playerLineActive) return;
        if (SecondsSince(_lastIdleChatterTime) < _idleChatterTimeSeconds || SecondsSince(_lastIdleEvalTime) < 1.5f) return;
        _lastIdleEvalTime = std::chrono::steady_clock::now();

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* donor = impMgr.GetDonor();
        if (!player || !donor) return;

        // MenuTopicManager's fields (speaker, menuOpen, isGreetingPlayer) stay set after a conversation
        // ends, so only the open menu counts as one
        const char* blockedBy = nullptr;
        if (IsInSelfDialogue()) {
            blockedBy = "self-dialogue";
        } else if (player->IsInCombat()) {
            blockedBy = "combat";
        } else if (IsBlockingMenuOpen()) {
            blockedBy = "menu";
        }
        if (blockedBy != _idleChatterBlockedBy) {
            if (blockedBy) {
                SKSE::log::info("Idle chatter waiting: {}", blockedBy);
            }
            _idleChatterBlockedBy = blockedBy;
        }
        if (blockedBy) return;

        auto item = PickIdleLine(player, donor);
        if (!item) {
            if (!_idleChatterFoundNothing) {
                SKSE::log::info("Idle chatter waiting: no idle line's conditions pass");
                _idleChatterFoundNothing = true;
            }
            return;
        }
        _idleChatterFoundNothing = false;

        SpeakAsPlayer(item->responses.front(), false, true);
        _playerLineItem = std::move(item);  // the response must outlive the line
        _lastIdleChatterTime = std::chrono::steady_clock::now();
    }

}
