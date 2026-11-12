#pragma once

#include "Singleton.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <set>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Routes dialogue so the impersonating player speaks the donor's lines.
    //
    // Every line is voiced once, by the player (PlayerVoice does the playing). In self-dialogue the engine's
    // speaker is the puppet, standing in for the donor: it runs the line silenced and is held "speaking" until
    // the player's line ends, so the engine waits for the player. The player's own lines (scenes on swapped
    // aliases, and lines scripts give the donor, which NativeRedirect sends to the player) and idle chatter
    // (picked by IdleChatter) are voiced the same way. This class routes the engine's lines to PlayerVoice and
    // moves the Dialogue Menu on when a stand-in's line ends.
    //
    // Whether self-dialogue is on is published in Session, which condition hooks read from AI threads.
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

        // BSTEventSink<RE::TESTopicInfoEvent>: tracks the puppet's topic infos starting and ending (see EndPuppetInfo)
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

        // The stand-in's line ended (PlayerVoice released it): advances the Dialogue Menu past the response
        void OnStandInLineDone(RE::Actor* a_standIn, const RE::DialogueResponse* a_response);
        void EndPuppetInfo(RE::Actor* a_puppet, RE::TESTopicInfo* a_info);

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
        // After self-dialogue ends (Session::IsPuppetSpeakerPending), the puppet is cleared as the engine's speaker
        // shortly after the menu closes. When the menu was first seen closed, once it has
        std::optional<std::chrono::steady_clock::time_point> _puppetSpeakerClearAt;

        // The puppet's topic infos that began and haven't ended, and those we ended ourselves (EndPuppetInfo).
        // Topic info events come from any thread.
        std::mutex _puppetInfoLock;
        std::set<RE::FormID> _puppetInfosOpen;
        std::set<RE::FormID> _puppetInfosEndedByUs;
    };

}
