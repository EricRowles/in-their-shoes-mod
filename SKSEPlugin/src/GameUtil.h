#pragma once

#include <atomic>
#include <chrono>
#include <source_location>

namespace InTheirShoes {

    // Set once, when the plugin loads (which runs on the game's main thread)
    inline std::atomic<std::uint32_t> g_mainThreadId{ 0 };
    inline bool IsMainThread() {
        auto tid = g_mainThreadId.load(std::memory_order_relaxed);
        if (tid == 0) return true;
        return REX::W32::GetCurrentThreadId() == tid;
    }

    // Engine and Papyrus VM calls must run on the main thread (AGENTS.md). Called at entry points: logs the
    // caller when it isn't, without changing what it does.
    inline bool CheckMainThread(std::source_location a_caller = std::source_location::current()) {
        if (IsMainThread()) return true;
        SKSE::log::error("{} ran off the main thread (thread {})", a_caller.function_name(), REX::W32::GetCurrentThreadId());
        return false;
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

    // Real time, which keeps running while the game is paused
    inline float SecondsSince(std::chrono::steady_clock::time_point a_time) {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_time).count();
    }

}
