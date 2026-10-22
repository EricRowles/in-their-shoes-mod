#include "PCH.h"
#include "DialogueManager.h"
#include "ImpersonationManager.h"
#include "IdleAIManager.h"

#include <random>
#include <cmath>
#include <algorithm>
#include <unordered_set>

#include "RE/T/TESDataHandler.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/T/TESCondition.h"
#include "RE/S/SubtitleManager.h"
#include "RE/P/PlayerCamera.h"
#include "RE/B/BSAudioManager.h"
#include "RE/I/ID.h"
#include "RE/A/AIProcess.h"
#include "RE/H/HighProcessData.h"
#include "RE/B/BSFaceGenAnimationData.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/B/BGSRefAlias.h"

namespace InTheirShoes {

    void DialogueManager::Initialize() {
        if (!_initialized) {
            auto* ui = RE::UI::GetSingleton();
            if (ui) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
                SKSE::log::info("DialogueManager registered MenuOpenCloseEvent sink successfully.");
            } else {
                SKSE::log::warn("DialogueManager failed to get UI singleton.");
            }

            InstallHooks();
            _initialized = true;
        }
    }

    void DialogueManager::InstallHooks() {
        if (_hooksInstalled) return;

        // Hook Character::SetDialogueWithPlayer on primary vtable (index 0x41)
        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[0] };
        _origSetDialogueWithPlayer = charVtbl.write_vfunc(0x41, &Hook_SetDialogueWithPlayer);
        SKSE::log::info("Hooked Character::SetDialogueWithPlayer at VTABLE_Character[0] idx 0x41 (orig: {:016X})",
            _origSetDialogueWithPlayer.address());

        // Hook Character vtable (0x4C UpdateInDialogue, 0x107 QSpeakingDone, 0x108 SetSpeakingDone)
        _origCharUpdateInDialogue = charVtbl.write_vfunc(0x4C, &Hook_Character_UpdateInDialogue);
        _origCharQSpeakingDone = charVtbl.write_vfunc(0x107, &Hook_Character_QSpeakingDone);
        _origCharSetSpeakingDone = charVtbl.write_vfunc(0x108, &Hook_Character_SetSpeakingDone);
        SKSE::log::info("Hooked Character dialogue vfuncs (0x4C, 0x107, 0x108)");

        // Hook PlayerCharacter vtable (0x4C UpdateInDialogue, 0x107 QSpeakingDone, 0x108 SetSpeakingDone)
        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerUpdateInDialogue = playerVtbl.write_vfunc(0x4C, &Hook_Player_UpdateInDialogue);
        _origPlayerQSpeakingDone = playerVtbl.write_vfunc(0x107, &Hook_Player_QSpeakingDone);
        _origPlayerSetSpeakingDone = playerVtbl.write_vfunc(0x108, &Hook_Player_SetSpeakingDone);
        SKSE::log::info("Hooked PlayerCharacter dialogue vfuncs (0x4C, 0x107, 0x108)");

        _hooksInstalled = true;
    }

    void DialogueManager::EndSelfDialogue() {
        bool wasSpeaking = _playerSpeaking.load(std::memory_order_acquire);
        _inSelfDialogue.store(false, std::memory_order_release);
        if (wasSpeaking) {
            CompleteSpeaking(RE::PlayerCharacter::GetSingleton());
        }
        ImpersonationManager::GetSingleton().ForwardPuppetFactionsToDonor();
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        if (topicMgr && puppet) {
            auto puppetHandle = puppet->GetHandle();
            if (topicMgr->speaker == puppetHandle) {
                topicMgr->speaker.reset();
            }
            if (topicMgr->lastSpeaker == puppetHandle) {
                topicMgr->lastSpeaker.reset();
            }
        }
    }

    RE::BSEventNotifyControl DialogueManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == "Dialogue Menu"sv) {
            if (!a_event->opening) {
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                bool isSpeaking = _playerSpeaking.load(std::memory_order_acquire);
                bool isGreeting = topicMgr && topicMgr->isGreetingPlayer;
                bool isGoodbye = topicMgr && topicMgr->forceGoodbye;

                if (isGoodbye || (!isSpeaking && !isGreeting && (!topicMgr || !topicMgr->menuOpen))) {
                    EndSelfDialogue();
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    bool DialogueManager::OpenSelfDialogue(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!a_speaker) {
            a_speaker = puppet ? puppet : donor;
        }
        if (!a_speaker || a_speaker == player) return false;

        SKSE::log::info("OpenSelfDialogue: received request for speaker {:08X} (puppet: {:08X}, donor: {:08X})",
            static_cast<std::uint32_t>(a_speaker->GetFormID()),
            puppet ? static_cast<std::uint32_t>(puppet->GetFormID()) : 0,
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0);

        auto* task = SKSE::GetTaskInterface();
        if (task) {
            RE::ActorHandle speakerHandle = a_speaker->GetHandle();
            task->AddTask([speakerHandle]() {
                auto speaker = speakerHandle.get();
                if (!speaker) {
                    SKSE::log::warn("OpenSelfDialogue task: speaker handle is no longer valid");
                    return;
                }
                DialogueManager::GetSingleton().OpenSelfDialogueDirect(speaker.get());
            });
            return true;
        }

        return OpenSelfDialogueDirect(a_speaker);
    }

    bool DialogueManager::OpenSelfDialogueDirect(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_speaker) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();

        auto* ui = RE::UI::GetSingleton();
        if (ui && (ui->GameIsPaused() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsModalMenuOpen())) {
            SKSE::log::warn("OpenSelfDialogueDirect: Menu or pause state blocking dialogue initiation");
            return false;
        }

        if (IdleAIManager::GetSingleton().IsAIActive()) {
            IdleAIManager::GetSingleton().StopIdleAI();
        }

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);

        _inSelfDialogue.store(true, std::memory_order_release);

        SKSE::log::info("OpenSelfDialogueDirect: initiating SetDialogueWithPlayer on speaker {:08X} (isPuppet: {}, donor: {:08X}, voiceType: {:08X})",
            static_cast<std::uint32_t>(a_speaker->GetFormID()),
            (a_speaker == puppet),
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0,
            ImpersonationManager::GetSingleton().GetDonorVoiceType() ?
                static_cast<std::uint32_t>(ImpersonationManager::GetSingleton().GetDonorVoiceType()->GetFormID()) : 0);

        if (a_speaker->SetDialogueWithPlayer(true, false, nullptr) ||
            a_speaker->SetDialogueWithPlayer(true, true, nullptr)) {
            SKSE::log::info("Successfully opened self-dialogue with actor {:08X} (isPuppet: {})",
                static_cast<std::uint32_t>(a_speaker->GetFormID()), (a_speaker == puppet));
            return true;
        }

        SKSE::log::error("Failed to open dialogue with actor {:08X}",
            static_cast<std::uint32_t>(a_speaker->GetFormID()));
        _inSelfDialogue.store(false, std::memory_order_release);
        return false;
    }

    bool DialogueManager::IsInSelfDialogue() const {
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr && topicMgr->forceGoodbye) {
            return false;
        }

        if (_inSelfDialogue.load(std::memory_order_acquire)) {
            return true;
        }

        auto* ui = RE::UI::GetSingleton();
        bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && (topicMgr->menuOpen || topicMgr->isGreetingPlayer));
        if (!menuOpen && !_playerSpeaking.load(std::memory_order_acquire)) {
            return false;
        }

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!puppet && !donor) return false;

        if (topicMgr) {
            if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                return true;
            }
            if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                return true;
            }
        }

        return false;
    }

    void DialogueManager::AttachSoundHandlesToHead(RE::Actor* a_actor) {
        if (!a_actor) return;
        auto* process = a_actor->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) return;

        auto* cam = RE::PlayerCamera::GetSingleton();
        bool isFirstPerson = cam && cam->IsInFirstPerson();
        auto actorPos = a_actor->GetPosition();

        auto* root3D = a_actor->Get3D();
        RE::NiAVObject* headNode = root3D ? root3D->GetObjectByName("NPC Head [Head]"sv) : nullptr;
        if (!headNode) {
            headNode = a_actor->GetFaceNodeSkinned();
        }
        if (!headNode && root3D) {
            headNode = root3D;
        }

        RE::NiPoint3 soundPos = actorPos;
        soundPos.z += 120.0f;

        bool useHeadNode = false;
        if (headNode && !isFirstPerson) {
            float dist = headNode->world.translate.GetDistance(actorPos);
            if (dist < 400.0f && (headNode->world.translate.x != 0.0f || headNode->world.translate.y != 0.0f || headNode->world.translate.z != 0.0f)) {
                soundPos = headNode->world.translate;
                useHeadNode = true;
            }
        }

        for (auto& handle : process->high->soundHandles) {
            if (handle.IsValid()) {
                handle.SetPosition(soundPos);
                if (headNode && useHeadNode) {
                    handle.SetObjectToFollow(headNode);
                }
                handle.SetVolume(1.0f);
            }
        }
    }

    void DialogueManager::OnDialogueResponseStarted(RE::DialogueResponse* a_response) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _playerSpeaking.store(true, std::memory_order_release);
        _speechStartTime = std::chrono::steady_clock::now();
        _speechAudioEndTime = std::chrono::steady_clock::time_point{};
        _speechHadSound.store(false, std::memory_order_release);
        float textLen = 0.0f;
        if (a_response && !a_response->text.empty()) {
            textLen = static_cast<float>(a_response->text.length());
        }
        _speechMinDuration = std::max(1.5f, textLen * 0.065f);
    }

    void DialogueManager::TickSpeech(RE::Actor* a_player, bool a_menuOpen) {
        if (!IsPlayerSpeaking()) return;
        if (a_player && !a_menuOpen) {
            _origCharUpdateInDialogue(a_player, nullptr, false);
            AttachSoundHandlesToHead(a_player);
        }
        CheckPlayerSpeakingDone();
    }

    static void ResetActorFaceGenAndEmotion(RE::Actor* a_actor) {
        if (!a_actor) return;
        if (auto* faceData = a_actor->GetFaceGenAnimationData()) {
            RE::BSSpinLockGuard locker(faceData->lock);
            faceData->dialogueData = nullptr;
            faceData->Reset(0.0f, true, true, true, false);
            faceData->ClearExpressionOverride();
        }
        a_actor->SetEmotionType(RE::EmotionType::kNeutral);
        a_actor->SetEmotionValue(0);
    }

    void DialogueManager::CompleteSpeaking(RE::Actor* a_player) {
        if (_inCompleteSpeaking.exchange(true)) {
            return;
        }

        bool wasIdleChatter = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _playerSpeaking.store(false, std::memory_order_release);
            _speechHadSound.store(false, std::memory_order_release);
            _speechAudioEndTime = std::chrono::steady_clock::time_point{};
            wasIdleChatter = _isPlayingIdleChatter.exchange(false, std::memory_order_acq_rel);
        }

        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        auto* dial = topicMgr ? topicMgr->lastSelectedDialogue : nullptr;
        bool hasNextResponse = (dial && dial->currentResponse && dial->currentResponse->next);

        if (a_player) {
            auto* process = a_player->GetActorRuntimeData().currentProcess;
            if (process && process->high) {
                for (auto& handle : process->high->soundHandles) {
                    if (handle.IsValid() && handle.IsPlaying()) {
                        handle.Stop();
                    }
                }
                process->high->greetTopic.reset();
            }

            SetSpeechBits(a_player, true, true);
            a_player->GetActorRuntimeData().voiceTimer = 0.0f;
            _origPlayerSetSpeakingDone(a_player, true);
            ResetActorFaceGenAndEmotion(a_player);
        }

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        if (puppet) {
            auto* proc = puppet->GetActorRuntimeData().currentProcess;
            if (proc && proc->high) {
                for (auto& handle : proc->high->soundHandles) {
                    if (handle.IsValid()) {
                        handle.Stop();
                    }
                    handle = RE::BSSoundHandle();
                }
                proc->high->voiceTimer = 0.0f;
                proc->high->breathTimer = 0.0f;
                proc->high->greetTopic.reset();
            }

            SetSpeechBits(puppet, true, true);
            puppet->GetActorRuntimeData().voiceTimer = 0.0f;
            ResetActorFaceGenAndEmotion(puppet);
            _origCharSetSpeakingDone(puppet, true);
        }

        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (donor) {
            SetSpeechBits(donor, true, true);
            donor->GetActorRuntimeData().voiceTimer = 0.0f;
            _origCharSetSpeakingDone(donor, true);
        }

        if (topicMgr && !wasIdleChatter) {
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || topicMgr->menuOpen;

            if (menuOpen) {
                if (puppet) {
                    topicMgr->lastSpeaker = puppet->GetHandle();
                }
                topicMgr->canSkip = true;
                topicMgr->advanceRequested = true;
                SKSE::log::info("CompleteSpeaking: line finished in Dialogue Menu, requesting advance (more responses: {})",
                    hasNextResponse);
            } else {
                topicMgr->isGreetingPlayer = false;
                if (puppet) {
                    topicMgr->lastSpeaker = puppet->GetHandle();
                }
                if (topicMgr->speaker == (puppet ? puppet->GetHandle() : RE::ObjectRefHandle()) ||
                    topicMgr->speaker == (a_player ? a_player->GetHandle() : RE::ObjectRefHandle())) {
                    topicMgr->speaker.reset();
                }
                if (topicMgr->currentTopicInfo) {
                    topicMgr->lastTopicInfo = topicMgr->currentTopicInfo;
                    topicMgr->currentTopicInfo = nullptr;
                }

                if (dial && hasNextResponse) {
                    dial->currentResponse = dial->currentResponse->next;
                    topicMgr->waitingToAdvance = true;
                    topicMgr->canSkip = true;
                    topicMgr->advanceRequested = true;
                    SKSE::log::info("CompleteSpeaking: advancing multi-line topic to next response");
                } else {
                    if (dial) {
                        dial->currentResponse = nullptr;
                    }
                    topicMgr->waitingToAdvance = false;
                    topicMgr->canSkip = false;
                    topicMgr->advanceRequested = false;
                    topicMgr->lastSelectedDialogue = nullptr;
                    SKSE::log::info("CompleteSpeaking: line finished outside Dialogue Menu, reset MenuTopicManager");
                }
            }
        }

        if (wasIdleChatter) {
            OnIdleChatterLineCompleted();
        }

        _inCompleteSpeaking.store(false);
    }

    void DialogueManager::ResetSpeechTracking() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        CompleteSpeaking(player);
    }

    bool DialogueManager::CheckPlayerSpeakingDone() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_playerSpeaking.load(std::memory_order_acquire)) {
            return true;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return true;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _speechStartTime).count();

        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && topicMgr->menuOpen);
        if (menuOpen && topicMgr && topicMgr->advanceRequested && elapsed > 0.25f) {
            SKSE::log::info("Player requested dialogue line skip via MenuTopicManager (elapsed: {:.2f}s)", elapsed);
            CompleteSpeaking(player);
            return true;
        }

        auto checkHandleActive = [](const RE::BSSoundHandle& h) -> bool {
            if (!h.IsValid()) return false;
            if (h.IsPlaying()) return true;
            auto st = h.state.get();
            return (st == RE::BSSoundHandle::AssumedState::kPlaying ||
                    st == RE::BSSoundHandle::AssumedState::kInitialized);
        };

        bool soundPlaying = false;
        auto* process = player->GetActorRuntimeData().currentProcess;
        if (process && process->high) {
            for (auto& soundHandle : process->high->soundHandles) {
                if (checkHandleActive(soundHandle)) {
                    soundPlaying = true;
                    break;
                }
            }
        }

        if (soundPlaying) {
            AttachSoundHandlesToHead(player);
            if (!_speechHadSound.load(std::memory_order_acquire)) {
                _speechStartTime = now;
                elapsed = 0.0f;
            }
            _speechHadSound.store(true, std::memory_order_release);

            if (elapsed < 25.0f) {
                return false;
            }
        }

        bool phonemesActive = false;
        if (auto* faceData = player->GetFaceGenAnimationData()) {
            RE::BSSpinLockGuard locker(faceData->lock);
            auto checkActiveKeyframe = [](const RE::BSFaceGenKeyframeMultiple& kf) -> bool {
                if (kf.values && kf.count > 0 && kf.count < 64) {
                    for (std::uint32_t i = 0; i < kf.count; ++i) {
                        if (kf.values[i] > 0.005f) {
                            return true;
                        }
                    }
                }
                return false;
            };
            if (checkActiveKeyframe(faceData->phoneme1) ||
                checkActiveKeyframe(faceData->phoneme3) ||
                checkActiveKeyframe(faceData->phenomeKeyFrame)) {
                phonemesActive = true;
            }
        }

        if (_speechHadSound.load(std::memory_order_acquire)) {
            if (elapsed < 0.5f) {
                return false;
            }

            if (phonemesActive && elapsed < 25.0f && _speechAudioEndTime == std::chrono::steady_clock::time_point{}) {
                _speechAudioEndTime = now;
                return false;
            }
            if (phonemesActive && _speechAudioEndTime != std::chrono::steady_clock::time_point{}) {
                float phonemeWait = std::chrono::duration<float>(now - _speechAudioEndTime).count();
                if (phonemeWait < 0.4f) {
                    return false;
                }
            }

            SKSE::log::info("Player dialogue speech completed (native audio & phonemes finished after {:.2f}s)", elapsed);
            CompleteSpeaking(player);
            return true;
        }

        if (elapsed < _speechMinDuration) {
            return false;
        }

        SKSE::log::info("Player dialogue speech completed (unvoiced line finished after {:.2f}s)", elapsed);
        CompleteSpeaking(player);
        return true;
    }

    bool DialogueManager::TryCreateDialogueSoundHandle(RE::Actor* a_actor, RE::DialogueResponse* a_response) {
        if (!a_actor || !a_response) return false;
        auto* process = a_actor->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) return false;

        RE::BSSoundHandle soundHandle;
        auto* audioMgr = RE::BSAudioManager::GetSingleton();
        if (!audioMgr) return false;

        if (a_response->voiceSound) {
            audioMgr->GetSoundHandle(soundHandle, a_response->voiceSound);
            if (soundHandle.IsValid()) {
                SKSE::log::info("TryCreateDialogueSoundHandle: Created sound handle from voiceSound descriptor {:08X}",
                    static_cast<std::uint32_t>(a_response->voiceSound->GetFormID()));
            }
        } else if (!a_response->voice.empty()) {
            std::string voicePath = a_response->voice.c_str();
            std::replace(voicePath.begin(), voicePath.end(), '/', '\\');
            if (voicePath.length() >= 5 && _strnicmp(voicePath.c_str(), "data\\", 5) == 0) {
                voicePath = voicePath.substr(5);
            }

            auto dotPos = voicePath.rfind('.');
            std::string basePath = (dotPos != std::string::npos) ? voicePath.substr(0, dotPos) : voicePath;

            std::vector<std::string> pathCandidates = {
                basePath + ".fuz",
                basePath + ".wav",
                voicePath
            };

            for (const auto& candidate : pathCandidates) {
                RE::BSResource::ID resID;
                resID.GenerateFromPath(candidate.c_str());
                audioMgr->GetSoundHandleByFile(soundHandle, resID, 0x10, 64);
                if (soundHandle.IsValid()) {
                    SKSE::log::info("TryCreateDialogueSoundHandle: Successfully resolved sound handle using candidate '{}'", candidate);
                    break;
                }
            }
        }

        if (soundHandle.IsValid()) {
            auto* headNode = a_actor->GetNodeByName("NPC Head [Head]");
            if (headNode) {
                soundHandle.SetObjectToFollow(headNode);
            } else if (auto* root = a_actor->Get3D()) {
                soundHandle.SetObjectToFollow(root);
            }
            soundHandle.SetPosition(a_actor->GetPosition());
            soundHandle.SetVolume(1.0f);
            soundHandle.Play();

            SKSE::log::info("TryCreateDialogueSoundHandle: Playing sound handle id={:08X}, durMS={}, isPlaying={}",
                soundHandle.soundID, soundHandle.GetDuration(), soundHandle.IsPlaying());

            process->high->soundHandles[0] = soundHandle;
            return true;
        } else {
            SKSE::log::warn("TryCreateDialogueSoundHandle: Failed to create sound handle for voice='{}'",
                a_response->voice.c_str());
            return false;
        }
    }

    void DialogueManager::QueueSubtitle(RE::Actor* a_speaker, const RE::DialogueResponse* a_response) {
        if (!a_speaker || !a_response || a_response->text.empty()) return;
        auto* subMgr = RE::SubtitleManager::GetSingleton();
        if (!subMgr) return;

        RE::BSSpinLockGuard locker(subMgr->lock);
        for (auto& sub : subMgr->subtitles) {
            if (sub.subtitle == a_response->text.c_str()) {
                sub.speaker = a_speaker->GetHandle();
                sub.targetDistance = 0.0f;
                sub.forceDisplay = true;
                return;
            }
        }
        RE::SubtitleInfo info;
        info.speaker = a_speaker->GetHandle();
        info.subtitle = a_response->text;
        info.targetDistance = 0.0f;
        info.forceDisplay = true;
        subMgr->subtitles.push_back(info);
    }

    void DialogueManager::SetSpeechBits(RE::Actor* a_actor, bool a_speakingDone, bool a_filesDone) {
        auto& bits = a_actor->GetActorRuntimeData().boolBits;
        if (a_speakingDone) {
            bits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
        } else {
            bits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
        }
        if (a_filesDone) {
            bits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
            bits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
        } else {
            bits.reset(RE::Actor::BOOL_BITS::kSoundFileDone);
            bits.reset(RE::Actor::BOOL_BITS::kVoiceFileDone);
        }
    }

    bool DialogueManager::HasPlayingSound(RE::Actor* a_actor) {
        auto* process = a_actor->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) return false;
        for (auto& handle : process->high->soundHandles) {
            if (handle.IsValid() && handle.IsPlaying()) return true;
        }
        return false;
    }

    bool DialogueManager::SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_response) return false;

        if (a_response->useEmotion) {
            player->SetEmotionType(a_response->animFaceArchType.get());
            player->SetEmotionValue(a_response->percent);
        }
        QueueSubtitle(player, a_response);

        SetSpeechBits(player, false, true);
        player->GetActorRuntimeData().voiceTimer = 0.0f;
        _origPlayerSetSpeakingDone(player, false);

        OnDialogueResponseStarted(a_response);

        bool ret = _origCharUpdateInDialogue(player, a_response, a_unused);

        // Notify engine sound callback that dialogue line has started
        player->GetActorRuntimeData().soundCallBackSet = 1;

        if (!HasPlayingSound(player)) {
            TryCreateDialogueSoundHandle(player, a_response);
        }
        AttachSoundHandlesToHead(player);
        return ret;
    }

    bool DialogueManager::PlayIdleDialogueLine(RE::DialogueResponse* a_response, RE::TESTopicInfo* a_info) {
        if (!a_response) return false;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        SKSE::log::info("PlayIdleDialogueLine: '{}' (topicInfo={:08X}, voice='{}', sound={:08X})",
            a_response->text.c_str(),
            a_info ? static_cast<std::uint32_t>(a_info->GetFormID()) : 0,
            a_response->voice.c_str(),
            a_response->voiceSound ? static_cast<std::uint32_t>(a_response->voiceSound->GetFormID()) : 0);

        auto* playerProcess = player->GetActorRuntimeData().currentProcess;
        if (playerProcess && playerProcess->high) {
            for (auto& handle : playerProcess->high->soundHandles) {
                if (handle.IsValid() && handle.IsPlaying()) {
                    handle.Stop();
                }
                handle = RE::BSSoundHandle();
            }
        }

        _isPlayingIdleChatter.store(true, std::memory_order_release);
        SpeakAsPlayer(a_response, false);

        if (playerProcess && playerProcess->high && playerProcess->high->soundHandles[0].IsValid()) {
            auto dur = playerProcess->high->soundHandles[0].GetDuration();
            if (dur > 0) {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                _speechMinDuration = std::max(_speechMinDuration, (static_cast<float>(dur) / 1000.0f) + 0.3f);
                _speechHadSound.store(true, std::memory_order_release);
            }
        }
        return true;
    }

    bool DialogueManager::HandleProxyDialogueOutput(RE::Actor* a_speaker, RE::DialogueResponse* a_response, bool a_unused) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_speaker) {
            return _origCharUpdateInDialogue(a_speaker, a_response, a_unused);
        }

        if (a_response != nullptr) {
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            bool inSelf = IsInSelfDialogue();
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && (topicMgr->menuOpen || topicMgr->isGreetingPlayer));
            auto* donor = ImpersonationManager::GetSingleton().GetDonor();
            bool isDonor = (donor && a_speaker == donor);
            auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
            bool isPuppet = (puppet && a_speaker == puppet);

            if (topicMgr && topicMgr->speaker &&
                (!puppet || topicMgr->speaker != puppet->GetHandle()) &&
                (!donor || topicMgr->speaker != donor->GetHandle())) {
                SKSE::log::info("HandleProxyDialogueOutput: Suppressing proxy dialogue because external speaker {:08X} is active",
                    topicMgr->speaker.native_handle());
                return false;
            }

            bool inTopicWithSpeaker = false;
            if (topicMgr && (isPuppet || isDonor)) {
                auto spkHandle = a_speaker->GetHandle();
                if (menuOpen && (topicMgr->speaker == spkHandle || topicMgr->lastSpeaker == spkHandle)) {
                    inTopicWithSpeaker = true;
                }
            }

            bool isGoodbye = false;
            if (topicMgr) {
                if (topicMgr->forceGoodbye) {
                    isGoodbye = true;
                }
                auto checkSubtypeGoodbye = [](RE::TESTopicInfo* info) -> bool {
                    if (!info || !info->parentTopic) return false;
                    auto st = info->parentTopic->data.subtype.get();
                    return (st == RE::DIALOGUE_DATA::Subtype::kGoodBye || st == RE::DIALOGUE_DATA::Subtype::kTimeToGo);
                };
                if (checkSubtypeGoodbye(topicMgr->currentTopicInfo)) {
                    isGoodbye = true;
                }
            }

            bool inActiveConversation = inSelf || menuOpen || inTopicWithSpeaker;

            if (isPuppet && (isGoodbye || !inActiveConversation)) {
                SKSE::log::info("Suppressing puppet dialogue exit/goodbye/ambient line: '{}'", a_response->text.c_str());
                return false;
            }

            if (inActiveConversation || isDonor) {
                SKSE::log::info("Decoupled proxy dialogue: text='{}', voice='{}', sound={:08X}",
                    a_response->text.c_str(),
                    a_response->voice.c_str(),
                    a_response->voiceSound ? static_cast<std::uint32_t>(a_response->voiceSound->GetFormID()) : 0);

                auto* playerProcess = player->GetActorRuntimeData().currentProcess;
                if (playerProcess && playerProcess->high) {
                    for (auto& handle : playerProcess->high->soundHandles) {
                        if (handle.IsValid() && handle.IsPlaying()) {
                            handle.Stop();
                        }
                    }
                }

                if (topicMgr) {
                    topicMgr->advanceRequested = false;
                    topicMgr->lineSkipped = false;
                }

                RE::DialogueResponse playerResp;
                playerResp.text = a_response->text;
                playerResp.animFaceArchType = a_response->animFaceArchType;
                playerResp.percent = a_response->percent;
                playerResp.voice = a_response->voice;
                playerResp.speakerIdle = a_response->speakerIdle;
                playerResp.listenIdle = a_response->listenIdle;
                playerResp.voiceSound = a_response->voiceSound;
                playerResp.useEmotion = a_response->useEmotion;
                playerResp.soundLip = a_response->soundLip;

                RE::DialogueResponse puppetResp;
                puppetResp.text = a_response->text;
                puppetResp.animFaceArchType = RE::EmotionType::kNeutral;
                puppetResp.percent = 0;
                puppetResp.voice = ""sv;
                puppetResp.voiceSound = nullptr;
                puppetResp.soundLip = false;
                puppetResp.speakerIdle = nullptr;
                puppetResp.listenIdle = nullptr;
                puppetResp.useEmotion = false;

                SetSpeechBits(a_speaker, false, false);
                a_speaker->GetActorRuntimeData().voiceTimer = 0.0f;
                _origCharSetSpeakingDone(a_speaker, false);

                SpeakAsPlayer(&playerResp, a_unused);

                bool ret = _origCharUpdateInDialogue(a_speaker, &puppetResp, a_unused);

                SetSpeechBits(a_speaker, false, false);
                ResetActorFaceGenAndEmotion(a_speaker);

                return ret;
            } else {
                SKSE::log::info("Suppressing proxy actor spontaneous idle speech outside conversation: '{}'", a_response->text.c_str());
                return false;
            }
        } else {
            if (_playerSpeaking.load(std::memory_order_relaxed)) {
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSoundFileDone);
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kVoiceFileDone);

                _origCharUpdateInDialogue(player, nullptr, a_unused);
                AttachSoundHandlesToHead(player);
            } else {
                SetSpeechBits(a_speaker, true, true);
                a_speaker->GetActorRuntimeData().voiceTimer = 0.0f;

                auto* ui = RE::UI::GetSingleton();
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                if ((ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && topicMgr->menuOpen)) {
                    if (auto* spkProcess = a_speaker->GetActorRuntimeData().currentProcess; spkProcess && spkProcess->high) {
                        spkProcess->high->voiceTimer = 10.0f;
                    }
                }
            }

            return _origCharUpdateInDialogue(a_speaker, nullptr, a_unused);
        }
    }

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        if (a_this) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                auto* puppet = impMgr.GetPuppet();
                auto* donor = impMgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    return DialogueManager::GetSingleton().HandleProxyDialogueOutput(a_this, a_response, a_unused);
                }
            }
        }
        return _origCharUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool DialogueManager::Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        if (a_this && a_response != nullptr) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                auto* puppet = impMgr.GetPuppet();
                auto* donor = impMgr.GetDonor();

                if (topicMgr && topicMgr->speaker &&
                    (!puppet || topicMgr->speaker != puppet->GetHandle()) &&
                    (!donor || topicMgr->speaker != donor->GetHandle())) {
                    SKSE::log::info("Hook_Player_UpdateInDialogue: Suppressed player proxy speech while external NPC speaker {:08X} is active",
                        topicMgr->speaker.native_handle());
                    return _origPlayerUpdateInDialogue(a_this, nullptr, a_unused);
                }

                SKSE::log::info("Player {:08X} speaking dialogue line: '{}'",
                    static_cast<std::uint32_t>(a_this->GetFormID()),
                    a_response->text.c_str());
                return DialogueManager::GetSingleton().SpeakAsPlayer(a_response, a_unused);
            }
        }
        bool ret = _origPlayerUpdateInDialogue(a_this, a_response, a_unused);
        if (a_this && a_response != nullptr) {
            AttachSoundHandlesToHead(a_this);
        }
        return ret;
    }

    bool DialogueManager::Hook_Character_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                auto* puppet = impMgr.GetPuppet();
                auto* donor = impMgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    if (!DialogueManager::GetSingleton().IsPlayerSpeaking()) {
                        return true;
                    }
                    return DialogueManager::GetSingleton().CheckPlayerSpeakingDone();
                }
            }
        }
        return _origCharQSpeakingDone(a_this);
    }

    bool DialogueManager::Hook_Player_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                if (!DialogueManager::GetSingleton().IsPlayerSpeaking()) {
                    return true;
                }
                return DialogueManager::GetSingleton().CheckPlayerSpeakingDone();
            }
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void DialogueManager::Hook_Character_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        if (a_this && a_set) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                auto* puppet = impMgr.GetPuppet();
                auto* donor = impMgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    if (DialogueManager::GetSingleton().IsPlayerSpeaking()) {
                        return;
                    }
                    ResetActorFaceGenAndEmotion(RE::PlayerCharacter::GetSingleton());
                }
            }
        }
        _origCharSetSpeakingDone(a_this, a_set);
    }

    void DialogueManager::Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        if (a_this) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                if (a_set) {
                    if (DialogueManager::GetSingleton().IsPlayerSpeaking() && !DialogueManager::GetSingleton().IsInCompleteSpeaking()) {
                        return;
                    }
                    if (!DialogueManager::GetSingleton().IsInCompleteSpeaking()) {
                        DialogueManager::GetSingleton().ResetSpeechTracking();
                    }
                    a_this->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
                } else {
                    DialogueManager::GetSingleton().SetPlayerSpeaking(true);
                    a_this->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
                }
            }
        }
        _origPlayerSetSpeakingDone(a_this, a_set);
    }

    bool DialogueManager::Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic) {
        if (a_this) {
            auto& impMgr = ImpersonationManager::GetSingleton();
            if (impMgr.IsActive()) {
                auto* puppet = impMgr.GetPuppet();
                auto* donor = impMgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    SKSE::log::info("Hook_SetDialogueWithPlayer: actor {:08X} (isPuppet: {}), flag: {}, forceGreet: {}",
                        static_cast<std::uint32_t>(a_this->GetFormID()), (a_this == puppet), a_flag, a_forceGreet);
                    DialogueManager::GetSingleton().SetInSelfDialogue(a_flag);
                    if (!a_flag && a_this == puppet) {
                        impMgr.ForwardPuppetFactionsToDonor();

                        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                        if (topicMgr) {
                            if (topicMgr->speaker == a_this->GetHandle()) {
                                topicMgr->speaker.reset();
                            }
                            if (topicMgr->lastSpeaker == a_this->GetHandle()) {
                                topicMgr->lastSpeaker.reset();
                            }
                        }
                    }
                } else if (a_flag) {
                    DialogueManager::GetSingleton().SetInSelfDialogue(false);
                }
            }
        }
        return _origSetDialogueWithPlayer(a_this, a_flag, a_forceGreet, a_topic);
    }

    static bool SafeConditionCheck(RE::TESCondition* a_cond, RE::TESObjectREFR* a_actionRef, RE::TESObjectREFR* a_targetRef) {
        if (!a_cond || !a_cond->head) return true;
        __try {
            return a_cond->IsTrue(a_actionRef, a_targetRef);
        } __except (1) {
            return false;
        }
    }

    static bool IsBaseGameOrDLCFile(const RE::TESFile* a_file) {
        if (!a_file) return true;
        std::string_view name = a_file->GetFilename();
        return (_strnicmp(name.data(), "Skyrim.esm", name.size()) == 0 ||
                _strnicmp(name.data(), "Update.esm", name.size()) == 0 ||
                _strnicmp(name.data(), "Dawnguard.esm", name.size()) == 0 ||
                _strnicmp(name.data(), "HearthFires.esm", name.size()) == 0 ||
                _strnicmp(name.data(), "Dragonborn.esm", name.size()) == 0);
    }

    static bool ConditionReferencesDonor(const RE::TESCondition* a_cond, RE::Actor* a_donor, RE::BGSVoiceType* a_donorVoice) {
        if (!a_cond || !a_donor) return false;
        auto* donorBase = a_donor->GetActorBase();
        for (auto* item = a_cond->head; item; item = item->next) {
            auto fn = item->data.functionData.function.underlying();
            auto* form = static_cast<RE::TESForm*>(item->data.functionData.params[0]);
            if (!form) continue;

            if (fn == 72) { // kGetIsID
                if (form == a_donor || form == a_donor->GetBaseObject() || form == donorBase) {
                    return true;
                }
            } else if (fn == 427 && a_donorVoice) { // kGetIsVoiceType
                if (form == a_donorVoice) {
                    return true;
                }
            } else if (fn == 71) { // kGetInFaction
                if (auto* fac = form->As<RE::TESFaction>()) {
                    if (a_donor->IsInFaction(fac)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static bool QuestHasIdleTopics(RE::TESQuest* a_quest) {
        if (!a_quest) return false;
        constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
        if (kMiscIndex >= (RE::DIALOGUE_TYPE::kTotal - RE::DIALOGUE_TYPE::kBranchedTotal)) {
            return false;
        }
        for (auto* topic : a_quest->topics[kMiscIndex]) {
            if (!topic || topic->numTopicInfos == 0 || !topic->topicInfos) continue;
            auto subtypeVal = topic->data.subtype.underlying();
            if (subtypeVal == 88 || subtypeVal == 94 || topic->data.subtype.get() == RE::DIALOGUE_DATA::Subtype::kIdle) {
                return true;
            }
        }
        return false;
    }

    static bool QuestRelatesToDonor(
        RE::TESQuest* a_quest,
        RE::Actor* a_donor,
        RE::BGSVoiceType* a_donorVoice,
        const RE::TESFile* a_donorFile,
        const std::unordered_set<RE::TESQuest*>& a_donorActiveQuests)
    {
        if (!a_quest || !a_donor) return false;
        if (a_donorActiveQuests.contains(a_quest)) {
            return true;
        }
        // Only treat same-file origin as donor-related for non-base-game mod plugins
        if (a_donorFile && !IsBaseGameOrDLCFile(a_donorFile) && a_quest->GetFile(0) == a_donorFile) {
            return true;
        }
        for (auto* baseAlias : a_quest->aliases) {
            if (!baseAlias) continue;
            if (auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(baseAlias)) {
                auto* ref = refAlias->GetReference();
                if (ref && (ref == a_donor || ref->GetBaseObject() == a_donor->GetBaseObject() || ref->GetBaseObject() == a_donor->GetActorBase())) {
                    return true;
                }
            }
        }
        if (ConditionReferencesDonor(a_quest->QConditions(), a_donor, a_donorVoice)) {
            return true;
        }
        constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
        for (auto* topic : a_quest->topics[kMiscIndex]) {
            if (!topic || topic->numTopicInfos == 0 || !topic->topicInfos) continue;
            auto subtypeVal = topic->data.subtype.underlying();
            if (subtypeVal != 88 && subtypeVal != 94 && topic->data.subtype.get() != RE::DIALOGUE_DATA::Subtype::kIdle) {
                continue;
            }
            for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                if (auto* info = topic->topicInfos[i]) {
                    if (ConditionReferencesDonor(&info->objConditions, a_donor, a_donorVoice)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void DialogueManager::RebuildIdleQuestCache(RE::Actor* a_donor, RE::Actor* a_player) {
        _cachedIdleDonor = a_donor;
        _cachedIdleQuests.clear();
        if (!a_donor || !a_player) return;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        auto& impMgr = ImpersonationManager::GetSingleton();
        auto* donorBase = a_donor->GetActorBase();
        const auto* donorFile = donorBase ? donorBase->GetFile(0) : nullptr;
        auto* donorVoice = impMgr.GetDonorVoiceType();

        std::unordered_set<RE::TESQuest*> donorActiveQuests;
        if (auto* aliasArray = a_donor->extraList.GetByType<RE::ExtraAliasInstanceArray>()) {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (inst && inst->quest) {
                    donorActiveQuests.insert(inst->quest);
                }
            }
        }
        if (auto* aliasArray = a_player->extraList.GetByType<RE::ExtraAliasInstanceArray>()) {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (inst && inst->quest && inst->alias && impMgr.IsSwappedAlias(inst->quest->GetFormID(), inst->alias->aliasID)) {
                    donorActiveQuests.insert(inst->quest);
                }
            }
        }

        for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
            if (!quest || !QuestHasIdleTopics(quest)) continue;
            if (QuestRelatesToDonor(quest, a_donor, donorVoice, donorFile, donorActiveQuests)) {
                _cachedIdleQuests.push_back(quest);
            }
        }

        SKSE::log::info("RebuildIdleQuestCache: Cached {} idle dialogue quests for donor {:08X}.",
            _cachedIdleQuests.size(), a_donor->GetFormID());
    }

    void DialogueManager::UpdateIdleChatter() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto now = std::chrono::steady_clock::now();
        float chatterElapsed = std::chrono::duration<float>(now - _lastIdleChatterTime).count();
        float chatterCooldown = _idleChatterTimeSeconds.load(std::memory_order_acquire);
        float evalElapsed = std::chrono::duration<float>(now - _lastIdleEvalTime).count();

        if (chatterElapsed >= chatterCooldown && evalElapsed >= 1.5f && !_playerSpeaking.load(std::memory_order_acquire)) {
            CheckAndTriggerIdleDialogue();
        }
    }

    bool DialogueManager::CheckAndTriggerIdleDialogue() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!impMgr.IsActive() || !impMgr.IsNPCVoiceEnabled()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* donor = impMgr.GetDonor();
        if (!player || !donor) return false;

        if (_playerSpeaking.load(std::memory_order_acquire) || IsInSelfDialogue() || player->IsInCombat()) {
            return false;
        }

        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr && (topicMgr->isGreetingPlayer || topicMgr->menuOpen || topicMgr->speaker || topicMgr->lastSpeaker || topicMgr->currentTopicInfo || topicMgr->lastSelectedDialogue)) {
            return false;
        }

        if (IsBlockingMenuOpen()) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _lastIdleChatterTime).count();
        float kIdleChatterCooldown = _idleChatterTimeSeconds.load(std::memory_order_acquire);
        if (elapsed < kIdleChatterCooldown) {
            return false;
        }

        float evalElapsed = std::chrono::duration<float>(now - _lastIdleEvalTime).count();
        if (evalElapsed < 1.5f) {
            return false;
        }
        _lastIdleEvalTime = now;

        if (_cachedIdleDonor != donor) {
            RebuildIdleQuestCache(donor, player);
        }

        auto* puppet = impMgr.GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(player);

        struct Candidate {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
            RE::TESTopicInfo* info{ nullptr };
        };
        std::vector<Candidate> candidates;

        constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
        for (auto* quest : _cachedIdleQuests) {
            if (!quest || !quest->IsRunning()) continue;
            if (quest->objConditions.head && !SafeConditionCheck(&quest->objConditions, player, targetRef)) {
                continue;
            }

            for (auto* topic : quest->topics[kMiscIndex]) {
                if (!topic || topic->numTopicInfos == 0 || !topic->topicInfos) continue;
                auto subtypeVal = topic->data.subtype.underlying();
                if (subtypeVal != 88 && subtypeVal != 94 && topic->data.subtype.get() != RE::DIALOGUE_DATA::Subtype::kIdle) {
                    continue;
                }

                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (!info) continue;

                    if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRequiresPlayerActivation)) {
                        continue;
                    }
                    if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                        continue;
                    }
                    if (info->objConditions.head && !SafeConditionCheck(&info->objConditions, player, targetRef)) {
                        continue;
                    }

                    candidates.push_back({ quest, topic, info });
                }
            }
        }

        if (candidates.empty()) {
            return false;
        }

        Candidate chosen = candidates[0];
        if (candidates.size() > 1) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
            chosen = candidates[dist(rng)];
        }

        auto* speakerObj = static_cast<RE::TESObjectREFR*>(donor);
        auto dialogueItem = RE::make_smart<RE::DialogueItem>(chosen.quest, chosen.topic, chosen.info, speakerObj);
        if (dialogueItem && !dialogueItem->responses.empty()) {
            if (auto* response = dialogueItem->responses.front()) {
                SKSE::log::info("Triggering autonomous idle dialogue on player: '{}' (topic {:08X}, info {:08X}, voice='{}', sound={:08X})",
                    response->text.c_str(), chosen.topic->GetFormID(), chosen.info->GetFormID(),
                    response->voice.c_str(),
                    response->voiceSound ? static_cast<std::uint32_t>(response->voiceSound->GetFormID()) : 0);

                dialogueItem->speaker = player;
                PlayIdleDialogueLine(response, chosen.info);

                if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                    chosen.info->saidOnce = true;
                }

                _lastIdleChatterTime = now;
                return true;
            }
        }

        return false;
    }

}
