#include "PCH.h"
#include "IdleChatter.h"
#include "Forms.h"
#include "QuestConditions.h"
#include "Session.h"

#include <random>

#include "RE/T/TESDataHandler.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"

namespace InTheirShoes {

    namespace {
        // How often a line is looked for once the interval has passed
        constexpr float kEvalIntervalSeconds = 1.5f;

        float SecondsSince(std::chrono::steady_clock::time_point a_time) {
            return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_time).count();
        }

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
        SKSE::log::info("Cached {} idle dialogue topics.", _topics.size());
    }

    RE::BSTSmartPointer<RE::DialogueItem> IdleChatter::PickLine(RE::Actor* a_player, RE::Actor* a_donor) {
        if (!_topicsCached) {
            RebuildTopicCache();
        }

        // Conditions run with the player as subject (condition hooks make it test as the donor)
        // and the puppet, standing in for the Dragonborn, as target
        auto* puppet = Session::GetSingleton().GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(a_player);

        struct Candidate {
            RE::TESQuest* quest;
            RE::TESTopic* topic;
            RE::TESTopicInfo* info;
        };
        std::vector<Candidate> candidates;

        for (const auto& [quest, topic] : _topics) {
            if (!IsQuestRunning(quest)) continue;
            if (!QuestConditions::Evaluate(quest->objConditions, quest, a_player, targetRef)) continue;

            for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                auto* info = topic->topicInfos[i];
                if (!info) continue;
                if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRequiresPlayerActivation)) continue;
                if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) continue;
                if (!QuestConditions::Evaluate(info->objConditions, quest, a_player, targetRef)) continue;
                candidates.push_back({ quest, topic, info });
            }
        }

        if (candidates.empty()) return nullptr;

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
        const auto& chosen = candidates[dist(rng)];

        // Build the line as the donor would say it, so the voice path resolves to the donor's voice type
        auto item = RE::make_smart<RE::DialogueItem>(chosen.quest, chosen.topic, chosen.info, a_donor);
        if (!item || item->responses.empty() || !item->responses.front()) return nullptr;

        SKSE::log::info("Idle chatter: picked topic {:08X}, info {:08X}", chosen.topic->GetFormID(), chosen.info->GetFormID());
        if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
            chosen.info->saidOnce = true;
        }
        return item;
    }

    RE::BSTSmartPointer<RE::DialogueItem> IdleChatter::Poll() {
        auto& session = Session::GetSingleton();
        if (!session.IsActive() || !session.IsNPCVoiceEnabled()) return nullptr;
        if (SecondsSince(_lastLineTime) < _intervalSeconds || SecondsSince(_lastEvalTime) < kEvalIntervalSeconds) return nullptr;
        _lastEvalTime = std::chrono::steady_clock::now();

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* donor = session.GetDonor();
        if (!player || !donor) return nullptr;

        // MenuTopicManager's fields (speaker, menuOpen, isGreetingPlayer) stay set after a conversation
        // ends, so only the open menu counts as one
        const char* blockedBy = nullptr;
        if (session.IsInSelfDialogue()) {
            blockedBy = "self-dialogue";
        } else if (player->IsInCombat()) {
            blockedBy = "combat";
        } else if (IsBlockingMenuOpen()) {
            blockedBy = "menu";
        }
        if (blockedBy != _blockedBy) {
            if (blockedBy) {
                SKSE::log::info("Idle chatter waiting: {}", blockedBy);
            }
            _blockedBy = blockedBy;
        }
        if (blockedBy) return nullptr;

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
