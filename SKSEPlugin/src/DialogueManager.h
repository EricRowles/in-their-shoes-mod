#pragma once

#include "Singleton.h"
#include <atomic>
#include <chrono>
#include <optional>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Routes dialogue so the impersonating player speaks the donor's lines.
    //
    // Every line is voiced once, by the player. When the engine's speaker is a stand-in for the donor
    // (the puppet in self-dialogue, or the hidden donor itself), that speaker runs the line silenced and
    // is held "speaking" until the player's line ends, so the engine waits for the player.
    // The player's own lines (scenes on swapped aliases) and idle chatter (picked by IdleChatter) are voiced
    // the same way.
    //
    // Hooks run on the main thread. Whether self-dialogue is on is published in Session, which condition hooks
    // read from AI threads.
    class DialogueManager :
        public Singleton<DialogueManager>,
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::InputEvent*>,
        public RE::BSTEventSink<RE::TESTopicInfoEvent> {
    public:
        void Initialize();
        void Update();
        void Clear();

        // BSTEventSink<RE::MenuOpenCloseEvent>
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

        // BSTEventSink<RE::InputEvent*>: skipping self-dialogue lines
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        // BSTEventSink<RE::TESTopicInfoEvent>: logs the puppet's topic infos starting and ending
        RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>* a_eventSource) override;

        // Self-dialogue: the player talking to the puppet, seeing the donor's topics
        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr, RE::TESTopic* a_forceGreetTopic = nullptr);


    private:
        friend Singleton<DialogueManager>;
        DialogueManager() = default;
        ~DialogueManager() override = default;

        void InstallHooks();
        bool OpenSelfDialogueDirect(RE::Actor* a_speaker, RE::TESTopic* a_forceGreetTopic = nullptr);
        void EndSelfDialogue();
        void UpdatePuppetSpeakerClear();

        // Player voice
        void SpeakAsPlayer(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter);
        void StartPlayerVoiceLate();
        void TickPlayerLine();
        void FinishPlayerLine();

        // Stand-in speakers (the puppet in self-dialogue, the hidden donor)
        void HoldStandIn(RE::Actor* a_standIn, RE::DialogueResponse* a_response);
        static void ReleaseStandIn(RE::ActorHandle a_standIn, const RE::DialogueResponse* a_response);

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
        std::atomic<bool> _pendingSelfDialogue{ false };
        std::atomic<RE::TESTopic*> _pendingSelfDialogueTopic{ nullptr };
        // Set when self-dialogue ends; the puppet is cleared as the engine's speaker shortly after the menu closes
        std::atomic<bool> _puppetSpeakerClearPending{ false };
        // When the menu was first seen closed, once it has
        std::optional<std::chrono::steady_clock::time_point> _puppetSpeakerClearAt;

        // The line the player is voicing. It exists only while the line plays, and is dropped as a whole
        // when it ends.
        struct PlayerLine {
            // The stand-in whose line it is (if any), and the engine's response the stand-in runs
            RE::ActorHandle standIn;
            RE::DialogueResponse* standInResponse{ nullptr };
            // Set while the line's voice hasn't started yet (see StartPlayerVoiceLate)
            RE::DialogueResponse* pendingVoice{ nullptr };
            // Owns the response of a line we built ourselves (idle chatter)
            RE::BSTSmartPointer<RE::DialogueItem> item;
            bool isIdleChatter{ false };
            bool hadSound{ false };
            bool waitsForMenu{ false };
            float unvoicedDuration{ 0.0f };
            float soundSeconds{ 0.0f };
            // Time the sound could be heard since it started, and since its length became known (see TickPlayerLine)
            float soundActive{ 0.0f };
            float soundPlayed{ 0.0f };
            // Time between ticks not counted as playback (game paused, loading screens, long frames)
            float soundPaused{ 0.0f };
            std::chrono::steady_clock::time_point start{};
            std::chrono::steady_clock::time_point lastSoundTick{};
            std::chrono::steady_clock::time_point lastVoiceRetry{};
        };
        std::optional<PlayerLine> _line;
        // The player's copy of a stand-in's line (the stand-in's own response is silenced). Kept outside
        // PlayerLine: the line being voiced points at it, and replacing a line must not free it.
        RE::DialogueResponse _standInLine;
    };

}
