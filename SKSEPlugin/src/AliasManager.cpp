#include "PCH.h"
#include "AliasManager.h"

namespace InTheirShoes {

    bool AliasManager::IsModInternalQuest(RE::TESQuest* a_quest) {
        if (!a_quest) return false;

        static RE::TESQuest* s_modQuest = nullptr;

        if (!s_modQuest) {
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                // We only have the one quest in this mod...
                s_modQuest = dataHandler->LookupForm<RE::TESQuest>(0x00000D92, "InTheirShoes.esp"sv);
            }
        }
        return a_quest == s_modQuest;
    }

    std::vector<RE::TESPackage*> AliasManager::GetAliasPackages(RE::Actor* a_actor) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::vector<RE::TESPackage*> result;
        if (!a_actor) return result;

        struct AliasPkgEntry {
            std::int8_t questPriority;
            RE::TESPackage* pkg;
        };
        std::vector<AliasPkgEntry> entries;

        auto* aliasArray = a_actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (aliasArray) {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (!inst || !inst->quest || !inst->instancedPackages) continue;
                if (!inst->quest->IsRunning()) continue;

                std::int8_t priority = inst->quest->data.priority;
                for (auto* pkg : *inst->instancedPackages) {
                    if (pkg) {
                        entries.push_back({ priority, pkg });
                    }
                }
            }
        }

        // Sort by quest priority descending (higher priority quests evaluate first)
        std::stable_sort(entries.begin(), entries.end(), [](const AliasPkgEntry& a, const AliasPkgEntry& b) {
            return a.questPriority > b.questPriority;
        });

        for (const auto& entry : entries) {
            bool found = false;
            for (auto* existing : result) {
                if (existing == entry.pkg) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.push_back(entry.pkg);
            }
        }

        return result;
    }

}
