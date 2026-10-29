#include "PCH.h"
#include "AliasSwapManager.h"
#include "Forms.h"
#include "GameUtil.h"
#include "Session.h"

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
    }

    void AliasSwapManager::SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        _swappedAliases.clear();
        {
            std::scoped_lock lock(_donorAliasFactionsLock);
            _donorAliasFactions.clear();
        }

        auto toSwap = GetFilledRefAliases(a_donor);
        if (toSwap.empty()) return;

        // Alias factions leave the donor with the aliases: whatever factions the swap takes away are those
        auto ranksBefore = GetFactionRanks(a_donor);
        for (const auto& [quest, aid] : toSwap) {
            quest->ForceRefIntoAlias(aid, a_player);
            _swappedAliases.push_back({ quest->GetFormID(), aid });
            SKSE::log::info("SwapDonorAliasesToPlayer: Swapped donor {:08X} to player in quest {:08X} alias ID {}",
                a_donor->GetFormID(), quest->GetFormID(), aid);
        }

        auto ranksAfter = GetFactionRanks(a_donor);
        std::scoped_lock lock(_donorAliasFactionsLock);
        for (const auto& [faction, rank] : ranksBefore) {
            if (!ranksAfter.contains(faction)) {
                _donorAliasFactions.emplace_back(faction, rank);
                SKSE::log::info("SwapDonorAliasesToPlayer: Donor {:08X} lost alias faction {:08X} (rank {})",
                    a_donor->GetFormID(), faction->GetFormID(), rank);
            }
        }
    }

    void AliasSwapManager::RestoreSwappedAliases(RE::Actor* a_donor) {
        auto* player = Session::GetPlayer();
        for (const auto& entry : _swappedAliases) {
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(entry.questFormID);
            if (!IsQuestRunning(quest) || !a_donor) {
                SKSE::log::info("RestoreSwappedAliases: Skipping quest {:08X} alias ID {} (quest stopped or no donor)",
                    entry.questFormID, entry.aliasID);
                continue;
            }

            RE::BGSBaseAlias* baseAlias = nullptr;
            for (auto* a : quest->aliases) {
                if (a && a->aliasID == entry.aliasID) {
                    baseAlias = a;
                    break;
                }
            }

            if (baseAlias && baseAlias->QType() == "Ref"sv) {
                auto* currentRef = static_cast<RE::BGSRefAlias*>(baseAlias)->GetReference();
                if (currentRef && currentRef != player && currentRef != a_donor) {
                    SKSE::log::info("RestoreSwappedAliases: Alias ID {} in quest {:08X} now holds {:08X}, leaving it alone.",
                        entry.aliasID, entry.questFormID, currentRef->GetFormID());
                    continue;
                }
            }

            quest->ForceRefIntoAlias(entry.aliasID, a_donor);
            SKSE::log::info("RestoreSwappedAliases: Restored donor {:08X} into quest {:08X} alias ID {}",
                a_donor->GetFormID(), quest->GetFormID(), entry.aliasID);
        }
        _swappedAliases.clear();
        // The aliases gave their factions back
        std::scoped_lock lock(_donorAliasFactionsLock);
        _donorAliasFactions.clear();
    }

    void AliasSwapManager::AdoptPuppetAliases(RE::Actor* a_puppet, RE::Actor* a_player) {
        if (!a_puppet || !a_player) return;

        auto toAdopt = GetFilledRefAliases(a_puppet);
        for (const auto& [quest, aid] : toAdopt) {
            quest->ForceRefIntoAlias(aid, a_player);
            if (!IsSwappedAlias(quest->GetFormID(), aid)) {
                _swappedAliases.push_back({ quest->GetFormID(), aid });
            }
            SKSE::log::info("AdoptPuppetAliases: Moved puppet's alias ID {} in quest {:08X} to the player", aid, quest->GetFormID());
        }
        if (!toAdopt.empty()) {
            a_puppet->EvaluatePackage(false, false);
        }
    }

    bool AliasSwapManager::IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const {
        for (const auto& entry : _swappedAliases) {
            if (entry.questFormID == a_questFormID && entry.aliasID == a_aliasID) return true;
        }
        return false;
    }

    void AliasSwapManager::Clear() {
        _swappedAliases.clear();
        std::scoped_lock lock(_donorAliasFactionsLock);
        _donorAliasFactions.clear();
    }

    std::optional<std::int8_t> AliasSwapManager::GetDonorAliasFactionRank(const RE::TESFaction* a_faction) const {
        std::scoped_lock lock(_donorAliasFactionsLock);
        for (const auto& [faction, rank] : _donorAliasFactions) {
            if (faction == a_faction) return rank;
        }
        return std::nullopt;
    }

    std::vector<AliasSwapManager::FactionRank> AliasSwapManager::GetDonorAliasFactions() const {
        std::scoped_lock lock(_donorAliasFactionsLock);
        return _donorAliasFactions;
    }

    void AliasSwapManager::SetDonorAliasFactions(std::vector<FactionRank> a_factions) {
        std::scoped_lock lock(_donorAliasFactionsLock);
        _donorAliasFactions = std::move(a_factions);
    }

}
