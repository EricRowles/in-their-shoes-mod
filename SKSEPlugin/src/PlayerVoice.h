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
    // A stand-in's line is the engine's: the stand-in runs it unheard and the engine paces the conversation by
    // it. The player's copy only follows, ending when the stand-in's does.
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

        // Per frame: ticks the playing line
        void Update();
        void Clear();

        // Whether a line is playing. Read from the engine's speaking hooks.
        bool IsPlaying() const { return _line.has_value(); }
        // How long the playing line would show without a voice file (0 when no line is playing)
        float GetUnvoicedSeconds() const { return _line ? _line->unvoicedDuration : 0.0f; }
        // Whether the actor is playing a voice file whose length is known (it was found and loaded)
        static bool HasVoiceSound(RE::Actor* a_actor);

        // A line for the player to voice (a scene line on a swapped alias)
        void Speak(RE::DialogueResponse* a_response, bool a_unused);

        // A line IdleChatter picked: sends the topic info's begin event and its end event when the line ends,
        // then reports back to IdleChatter. The item is kept for as long as the line plays.
        void SpeakIdleChatter(RE::BSTSmartPointer<RE::DialogueItem> a_item);

        // A stand-in's line, in two steps around the stand-in running its own response:
        //  1. TakeStandInLine copies the engine's response for the player and strips the engine's of emotion
        //     and idles.
        //  2. SpeakForStandIn voices the copy, which ends when the stand-in's line does.
        void TakeStandInLine(RE::DialogueResponse* a_engineResponse);
        void SpeakForStandIn(RE::Actor* a_standIn, bool a_unused);

        // Makes the stand-in's voice inaudible and drops its lip sync. The engine may start the voice some
        // frames after the line, so this is called when the line starts and from the stand-in's per-frame update.
        static void SilenceStandIn(RE::Actor* a_actor);

        // Ends the line now (skip, or something took over)
        void Finish();

    private:
        friend Singleton<PlayerVoice>;
        PlayerVoice() = default;
        ~PlayerVoice() = default;

        void SpeakLine(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter);
        void StartVoiceLate();
        bool StandInLineDone();

        EngineCalls _engine;

        // The line the player is voicing. It exists only while the line plays, and is dropped as a whole
        // when it ends.
        struct Line {
            // Set for a stand-in's line: the line ends when the stand-in's does (see StandInLineDone)
            bool followsStandIn{ false };
            RE::ActorHandle standIn;
            std::optional<std::chrono::steady_clock::time_point> standInDoneAt;
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
            // When the line was asked for, and when the player's voice started (start moves if the line waits
            // for the menu)
            std::chrono::steady_clock::time_point created{};
            std::chrono::steady_clock::time_point soundStart{};
            std::chrono::steady_clock::time_point lastSoundTick{};
            std::chrono::steady_clock::time_point lastVoiceRetry{};
        };
        std::optional<Line> _line;
        // The player's copy of a stand-in's line. Kept outside
        // Line: the line being voiced points at it, and replacing a line must not free it.
        RE::DialogueResponse _standInLine;
    };

}
