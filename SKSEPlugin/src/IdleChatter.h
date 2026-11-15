#pragma once

#include "GameClock.h"
#include "PlayerBusy.h"
#include <unordered_map>
#include <vector>
#include "RE/D/DialogueItem.h"

namespace InTheirShoes {

    // Picks idle lines for the impersonating player to say as the donor, and decides when (the interval is
    // Settings::GetIdleChatterInterval).
    // PlayerVoice voices them. Not thread-safe.
    class IdleChatter : public REX::Singleton<IdleChatter> {
    public:
        // The idle topic list is rebuilt the next time a line is picked
        void InvalidateTopicCache() { _topicsCached = false; }
        void Clear();

        // A line for the player to say now, or null when it isn't time, the player is busy, or no idle line's
        // conditions pass. Only called while the player isn't voicing another line.
        RE::BSTSmartPointer<RE::DialogueItem> Poll();
        // The interval counts from the end of the last line
        void OnLineFinished() { _lastLineTime = GameClock::Now(); }

    private:
        friend REX::Singleton<IdleChatter>;
        IdleChatter() = default;
        ~IdleChatter() = default;

        void RebuildTopicCache();
        RE::BSTSmartPointer<RE::DialogueItem> PickLine(RE::Actor* a_player, RE::Actor* a_donor);
        // The info the engine would pick from a_topic, or null when none can be said
        RE::TESTopicInfo* PickInfo(RE::TESQuest* a_quest, RE::TESTopic* a_topic, RE::TESObjectREFR* a_subject,
            RE::TESObjectREFR* a_target) const;
        bool CanSay(const RE::TESTopicInfo* a_info) const;

        struct IdleTopic {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
        };
        bool _topicsCached{ false };
        // In quest priority order, highest first
        std::vector<IdleTopic> _topics;
        // When an info with reset hours was last said, in game hours (RE::Calendar::GetHoursPassed)
        std::unordered_map<RE::FormID, float> _lastSaidHours;
        PlayerBusy _blockedBy{ PlayerBusy::kNo };
        bool _foundNothing{ false };
        // GameClock times, so menus and pauses don't count toward the interval
        GameClock::TimePoint _lastLineTime{ GameClock::Now() };
        GameClock::TimePoint _lastEvalTime{};
    };

}
