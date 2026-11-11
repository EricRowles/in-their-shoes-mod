#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>

namespace InTheirShoes {

    // Set once, when the plugin loads (which runs on the game's main thread)
    inline std::atomic<std::uint32_t> g_mainThreadId{ 0 };

    // Guards the per-frame hook. Other entry points run on synchronized job threads, which is fine (#15).
    inline bool IsMainThread() {
        auto tid = g_mainThreadId.load(std::memory_order_relaxed);
        if (tid == 0) return true;
        return REX::W32::GetCurrentThreadId() == tid;
    }

    // Whether a quest is running in the Papyrus sense. CommonLib's TESQuest::IsRunning() only checks that no
    // stop or start is in progress, so it is also true for quests that were never started.
    inline bool IsQuestRunning(const RE::TESQuest* a_quest) {
        return a_quest && a_quest->IsEnabled() && !a_quest->IsStopping();
    }

    // An actor's faction memberships. VisitFactions lists the base record's factions, then the runtime changes; a
    // later entry overrides an earlier one, and a rank below 0 means the actor left the faction.
    inline std::unordered_map<RE::TESFaction*, std::int8_t> GetFactionMemberships(RE::Actor* a_actor) {
        std::unordered_map<RE::TESFaction*, std::int8_t> ranks;
        a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
            if (a_faction) ranks[a_faction] = a_rank;
            return false;
        });
        std::erase_if(ranks, [](const auto& a_entry) { return a_entry.second < 0; });
        return ranks;
    }

    // Whether a script variable holds a_form. A form's VM handle holds its form ID in the low 32 bits.
    inline bool HoldsForm(const RE::BSScript::Variable& a_var, const RE::TESForm* a_form) {
        if (!a_form || !a_var.IsObject()) return false;
        auto object = a_var.GetObject();
        return object && static_cast<RE::FormID>(object->GetHandle() & 0xFFFFFFFF) == a_form->GetFormID();
    }

    inline bool IsBlockingMenuOpen() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
        return ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() ||
               ui->IsApplicationMenuOpen() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsMenuOpen("Console"sv) ||
               ui->IsMenuOpen("Loading Menu"sv) || ui->IsMenuOpen("Crafting Menu"sv) || ui->IsMenuOpen("RaceSex Menu"sv) ||
               ui->IsMenuOpen("Barter Menu"sv) || ui->IsMenuOpen("Container Menu"sv) || ui->IsMenuOpen("Journal Menu"sv) ||
               ui->IsMenuOpen("MapMenu"sv) || ui->IsMenuOpen("StatsMenu"sv) || ui->IsMenuOpen("InventoryMenu"sv) ||
               ui->IsMenuOpen("MagicMenu"sv) || ui->IsMenuOpen("TweenMenu"sv) || ui->IsMenuOpen("Book Menu"sv);
    }

    // The scene the player is in, or null when none is playing
    inline RE::BGSScene* GetPlayingScene(RE::Actor* a_actor) {
        auto* scene = a_actor ? a_actor->GetCurrentScene() : nullptr;
        return scene && scene->isPlaying ? scene : nullptr;
    }

    // Calls a_visit for each action of a_type that a_actor has in the scene's current phase, through the scene
    // quest's aliases
    template <class Visit>
    void ForEachSceneActionInCurrentPhase(const RE::BGSScene* a_scene, const RE::Actor* a_actor, RE::BGSSceneAction::Type a_type, Visit&& a_visit) {
        if (!a_scene || !a_scene->isPlaying || !a_scene->parentQuest || !a_actor) return;
        const auto phase = a_scene->currentPhaseIndex;
        for (auto* action : a_scene->actions) {
            if (!action || action->GetType() != a_type) continue;
            if (phase < action->startPhase || phase > action->endPhase) continue;
            if (a_scene->parentQuest->GetAliasedRef(action->actorID).get().get() != a_actor) continue;
            a_visit(action);
        }
    }

    // The packages a_actor's package actions give it in the scene's current phase, in order
    inline std::vector<RE::TESPackage*> GetScenePackagesInCurrentPhase(const RE::BGSScene* a_scene, const RE::Actor* a_actor) {
        std::vector<RE::TESPackage*> packages;
        ForEachSceneActionInCurrentPhase(a_scene, a_actor, RE::BGSSceneAction::Type::kPackage, [&](RE::BGSSceneAction* a_action) {
            for (auto* pkg : static_cast<RE::BGSSceneActionPackage*>(a_action)->packages) {
                if (pkg && std::find(packages.begin(), packages.end(), pkg) == packages.end()) {
                    packages.push_back(pkg);
                }
            }
        });
        return packages;
    }

    // Why the player can't be given something to do right now (idle AI, idle chatter)
    enum class PlayerBusy {
        kNo,
        kCombat,
        kMenu,
        kScene,
        kSelfDialogue,
    };

    constexpr std::string_view ToString(PlayerBusy a_busy) {
        switch (a_busy) {
            case PlayerBusy::kCombat: return "combat"sv;
            case PlayerBusy::kMenu: return "menu"sv;
            case PlayerBusy::kScene: return "scene"sv;
            case PlayerBusy::kSelfDialogue: return "self-dialogue"sv;
            default: return "not busy"sv;
        }
    }

    // Real time, which keeps running while the game is paused
    inline float SecondsSince(std::chrono::steady_clock::time_point a_time) {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_time).count();
    }

}
