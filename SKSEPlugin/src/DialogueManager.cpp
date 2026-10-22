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
#include "RE/A/AIProcess.h"
#include "RE/H/HighProcessData.h"
#include "RE/B/BSFaceGenAnimationData.h"

namespace InTheirShoes {

    // Unvoiced lines are shown for roughly this long per character of text
    constexpr float kUnvoicedSecondsPerChar = 0.065f;
    constexpr float kUnvoicedMinSeconds = 1.5f;
    // Failsafe so a sound handle that never reports finishing can't lock up a scene
    constexpr float kMaxLineSeconds = 30.0f;
    // A line's sound handle may not report as playing on the frame it starts
    constexpr float kLineStartGraceSeconds = 0.2f;

    static float SecondsSince(std::chrono::steady_clock::time_point a_time) {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_time).count();
    }

    static std::span<RE::BSSoundHandle> GetSoundHandles(RE::Actor* a_actor) {
        auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
        if (!process || !process->high) return {};
        return process->high->soundHandles;
    }

    static void StopSounds(RE::Actor* a_actor) {
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (handle.IsValid()) {
                handle.Stop();
            }
        }
    }

    static void SetSoundVolume(RE::Actor* a_actor, float a_volume) {
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (handle.IsValid()) {
                handle.SetVolume(a_volume);
            }
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

    static RE::NiAVObject* GetHeadNode(RE::Actor* a_actor) {
        auto* root = a_actor->Get3D();
        RE::NiAVObject* head = root ? root->GetObjectByName("NPC Head [Head]"sv) : nullptr;
        return head ? head : root;
    }

    // Makes a_source's playing sounds come from a_listener's head
    static void MoveSoundsToHead(RE::Actor* a_source, RE::Actor* a_listener) {
        auto* head = GetHeadNode(a_listener);
        if (!head) return;
        for (auto& handle : GetSoundHandles(a_source)) {
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

    static RE::DialogueResponse MakeSilentCopy(const RE::DialogueResponse* a_response) {
        RE::DialogueResponse silent;
        silent.text = a_response->text;
        silent.animFaceArchType = RE::EmotionType::kNeutral;
        silent.percent = 0;
        silent.voice = ""sv;
        silent.voiceSound = nullptr;
        silent.soundLip = false;
        silent.speakerIdle = nullptr;
        silent.listenIdle = nullptr;
        silent.useEmotion = false;
        return silent;
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
        _origCharQSpeakingDone = charVtbl.write_vfunc(0x107, &Hook_Character_QSpeakingDone);

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerUpdateInDialogue = playerVtbl.write_vfunc(0x4C, &Hook_Player_UpdateInDialogue);
        _origPlayerQSpeakingDone = playerVtbl.write_vfunc(0x107, &Hook_Player_QSpeakingDone);
        _origPlayerSetSpeakingDone = playerVtbl.write_vfunc(0x108, &Hook_Player_SetSpeakingDone);

        SKSE::log::info("Hooked Character (0x41, 0x4C, 0x107) and PlayerCharacter (0x4C, 0x107, 0x108) dialogue vfuncs");
    }

    void DialogueManager::Update() {
        if (_puppetLineActive) {
            TickPuppetLine();
        }
        if (_playerLineActive) {
            TickPlayerLine();
        }
        UpdateIdleChatter();
    }

    void DialogueManager::Clear() {
        _inSelfDialogue.store(false, std::memory_order_release);
        _puppetLineActive = false;
        _playerLineActive = false;
        _playerLineIsIdleChatter = false;
        _idleTopicsCached = false;
        _idleTopics.clear();
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

    void DialogueManager::EndSelfDialogue() {
        SKSE::log::info("Self-dialogue ended");
        _inSelfDialogue.store(false, std::memory_order_release);
        // Factions the puppet picked up in conversation (e.g. recruited as a follower) belong to the donor
        ImpersonationManager::GetSingleton().ForwardPuppetFactionsToDonor();
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
        return _origSetDialogueWithPlayer(a_this, a_flag, a_forceGreet, a_topic);
    }

    // --- Puppet path ---

    bool DialogueManager::SpeakThroughPuppet(RE::Actor* a_puppet, RE::DialogueResponse* a_response, bool a_unused) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        SKSE::log::info("Puppet line: '{}' (voice='{}')", a_response->text.c_str(), a_response->voice.c_str());

        if (_playerLineActive) {
            FinishPlayerLine();
        }
        StopSounds(player);

        // The puppet speaks natively: audio, timing and topic advance stay with the engine
        bool ret = _origCharUpdateInDialogue(a_puppet, a_response, a_unused);

        // The player mouths the same line with its own copy of the audio muted
        _origCharUpdateInDialogue(player, a_response, a_unused);
        SetSoundVolume(player, 0.0f);
        if (a_response->useEmotion) {
            player->SetEmotionType(a_response->animFaceArchType.get());
            player->SetEmotionValue(a_response->percent);
        }

        // The audio comes from the player, and the puppet (the Dragonborn) listens instead of mouthing
        MoveSoundsToHead(a_puppet, player);
        ResetFace(a_puppet);

        _puppetLineActive = true;
        _puppetLineStart = std::chrono::steady_clock::now();
        return ret;
    }

    void DialogueManager::TickPuppetLine() {
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!puppet) {
            FinishPuppetLine();
            return;
        }

        // Keep the player's lip sync running; the engine only ticks the puppet's line
        _origCharUpdateInDialogue(player, nullptr, false);
        SetSoundVolume(player, 0.0f);

        if (SecondsSince(_puppetLineStart) >= kLineStartGraceSeconds && _origCharQSpeakingDone(puppet)) {
            FinishPuppetLine();
        }
    }

    void DialogueManager::FinishPuppetLine() {
        _puppetLineActive = false;
        auto* player = RE::PlayerCharacter::GetSingleton();
        StopSounds(player);
        ResetFace(player);
    }

    // --- Player path ---

    bool DialogueManager::SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        SKSE::log::info("Player line: '{}' (voice='{}', idleChatter={})",
            a_response->text.c_str(), a_response->voice.c_str(), a_isIdleChatter);

        if (_puppetLineActive) {
            FinishPuppetLine();
        }
        StopSounds(player);

        if (a_response->useEmotion) {
            player->SetEmotionType(a_response->animFaceArchType.get());
            player->SetEmotionValue(a_response->percent);
        }
        QueueSubtitle(player, a_response);

        // PlayerCharacter's own UpdateInDialogue doesn't voice lines; the Character version does
        player->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
        bool ret = _origCharUpdateInDialogue(player, a_response, a_unused);
        MoveSoundsToHead(player, player);

        float textLen = static_cast<float>(a_response->text.length());
        _playerLineUnvoicedDuration = std::max(kUnvoicedMinSeconds, textLen * kUnvoicedSecondsPerChar);
        _playerLineHadSound = false;
        _playerLineIsIdleChatter = a_isIdleChatter;
        _playerLineStart = std::chrono::steady_clock::now();
        _playerLineActive = true;
        return ret;
    }

    void DialogueManager::TickPlayerLine() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        _origCharUpdateInDialogue(player, nullptr, false);

        float elapsed = SecondsSince(_playerLineStart);
        if (IsSoundActive(player)) {
            _playerLineHadSound = true;
            if (elapsed < kMaxLineSeconds) return;
            SKSE::log::warn("Player line still reports sound after {:.0f}s; finishing it", elapsed);
        } else if (elapsed < kLineStartGraceSeconds || (!_playerLineHadSound && elapsed < _playerLineUnvoicedDuration)) {
            return;
        }

        SKSE::log::info("Player line finished after {:.2f}s (voiced={})", elapsed, _playerLineHadSound);
        FinishPlayerLine();
    }

    void DialogueManager::FinishPlayerLine() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        _playerLineActive = false;
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

    // --- Hooks ---

    bool DialogueManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!a_this || !impMgr.IsActive()) {
            return _origCharUpdateInDialogue(a_this, a_response, a_unused);
        }
        auto& self = DialogueManager::GetSingleton();

        if (a_this == impMgr.GetPuppet() && a_response) {
            if (self.IsInSelfDialogue()) {
                return self.SpeakThroughPuppet(a_this, a_response, a_unused);
            }
            // Outside self-dialogue the puppet is the silent Dragonborn
            SKSE::log::info("Suppressing puppet line outside self-dialogue: '{}'", a_response->text.c_str());
            return false;
        }

        if (a_this == impMgr.GetDonor() && a_response) {
            // The hidden donor's lines are voiced by the player; the donor runs a silent copy so the
            // engine's bookkeeping for it stays consistent. Its speaking-done follows the player's line.
            self.SpeakAsPlayer(a_response, a_unused, false);
            auto silent = MakeSilentCopy(a_response);
            return _origCharUpdateInDialogue(a_this, &silent, a_unused);
        }

        return _origCharUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool DialogueManager::Hook_Character_QSpeakingDone(const RE::Actor* a_this) {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (a_this && impMgr.IsActive() && a_this == impMgr.GetDonor()) {
            return !DialogueManager::GetSingleton()._playerLineActive;
        }
        return _origCharQSpeakingDone(a_this);
    }

    bool DialogueManager::Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (a_this && a_response && impMgr.IsActive()) {
            // Talking to a regular NPC: the player's prompt is never voiced
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            auto* puppet = impMgr.GetPuppet();
            if (topicMgr && topicMgr->speaker && (!puppet || topicMgr->speaker != puppet->GetHandle())) {
                return _origPlayerUpdateInDialogue(a_this, nullptr, a_unused);
            }
            // A scene line for one of the donor's swapped aliases
            return DialogueManager::GetSingleton().SpeakAsPlayer(a_response, a_unused, false);
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

    void DialogueManager::UpdateIdleChatter() {
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!impMgr.IsActive() || !impMgr.IsNPCVoiceEnabled() || _playerLineActive || _puppetLineActive) return;
        if (SecondsSince(_lastIdleChatterTime) < _idleChatterTimeSeconds || SecondsSince(_lastIdleEvalTime) < 1.5f) return;
        _lastIdleEvalTime = std::chrono::steady_clock::now();

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* donor = impMgr.GetDonor();
        if (!player || !donor || IsInSelfDialogue() || player->IsInCombat() || IsBlockingMenuOpen()) return;

        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr && (topicMgr->isGreetingPlayer || topicMgr->menuOpen || topicMgr->speaker)) return;

        if (!_idleTopicsCached) {
            RebuildIdleTopicCache();
        }

        // Conditions run with the player as subject (condition hooks make it test as the donor)
        // and the puppet, standing in for the Dragonborn, as target
        auto* puppet = impMgr.GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(player);

        struct Candidate {
            RE::TESQuest* quest;
            RE::TESTopic* topic;
            RE::TESTopicInfo* info;
        };
        std::vector<Candidate> candidates;

        for (const auto& [quest, topic] : _idleTopics) {
            if (!quest->IsRunning()) continue;
            if (quest->objConditions.head && !quest->objConditions.IsTrue(player, targetRef)) continue;

            for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                auto* info = topic->topicInfos[i];
                if (!info) continue;
                if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRequiresPlayerActivation)) continue;
                if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) continue;
                if (info->objConditions.head && !info->objConditions.IsTrue(player, targetRef)) continue;
                candidates.push_back({ quest, topic, info });
            }
        }

        if (candidates.empty()) return;

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
        const auto& chosen = candidates[dist(rng)];

        // Build the line as the donor would say it, so the voice path resolves to the donor's voice type
        auto dialogueItem = RE::make_smart<RE::DialogueItem>(chosen.quest, chosen.topic, chosen.info, donor);
        if (!dialogueItem || dialogueItem->responses.empty()) return;
        auto* response = dialogueItem->responses.front();
        if (!response) return;

        SKSE::log::info("Idle chatter: topic {:08X}, info {:08X}", chosen.topic->GetFormID(), chosen.info->GetFormID());
        SpeakAsPlayer(response, false, true);
        if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
            chosen.info->saidOnce = true;
        }
        _lastIdleChatterTime = std::chrono::steady_clock::now();
    }

}
