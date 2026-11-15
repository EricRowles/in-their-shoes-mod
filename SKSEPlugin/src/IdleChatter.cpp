#include "PCH.h"
#include "IdleChatter.h"
#include "Forms.h"
#include "GameUtil.h"
#include "QuestConditions.h"
#include "Session.h"
#include "Settings.h"

#include <random>

#include "RE/T/TESDataHandler.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/C/Calendar.h"

#include <algorithm>
#include <functional>

namespace InTheirShoes {

    namespace {
        // How often a line is looked for once the interval has passed
        constexpr float kEvalIntervalSeconds = 1.5f;

        // Only kIdle: other "misc" subtypes such as kPickpocketTopic played the wrong lines
        bool IsIdleTopic(const RE::TESTopic* a_topic) {
            return a_topic->data.subtype.get() == RE::DIALOGUE_DATA::Subtype::kIdle;
        }
    }

    void IdleChatter::Clear() {
        _topicsCached = false;
        _topics.clear();
        _lastEvalTime = {};
        _lastLineTime = std::chrono::steady_clock::now();
        _lastSaidHours.clear();
    }

    void IdleChatter::RebuildTopicCache() {
        _topicsCached = true;
        _topics.clear();
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
        for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
            if (!quest || Forms::IsModQuest(quest)) continue;
            for (auto* topic : quest->topics[kMiscIndex]) {
                if (topic && topic->numTopicInfos > 0 && topic->topicInfos && IsIdleTopic(topic)) {
                    _topics.push_back({ quest, topic });
                }
            }
        }
        // As the engine picks: a higher priority quest's idle line comes first (#34)
        std::ranges::stable_sort(_topics, std::greater{}, [](const IdleTopic& a_topic) { return a_topic.quest->data.priority; });
        SKSE::log::info("Cached {} idle dialogue topics.", _topics.size());
    }

    bool IdleChatter::CanSay(const RE::TESTopicInfo* a_info) const {
        using Flag = RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS;
        if (a_info->data.flags.any(Flag::kRequiresPlayerActivation)) return false;
        if (a_info->saidOnce && a_info->data.flags.any(Flag::kSayOnce)) return false;
        // Not again until its reset hours have passed
        const auto hours = a_info->data.GetResetHours();
        auto said = _lastSaidHours.find(a_info->GetFormID());
        auto* calendar = RE::Calendar::GetSingleton();
        return hours <= 0.0f || said == _lastSaidHours.end() || !calendar || calendar->GetHoursPassed() - said->second >= hours;
    }

    RE::TESTopicInfo* IdleChatter::PickInfo(RE::TESQuest* a_quest, RE::TESTopic* a_topic, RE::TESObjectREFR* a_subject,
        RE::TESObjectREFR* a_target) const {
        // The engine's rule: the first passing info, except a run of Random infos (ended by Random End or a non-random
        // info) picks one of its passing infos at random (#34).
        using Flag = RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS;
        static std::mt19937 rng(std::random_device{}());
        std::vector<RE::TESTopicInfo*> group;
        auto pickFromGroup = [&]() -> RE::TESTopicInfo* {
            if (group.empty()) return nullptr;
            return group[std::uniform_int_distribution<std::size_t>(0, group.size() - 1)(rng)];
        };

        for (std::uint32_t i = 0; i < a_topic->numTopicInfos; ++i) {
            auto* info = a_topic->topicInfos[i];
            if (!info) continue;
            const bool random = info->data.flags.any(Flag::kRandom);
            if (!random) {
                if (auto* picked = pickFromGroup()) return picked;
                group.clear();
            }
            const bool passes = CanSay(info) && QuestConditions::Evaluate(info->objConditions, a_quest, a_subject, a_target);
            if (!random) {
                if (passes) return info;
                continue;
            }
            if (passes) group.push_back(info);
            if (info->data.flags.any(Flag::kRandomEnd)) {
                if (auto* picked = pickFromGroup()) return picked;
                group.clear();
            }
        }
        return pickFromGroup();
    }

    RE::BSTSmartPointer<RE::DialogueItem> IdleChatter::PickLine(RE::Actor* a_player, RE::Actor* a_donor) {
        if (!_topicsCached) {
            RebuildTopicCache();
        }

        // Conditions run with the player as subject (condition hooks make it test as the donor)
        // and the puppet, standing in for the Dragonborn, as target
        auto* puppet = Session::GetSingleton().GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(a_player);

        for (const auto& [quest, topic] : _topics) {
            if (!IsQuestRunning(quest)) continue;
            if (!QuestConditions::Evaluate(quest->objConditions, quest, a_player, targetRef)) continue;
            auto* info = PickInfo(quest, topic, a_player, targetRef);
            if (!info) continue;

            // Build the line as the donor would say it, so the voice path resolves to the donor's voice type
            auto item = RE::make_smart<RE::DialogueItem>(quest, topic, info, a_donor);
            if (!item || item->responses.empty() || !item->responses.front()) continue;

            SKSE::log::info("Idle chatter: picked topic {:08X}, info {:08X}", topic->GetFormID(), info->GetFormID());
            if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                info->saidOnce = true;
            }
            if (auto* calendar = RE::Calendar::GetSingleton(); calendar && info->data.GetResetHours() > 0.0f) {
                _lastSaidHours[info->GetFormID()] = calendar->GetHoursPassed();
            }
            return item;
        }
        return nullptr;
    }

    RE::BSTSmartPointer<RE::DialogueItem> IdleChatter::Poll() {
        auto& session = Session::GetSingleton();
        const auto& settings = Settings::GetSingleton();
        if (!session.IsActive() || !settings.IsNPCVoiceEnabled()) return nullptr;
        if (SecondsSince(_lastLineTime) < settings.GetIdleChatterInterval() || SecondsSince(_lastEvalTime) < kEvalIntervalSeconds) return nullptr;
        _lastEvalTime = std::chrono::steady_clock::now();

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* donor = session.GetDonor();
        if (!player || !donor) return nullptr;

        // MenuTopicManager's fields (speaker, menuOpen, isGreetingPlayer) stay set after a conversation
        // ends, so only the open menu counts as one
        auto blockedBy = PlayerBusy::kNo;
        if (session.IsInSelfDialogue()) {
            blockedBy = PlayerBusy::kSelfDialogue;
        } else if (player->IsInCombat()) {
            blockedBy = PlayerBusy::kCombat;
        } else if (IsBlockingMenuOpen()) {
            blockedBy = PlayerBusy::kMenu;
        } else if (GetPlayingScene(player)) {
            // Conditions can't rule this out: IsInScene answers no for the player (ConditionHooks)
            blockedBy = PlayerBusy::kScene;
        }
        LogPlayerBusy(_blockedBy, blockedBy, "Idle chatter"sv);
        if (blockedBy != PlayerBusy::kNo) return nullptr;

        auto item = PickLine(player, donor);
        if (!item) {
            if (!_foundNothing) {
                SKSE::log::info("Idle chatter waiting: no idle line's conditions pass");
                _foundNothing = true;
            }
            return nullptr;
        }
        _foundNothing = false;
        _lastLineTime = std::chrono::steady_clock::now();
        return item;
    }

}
