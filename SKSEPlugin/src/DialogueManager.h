#pragma once

#include "Singleton.h"
#include <atomic>
#include <chrono>
#include <optional>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Routes dialogue so the impersonating player speaks the donor's lines.
    //
    // Every line is heard once, from the player (PlayerVoice does the playing). In self-dialogue the engine's
    // speaker is the puppet, standing in for the donor: it runs the line unheard, so the engine paces the
    // conversation, and the player's copy of the line follows it. The player's own lines (scenes on swapped
    // aliases, and lines scripts give the donor, which NativeRedirect sends to the player) and idle chatter
    // (picked by IdleChatter) are voiced by the player alone. This class routes the engine's lines to PlayerVoice.
    //
    // Whether self-dialogue is on is published in Session, which condition hooks read from AI threads.
    class DialogueManager :
        public Singleton<DialogueManager>,
        public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        void Initialize();
        void Update();
        void Clear();

        // BSTEventSink<RE::MenuOpenCloseEvent>
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

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
        // Moves the menu past a puppet line that has no voice file (#37)
        void AdvancePastUnvoicedLine();

        static bool Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        using UpdateInDialogue_t = bool(RE::Actor*, RE::DialogueResponse*, bool);
        inline static REL::Relocation<UpdateInDialogue_t> _origCharUpdateInDialogue;

        // The engine's call that plays a speaker's talking idle (gestures): skipped for the puppet
        static bool Hook_TalkingIdle(RE::AIProcess* a_process, RE::Actor* a_actor, RE::DEFAULT_OBJECT a_action,
            RE::TESIdleForm* a_idle, bool a_arg5, bool a_arg6, RE::TESObjectREFR* a_target);
        inline static REL::Relocation<decltype(Hook_TalkingIdle)> _origTalkingIdle;

        // The engine's function that starts a line's voice, lip sync and talking idle on a speaker. Hooked at its
        // calls outside Character::UpdateInDialogue, which say lines without a conversation (see the definition).
        // Its arguments past the speaker are passed through untouched.
        using StartSpeech_t = float(RE::Actor*, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
            std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
        static float Hook_StartSpeech(RE::Actor* a_speaker, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5,
            std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9, std::uint64_t a10, std::uint64_t a11,
            std::uint64_t a12, std::uint64_t a13, std::uint64_t a14);
        inline static REL::Relocation<StartSpeech_t> _origStartSpeech;
        void InstallStartSpeechHooks(std::uintptr_t a_talkingIdleCall);
        // Ends a self-dialogue whose Dialogue Menu never opened (a one-line greeting)
        void EndSelfDialogueWithoutMenu();

        static bool Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        inline static REL::Relocation<UpdateInDialogue_t> _origPlayerUpdateInDialogue;

        static bool Hook_Player_QSpeakingDone(const RE::Actor* a_this);
        using QSpeakingDone_t = bool(const RE::Actor*);
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
        // After self-dialogue ends (Session::IsPuppetSpeakerPending), the puppet is cleared as the engine's speaker
        // shortly after the menu closes. When the menu was first seen closed, once it has
        std::optional<std::chrono::steady_clock::time_point> _puppetSpeakerClearAt;
        // The menu line waiting on a voice file that doesn't exist (see AdvancePastUnvoicedLine)
        struct UnvoicedLine {
            const RE::TESTopicInfo* info{ nullptr };
            std::chrono::steady_clock::time_point since{};
            bool advanced{ false };
        };
        std::optional<UnvoicedLine> _unvoicedLine;
        // When self-dialogue was opened, until its Dialogue Menu opens (main thread)
        std::optional<std::chrono::steady_clock::time_point> _awaitingMenuSince;
        static constexpr float kNoMenuSeconds = 2.0f;
        static constexpr float kUnvoicedLineMinSeconds = 1.5f;
    };

}
