#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include "RE/B/BSTSmartPointer.h"
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    class DialogueManager : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static DialogueManager& GetSingleton() {
            static DialogueManager instance;
            return instance;
        }

        void Initialize();
        void InstallHooks();

        // BSTEventSink<RE::MenuOpenCloseEvent>
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

        // Self-Dialogue
        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr);
        bool OpenSelfDialogueDirect(RE::Actor* a_speaker);
        bool IsInSelfDialogue() const;
        void SetInSelfDialogue(bool a_inSelf) { _inSelfDialogue.store(a_inSelf, std::memory_order_release); }
        void EndSelfDialogue();

        // Idle Chatter Configuration & Scanning
        void SetIdleChatterTime(float a_seconds) {
            if (a_seconds < 10.0f) {
                a_seconds = 60.0f;
            }
            _idleChatterTimeSeconds.store(a_seconds, std::memory_order_release);
        }
        float GetIdleChatterTime() const { return _idleChatterTimeSeconds.load(std::memory_order_acquire); }

        void OnIdleChatterLineCompleted() {
            _lastIdleChatterTime = std::chrono::steady_clock::now();
        }

        void InvalidateIdleQuestCache() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _cachedIdleDonor = nullptr;
            _cachedIdleQuests.clear();
        }

        void UpdateIdleChatter();
        bool CheckAndTriggerIdleDialogue();

        // Speech & Playback
        void OnDialogueResponseStarted(RE::DialogueResponse* a_response);
        bool IsPlayerSpeaking() const { return _playerSpeaking.load(std::memory_order_acquire); }
        void SetPlayerSpeaking(bool a_speaking) { _playerSpeaking.store(a_speaking, std::memory_order_release); }
        void TickSpeech(RE::Actor* a_player, bool a_menuOpen);
        void CompleteSpeaking(RE::Actor* a_player);
        bool IsInCompleteSpeaking() const { return _inCompleteSpeaking.load(std::memory_order_acquire); }
        bool CheckPlayerSpeakingDone();
        void ResetSpeechTracking();

        bool SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused);
        bool PlayIdleDialogueLine(RE::DialogueResponse* a_response, RE::TESTopicInfo* a_info);
        bool HandleProxyDialogueOutput(RE::Actor* a_speaker, RE::DialogueResponse* a_response, bool a_unused);

        static void QueueSubtitle(RE::Actor* a_speaker, const RE::DialogueResponse* a_response);
        static void SetSpeechBits(RE::Actor* a_actor, bool a_speakingDone, bool a_filesDone);
        static bool HasPlayingSound(RE::Actor* a_actor);
        static void AttachSoundHandlesToHead(RE::Actor* a_actor);
        bool TryCreateDialogueSoundHandle(RE::Actor* a_actor, RE::DialogueResponse* a_response);

        void Clear() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _inSelfDialogue.store(false, std::memory_order_release);
            _playerSpeaking.store(false, std::memory_order_release);
            _inCompleteSpeaking.store(false, std::memory_order_release);
            _speechHadSound.store(false, std::memory_order_release);
            _isPlayingIdleChatter.store(false, std::memory_order_release);
            _cachedIdleDonor = nullptr;
            _cachedIdleQuests.clear();
            _lastIdleEvalTime = {};
            _lastIdleChatterTime = std::chrono::steady_clock::now();
        }

    private:
        DialogueManager() = default;
        ~DialogueManager() override = default;
        DialogueManager(const DialogueManager&) = delete;
        DialogueManager& operator=(const DialogueManager&) = delete;

        void RebuildIdleQuestCache(RE::Actor* a_donor, RE::Actor* a_player);

        // VFunc Hooks
        static bool Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        using UpdateInDialogue_t = bool(RE::Actor*, RE::DialogueResponse*, bool);
        inline static REL::Relocation<UpdateInDialogue_t> _origCharUpdateInDialogue;

        static bool Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        inline static REL::Relocation<UpdateInDialogue_t> _origPlayerUpdateInDialogue;

        static bool Hook_Character_QSpeakingDone(const RE::Actor* a_this);
        using QSpeakingDone_t = bool(const RE::Actor*);
        inline static REL::Relocation<QSpeakingDone_t> _origCharQSpeakingDone;

        static bool Hook_Player_QSpeakingDone(const RE::Actor* a_this);
        inline static REL::Relocation<QSpeakingDone_t> _origPlayerQSpeakingDone;

        static void Hook_Character_SetSpeakingDone(RE::Actor* a_this, bool a_set);
        using SetSpeakingDone_t = void(RE::Actor*, bool);
        inline static REL::Relocation<SetSpeakingDone_t> _origCharSetSpeakingDone;

        static void Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set);
        inline static REL::Relocation<SetSpeakingDone_t> _origPlayerSetSpeakingDone;

        static bool Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic);
        using SetDialogueWithPlayer_t = bool(RE::Actor*, bool, bool, RE::TESTopicInfo*);
        inline static REL::Relocation<SetDialogueWithPlayer_t> _origSetDialogueWithPlayer;

        mutable std::recursive_mutex _mutex;
        bool _initialized{ false };
        bool _hooksInstalled{ false };

        std::atomic<bool> _inSelfDialogue{ false };
        std::atomic<bool> _playerSpeaking{ false };
        std::atomic<bool> _inCompleteSpeaking{ false };
        std::atomic<bool> _speechHadSound{ false };
        std::atomic<bool> _isPlayingIdleChatter{ false };

        RE::Actor* _cachedIdleDonor{ nullptr };
        std::vector<RE::TESQuest*> _cachedIdleQuests;

        std::atomic<float> _idleChatterTimeSeconds{ 60.0f };
        std::chrono::steady_clock::time_point _lastIdleChatterTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastIdleEvalTime{};

        std::chrono::steady_clock::time_point _speechStartTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _speechAudioEndTime{};
        float _speechMinDuration{ 2.0f };
    };

}
