#include "PCH.h"
#include "AliasSwapManager.h"
#include "Forms.h"
#include "GameUtil.h"
#include "Session.h"

#include <algorithm>
#include <unordered_map>
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

        std::unordered_map<RE::TESFaction*, std::int8_t> GetFactionRanks(RE::Actor* a_actor) {
            std::unordered_map<RE::TESFaction*, std::int8_t> ranks;
            a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
                if (a_faction && a_rank >= 0) {
                    ranks[a_faction] = a_rank;
                }
                return false;
            });
            return ranks;
        }

        // Moves a_from's alias to a_to, returning the factions a_from lost with it (the alias's factions)
        std::vector<AliasSwapManager::FactionRank> MoveAlias(RE::TESQuest* a_quest, std::uint32_t a_aliasID,
            RE::Actor* a_from, RE::Actor* a_to) {
            auto before = GetFactionRanks(a_from);
            a_quest->ForceRefIntoAlias(a_aliasID, a_to);
            auto after = GetFactionRanks(a_from);
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
        PublishDonorAliasFactions();
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
        PublishDonorAliasFactions();
    }

    void AliasSwapManager::AdoptPuppetAliases(RE::Actor* a_puppet, RE::Actor* a_player) {
        if (!a_puppet || !a_player) return;

        auto toAdopt = GetFilledRefAliases(a_puppet);
        if (toAdopt.empty()) return;

        // Their factions count for the donor too, as in SwapDonorAliasesToPlayer: a follower alias recruited in
        // self-dialogue makes the donor a follower
        for (const auto& [quest, aid] : toAdopt) {
            SKSE::log::info("Moved puppet's alias ID {} in quest {:08X} to the player", aid, quest->GetFormID());
            auto factions = MoveAlias(quest, aid, a_puppet, a_player);
            auto known = std::ranges::find_if(_swappedAliases, [&](const SwappedAlias& a_entry) {
                return a_entry.questFormID == quest->GetFormID() && a_entry.aliasID == aid;
            });
            if (known != _swappedAliases.end()) {
                known->factions = std::move(factions);
            } else {
                _swappedAliases.push_back({ quest->GetFormID(), aid, std::move(factions) });
            }
        }
        a_puppet->EvaluatePackage(false, false);
        PublishDonorAliasFactions();
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
        PublishDonorAliasFactions();
    }

    bool AliasSwapManager::IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const {
        for (const auto& entry : _swappedAliases) {
            if (entry.questFormID == a_questFormID && entry.aliasID == a_aliasID) return true;
        }
        return false;
    }

    void AliasSwapManager::Clear() {
        _swappedAliases.clear();
        PublishDonorAliasFactions();
    }

    void AliasSwapManager::PublishDonorAliasFactions() {
        std::vector<FactionRank> factions;
        for (const auto& entry : _swappedAliases) {
            for (const auto& [faction, rank] : entry.factions) {
                auto known = std::ranges::find(factions, faction, &FactionRank::first);
                if (known == factions.end()) {
                    factions.emplace_back(faction, rank);
                } else {
                    known->second = std::max(known->second, rank);
                }
            }
        }
        std::scoped_lock lock(_donorAliasFactionsLock);
        _donorAliasFactions = std::move(factions);
    }

    std::optional<std::int8_t> AliasSwapManager::GetDonorAliasFactionRank(const RE::TESFaction* a_faction) const {
        std::scoped_lock lock(_donorAliasFactionsLock);
        for (const auto& [faction, rank] : _donorAliasFactions) {
            if (faction == a_faction) return rank;
        }
        return std::nullopt;
    }

    void AliasSwapManager::SetSwappedAliases(std::vector<SwappedAlias> a_aliases) {
        _swappedAliases = std::move(a_aliases);
        PublishDonorAliasFactions();
    }

}
