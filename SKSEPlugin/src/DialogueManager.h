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

        static bool Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        using UpdateInDialogue_t = bool(RE::Actor*, RE::DialogueResponse*, bool);
        inline static REL::Relocation<UpdateInDialogue_t> _origCharUpdateInDialogue;

        // The engine's call that plays a speaker's talking idle (gestures): skipped for the puppet
        static bool Hook_TalkingIdle(RE::AIProcess* a_process, RE::Actor* a_actor, RE::DEFAULT_OBJECT a_action,
            RE::TESIdleForm* a_idle, bool a_arg5, bool a_arg6, RE::TESObjectREFR* a_target);
        inline static REL::Relocation<decltype(Hook_TalkingIdle)> _origTalkingIdle;

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
    };

}
