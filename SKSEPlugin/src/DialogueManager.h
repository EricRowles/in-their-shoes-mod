#pragma once

#include <atomic>
#include <chrono>
#include <vector>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Routes dialogue so the impersonating player speaks the donor's lines.
    //
    // Every line is voiced once, by the player. When the engine's speaker is a stand-in for the donor
    // (the puppet in self-dialogue, or the hidden donor itself), that speaker runs the line silenced and
    // is held "speaking" until the player's line ends, so the engine waits for the player.
    // The player's own lines (scenes on swapped aliases) and idle chatter are voiced the same way.
    //
    // Hooks run on the main thread, except IsInSelfDialogue(), which condition hooks may call from AI threads.
    class DialogueManager :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static DialogueManager& GetSingleton() {
            static DialogueManager instance;
            return instance;
        }

        void Initialize();
        void Update();
        void Clear();

        // BSTEventSink<RE::MenuOpenCloseEvent>
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

        // BSTEventSink<RE::InputEvent*>: skipping self-dialogue lines
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        // Self-dialogue: the player talking to the puppet, seeing the donor's topics
        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr, RE::TESTopic* a_forceGreetTopic = nullptr);
        bool IsInSelfDialogue() const { return _inSelfDialogue.load(std::memory_order_acquire); }

        // Idle chatter
        void SetIdleChatterTime(float a_seconds) { _idleChatterTimeSeconds = a_seconds < 10.0f ? 60.0f : a_seconds; }
        void InvalidateIdleQuestCache() { _idleTopicsCached = false; }

    private:
        DialogueManager() = default;
        ~DialogueManager() override = default;
        DialogueManager(const DialogueManager&) = delete;
        DialogueManager& operator=(const DialogueManager&) = delete;

        void InstallHooks();
        bool OpenSelfDialogueDirect(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic = nullptr);
        void EndSelfDialogue();

        // Player voice
        void SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter);
        void StartPlayerVoiceLate();
        void TickPlayerLine();
        void FinishPlayerLine();

        // Stand-in speakers (the puppet in self-dialogue, the hidden donor)
        void HoldStandIn(RE::Actor* a_standIn);
        void ReleaseStandIn();

        // Idle chatter
        void RebuildIdleTopicCache();
        void UpdateIdleChatter();
        RE::BSTSmartPointer<RE::DialogueItem> PickIdleLine(RE::Actor* a_player, RE::Actor* a_donor);

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

        static void Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set);
        using SetSpeakingDone_t = void(RE::Actor*, bool);
        inline static REL::Relocation<SetSpeakingDone_t> _origPlayerSetSpeakingDone;

        static bool Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic);
        using SetDialogueWithPlayer_t = bool(RE::Actor*, bool, bool, RE::TESTopicInfo*);
        inline static REL::Relocation<SetDialogueWithPlayer_t> _origSetDialogueWithPlayer;

        bool _initialized{ false };
        std::atomic<bool> _inSelfDialogue{ false };
        std::atomic<bool> _pendingSelfDialogue{ false };
        std::atomic<RE::TESTopic*> _pendingSelfDialogueTopic{ nullptr };

        // Line the player is voicing, and the stand-in whose line it is (if any)
        bool _playerLineActive{ false };
        RE::ActorHandle _standIn;
        RE::DialogueResponse* _standInResponse{ nullptr };  // the engine's response the stand-in runs
        // The player's copy of the stand-in's line (the stand-in's own response is silenced)
        RE::DialogueResponse _standInLine;
        bool _playerLineIsIdleChatter{ false };
        bool _playerLineHadSound{ false };
        bool _playerLineWaitsForMenu{ false };
        // Set while the line's voice hasn't started yet (see StartPlayerVoiceLate)
        RE::DialogueResponse* _playerLineResponse{ nullptr };
        // Owns the response of a line we built ourselves (idle chatter)
        RE::BSTSmartPointer<RE::DialogueItem> _playerLineItem;
        float _playerLineUnvoicedDuration{ 0.0f };
        float _playerLineSoundSeconds{ 0.0f };
        std::chrono::steady_clock::time_point _playerLineStart{};
        std::chrono::steady_clock::time_point _playerLineSoundStart{};
        std::chrono::steady_clock::time_point _lastVoiceRetry{};

        // Idle chatter
        struct IdleTopic {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
        };
        bool _idleTopicsCached{ false };
        std::vector<IdleTopic> _idleTopics;
        float _idleChatterTimeSeconds{ 60.0f };
        const char* _idleChatterBlockedBy{ nullptr };
        bool _idleChatterFoundNothing{ false };
        std::chrono::steady_clock::time_point _lastIdleChatterTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastIdleEvalTime{};
    };

}
