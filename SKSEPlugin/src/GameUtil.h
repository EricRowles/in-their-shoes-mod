#pragma once

#include <atomic>
#include <chrono>

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
