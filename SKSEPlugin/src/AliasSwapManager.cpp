#include "PCH.h"
#include "AliasSwapManager.h"
#include "Forms.h"
#include "GameUtil.h"
#include "Session.h"

#include <algorithm>
#include <RE/B/BGSRefAlias.h>
#include <RE/E/ExtraAliasInstanceArray.h>
#include <RE/T/TESQuest.h>

namespace InTheirShoes {

    namespace {
        // The reference aliases a_actor fills in running quests (other than the mod's own)
        std::vector<std::pair<RE::TESQuest*, std::uint32_t>> GetFilledRefAliases(RE::Actor* a_actor) {
            std::vector<std::pair<RE::TESQuest*, std::uint32_t>> aliases;
            auto* aliasArray = a_actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!aliasArray) return aliases;

            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (!inst || !inst->quest || !inst->alias) continue;
                if (!IsQuestRunning(inst->quest) || Forms::IsModQuest(inst->quest)) continue;
                if (inst->alias->QType() != "Ref"sv) continue;

                auto* refAlias = static_cast<const RE::BGSRefAlias*>(inst->alias);
                if (refAlias->GetReference() == a_actor) {
                    aliases.emplace_back(inst->quest, refAlias->aliasID);
                }
            }
            return aliases;
        }

        // Moves a_from's alias to a_to, returning the factions a_from lost with it (the alias's factions)
        std::vector<AliasSwapManager::FactionRank> MoveAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID,
            RE::Actor* a_from, RE::Actor* a_to) {
            auto before = GetFactionMemberships(a_from);
            a_quest->ForceRefIntoAlias(a_aliasID, a_to);
            auto after = GetFactionMemberships(a_from);
            std::vector<AliasSwapManager::FactionRank> lost;
            for (const auto& [faction, rank] : before) {
                if (!after.contains(faction)) {
                    lost.emplace_back(faction, rank);
                    SKSE::log::info("  alias faction {:08X} (rank {})", faction->GetFormID(), rank);
                }
            }
            return lost;
        }

        RE::TESObjectREFR* GetAliasReference(RE::TESQuest* a_quest, std::uint32_t a_aliasID) {
            for (auto* alias : a_quest->aliases) {
                if (alias && alias->aliasID == a_aliasID) {
                    return alias->QType() == "Ref"sv ? static_cast<RE::BGSRefAlias*>(alias)->GetReference() : nullptr;
                }
            }
            return nullptr;
        }
    }

    void AliasSwapManager::SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        _swappedAliases.clear();
        for (const auto& [quest, aid] : GetFilledRefAliases(a_donor)) {
            SKSE::log::info("Swapped donor {:08X} to player in quest {:08X} alias ID {}",
                a_donor->GetFormID(), quest->GetFormID(), aid);
            _swappedAliases.push_back({ quest->GetFormID(), aid, MoveAlias(quest, aid, a_donor, a_player) });
        }
    }

    void AliasSwapManager::RestoreSwappedAliases(RE::Actor* a_donor) {
        auto* player = Session::GetPlayer();
        for (const auto& entry : _swappedAliases) {
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(entry.questFormID);
            if (!IsQuestRunning(quest) || !a_donor) {
                SKSE::log::info("Skipping quest {:08X} alias ID {} (quest stopped or no donor)",
                    entry.questFormID, entry.aliasID);
                continue;
            }

            auto* currentRef = GetAliasReference(quest, entry.aliasID);
            if (currentRef && currentRef != player && currentRef != a_donor) {
                SKSE::log::info("Alias ID {} in quest {:08X} now holds {:08X}, leaving it alone.",
                    entry.aliasID, entry.questFormID, currentRef->GetFormID());
                continue;
            }

            quest->ForceRefIntoAlias(entry.aliasID, a_donor);
            SKSE::log::info("Restored donor {:08X} into quest {:08X} alias ID {}",
                a_donor->GetFormID(), quest->GetFormID(), entry.aliasID);
        }
        // The aliases gave their factions back
        _swappedAliases.clear();
    }

    void AliasSwapManager::AdoptPuppetAliases(RE::Actor* a_puppet, RE::Actor* a_player) {
        if (!a_puppet || !a_player) return;

        auto toAdopt = GetFilledRefAliases(a_puppet);
        if (toAdopt.empty()) return;

        // Their factions count for the donor too, as in SwapDonorAliasesToPlayer: a follower alias recruited in
        // self-dialogue makes the donor a follower
        for (const auto& [quest, aid] : toAdopt) {
            SKSE::log::info("Moved puppet's alias ID {} in quest {:08X} to the player", aid, quest->GetFormID());
            TakeAlias(quest, aid, a_puppet, a_player);
        }
        a_puppet->EvaluatePackage(false, false);
    }

    void AliasSwapManager::SwapNewDonorAliases() {
        // Aliases are only swapped when the session begins. A quest that starts later and fills a Unique Actor
        // alias with the donor (e.g. a follower mod's banter quest started by a Change Location event) gives the
        // parked donor its scenes, packages and force-greets, so they never reach the player.
        auto& session = Session::GetSingleton();
        auto* donor = session.GetDonor();
        auto* player = Session::GetPlayer();
        if (!session.IsActive() || !donor || !player) return;

        auto toSwap = GetFilledRefAliases(donor);
        for (const auto& [quest, aid] : toSwap) {
            SKSE::log::info("Quest {:08X} alias ID {} holds the donor; moving it to the player", quest->GetFormID(), aid);
            TakeAlias(quest, aid, donor, player);
        }
        if (!toSwap.empty()) {
            player->EvaluatePackage(false, false);
        }
    }

    void AliasSwapManager::TakeAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID, RE::Actor* a_from, RE::Actor* a_to) {
        auto factions = MoveAlias(a_quest, a_aliasID, a_from, a_to);
        auto known = std::ranges::find_if(_swappedAliases, [&](const SwappedAlias& a_entry) {
            return a_entry.questFormID == a_quest->GetFormID() && a_entry.aliasID == a_aliasID;
        });
        if (known != _swappedAliases.end()) {
            known->factions = std::move(factions);
        } else {
            _swappedAliases.push_back({ a_quest->GetFormID(), a_aliasID, std::move(factions) });
        }
    }

    void AliasSwapManager::Initialize() {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->GetEventSource<RE::TESQuestStartStopEvent>()->AddEventSink(this);
        }
    }

    RE::BSEventNotifyControl AliasSwapManager::ProcessEvent(const RE::TESQuestStartStopEvent* a_event,
        RE::BSTEventSource<RE::TESQuestStartStopEvent>* /*a_eventSource*/) {
        // Deferred to the main thread, where _swappedAliases is used, and until the quest has filled its aliases
        if (a_event && a_event->started && Session::GetSingleton().IsActive()) {
            SKSE::GetTaskInterface()->AddTask([] { AliasSwapManager::GetSingleton().SwapNewDonorAliases(); });
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    void AliasSwapManager::DropReleasedAliases() {
        // A quest that clears or refills a swapped alias (e.g. dismissing a follower) takes it, and its factions,
        // from the donor; restoring it on revert would undo that
        auto* player = Session::GetPlayer();
        auto released = std::ranges::remove_if(_swappedAliases, [&](const SwappedAlias& a_entry) {
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_entry.questFormID);
            if (!IsQuestRunning(quest) || GetAliasReference(quest, a_entry.aliasID) == player) return false;
            SKSE::log::info("Quest {:08X} alias ID {} no longer holds the player; the donor loses it",
                a_entry.questFormID, a_entry.aliasID);
            return true;
        });
        if (released.empty()) return;
        _swappedAliases.erase(released.begin(), released.end());
    }

    bool AliasSwapManager::IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const {
        for (const auto& entry : _swappedAliases) {
            if (entry.questFormID == a_questFormID && entry.aliasID == a_aliasID) return true;
        }
        return false;
    }

    void AliasSwapManager::Clear() {
        _swappedAliases.clear();
    }

    bool AliasSwapManager::IsAliasFaction(const RE::TESFaction* a_faction) const {
        return std::ranges::any_of(_swappedAliases, [&](const SwappedAlias& a_entry) {
            return std::ranges::find(a_entry.factions, a_faction, &FactionRank::first) != a_entry.factions.end();
        });
    }

    void AliasSwapManager::SetSwappedAliases(std::vector<SwappedAlias> a_aliases) {
        _swappedAliases = std::move(a_aliases);
    }

}
