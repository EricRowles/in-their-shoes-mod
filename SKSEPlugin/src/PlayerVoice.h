#pragma once

#include "Singleton.h"
#include <chrono>
#include <functional>
#include <optional>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Plays one line at a time as the player: voice, lip sync, timing and when the line is done.
    //
    // A line is spoken for one of three sources: a scene line on a swapped alias (Speak), idle chatter
    // (SpeakIdleChatter), or a stand-in's line (SpeakForStandIn). Update() ticks the line and ends it.
    //
    // The stand-in hold lives here, not in DialogueManager, because it is tied to the line's lifetime: it begins
    // with the line, is kept alive while the line plays, and is released when the line ends. DialogueManager
    // learns of the release through the handler it registers (SetStandInDoneHandler) and does the menu
    // side, which needs MenuTopicManager and topic-info tracking.
    //
    // The engine's original UpdateInDialogue and SetSpeakingDone are hooked by DialogueManager; it hands them
    // over in SetEngineCalls.
    class PlayerVoice : public Singleton<PlayerVoice> {
    public:
        struct EngineCalls {
            // Character::UpdateInDialogue run on the given actor, bypassing our hook
            std::function<void(RE::Actor*, RE::DialogueResponse*, bool)> updateInDialogue;
            // PlayerCharacter::SetSpeakingDone, bypassing our hook
            std::function<void(RE::Actor*, bool)> setSpeakingDone;
        };
        void SetEngineCalls(EngineCalls a_calls) { _engine = std::move(a_calls); }

        // Called when a stand-in's line ends and it has been released: a_response is the engine response the
        // stand-in ran
        using StandInDoneHandler = std::function<void(RE::Actor* a_standIn, const RE::DialogueResponse* a_response)>;
        void SetStandInDoneHandler(StandInDoneHandler a_handler) { _standInDone = std::move(a_handler); }

        // Per frame: ticks the playing line
        void Update();
        void Clear();

        // Whether a line is playing. Read from the engine's speaking hooks.
        bool IsPlaying() const { return _line.has_value(); }

        // A line for the player to voice (a scene line on a swapped alias)
        void Speak(RE::DialogueResponse* a_response, bool a_unused);

        // A line IdleChatter picked: sends the topic info's begin event and its end event when the line ends,
        // then reports back to IdleChatter. The item is kept for as long as the line plays.
        void SpeakIdleChatter(RE::BSTSmartPointer<RE::DialogueItem> a_item);

        // A stand-in's line, in two steps around the stand-in running its own response:
        //  1. TakeStandInLine copies the engine's response for the player and silences the engine's.
        //  2. SpeakForStandIn voices the copy and holds the stand-in as speaking until the line ends.
        void TakeStandInLine(RE::DialogueResponse* a_engineResponse);
        void SpeakForStandIn(RE::Actor* a_standIn, RE::DialogueResponse* a_engineResponse, bool a_unused);

        // From the stand-in's per-frame update: keeps it "speaking" while its line plays
        void KeepStandInSpeaking(RE::Actor* a_actor) const;

        // A stand-in's line that has been playing long enough to be skipped by the player
        bool IsSkippableStandInLine() const;

        // Ends the line now (skip, or something took over)
        void Finish();

    private:
        friend Singleton<PlayerVoice>;
        PlayerVoice() = default;
        ~PlayerVoice() = default;

        void SpeakLine(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter);
        void StartVoiceLate();
        void HoldStandIn(RE::Actor* a_standIn, RE::DialogueResponse* a_response);
        void ReleaseStandIn(RE::ActorHandle a_standIn, const RE::DialogueResponse* a_response);

        EngineCalls _engine;
        StandInDoneHandler _standInDone;

        // The line the player is voicing. It exists only while the line plays, and is dropped as a whole
        // when it ends.
        struct Line {
            // The stand-in whose line it is (if any), and the engine's response the stand-in runs
            RE::ActorHandle standIn;
            RE::DialogueResponse* standInResponse{ nullptr };
            // Set while the line's voice hasn't started yet (see StartVoiceLate)
            RE::DialogueResponse* pendingVoice{ nullptr };
            // Owns the response of a line we built ourselves (idle chatter)
            RE::BSTSmartPointer<RE::DialogueItem> item;
            bool isIdleChatter{ false };
            bool hadSound{ false };
            bool waitsForMenu{ false };
            float unvoicedDuration{ 0.0f };
            float soundSeconds{ 0.0f };
            // Time the sound could be heard since it started, and since its length became known (see Update)
            float soundActive{ 0.0f };
            float soundPlayed{ 0.0f };
            // Time between ticks not counted as playback (game paused, loading screens, long frames)
            float soundPaused{ 0.0f };
            std::chrono::steady_clock::time_point start{};
            std::chrono::steady_clock::time_point lastSoundTick{};
            std::chrono::steady_clock::time_point lastVoiceRetry{};
        };
        std::optional<Line> _line;
        // The player's copy of a stand-in's line (the stand-in's own response is silenced). Kept outside
        // Line: the line being voiced points at it, and replacing a line must not free it.
        RE::DialogueResponse _standInLine;
    };

}
