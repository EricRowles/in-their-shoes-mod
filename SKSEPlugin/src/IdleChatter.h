#pragma once

#include <chrono>
#include <vector>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Picks idle lines for the impersonating player to say as the donor, and decides when.
    // DialogueManager voices them. Main thread only.
    class IdleChatter {
    public:
        static IdleChatter& GetSingleton() {
            static IdleChatter instance;
            return instance;
        }

        // Seconds between lines (MCM)
        void SetInterval(float a_seconds) { _intervalSeconds = a_seconds < 10.0f ? 60.0f : a_seconds; }
        // The idle topic list is rebuilt the next time a line is picked
        void InvalidateTopicCache() { _topicsCached = false; }
        void Clear();

        // A line for the player to say now, or null when it isn't time, the player is busy, or no idle line's
        // conditions pass. Only called while the player isn't voicing another line.
        RE::BSTSmartPointer<RE::DialogueItem> Poll();
        // The interval counts from the end of the last line
        void OnLineFinished() { _lastLineTime = std::chrono::steady_clock::now(); }

    private:
        IdleChatter() = default;
        ~IdleChatter() = default;
        IdleChatter(const IdleChatter&) = delete;
        IdleChatter& operator=(const IdleChatter&) = delete;

        void RebuildTopicCache();
        RE::BSTSmartPointer<RE::DialogueItem> PickLine(RE::Actor* a_player, RE::Actor* a_donor);

        struct IdleTopic {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
        };
        bool _topicsCached{ false };
        std::vector<IdleTopic> _topics;
        float _intervalSeconds{ 60.0f };
        const char* _blockedBy{ nullptr };
        bool _foundNothing{ false };
        std::chrono::steady_clock::time_point _lastLineTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastEvalTime{};
    };

}
