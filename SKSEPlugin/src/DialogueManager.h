#pragma once

#include <atomic>
#include <chrono>
#include <vector>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Routes dialogue so the impersonating player speaks the donor's lines.
    //
    // Two speech paths:
    //  - Puppet lines (self-dialogue): the puppet stays the engine's speaker and plays the line natively,
    //    so timing, menu advancing and speaking-done are all native. Its audio follows the player's head
    //    and the player runs the same line muted for lip sync.
    //  - Player lines (scenes on swapped aliases, donor lines, idle chatter): the player plays the line
    //    and we report speaking-done from the player's voice sound.
    //
    // Hooks run on the main thread, except IsInSelfDialogue(), which condition hooks may call from AI threads.
    class DialogueManager : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
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

        // Self-dialogue: the player talking to the puppet, seeing the donor's topics
        bool OpenSelfDialogue(RE::Actor* a_speaker);
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
        bool OpenSelfDialogueDirect(RE::Actor* a_speaker);
        void EndSelfDialogue();

        // Puppet path
        bool SpeakThroughPuppet(RE::Actor* a_puppet, RE::DialogueResponse* a_response, bool a_unused);
        void TickPuppetLine();
        void FinishPuppetLine();

        // Player path
        bool SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter);
        void TickPlayerLine();
        void FinishPlayerLine();

        // Idle chatter
        void RebuildIdleTopicCache();
        void UpdateIdleChatter();

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

        // Puppet line being mirrored onto the player
        bool _puppetLineActive{ false };
        std::chrono::steady_clock::time_point _puppetLineStart{};

        // Line the player is voicing
        bool _playerLineActive{ false };
        bool _playerLineIsIdleChatter{ false };
        bool _playerLineHadSound{ false };
        float _playerLineUnvoicedDuration{ 0.0f };
        std::chrono::steady_clock::time_point _playerLineStart{};

        // Idle chatter
        struct IdleTopic {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
        };
        bool _idleTopicsCached{ false };
        std::vector<IdleTopic> _idleTopics;
        float _idleChatterTimeSeconds{ 60.0f };
        std::chrono::steady_clock::time_point _lastIdleChatterTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastIdleEvalTime{};
    };

}
