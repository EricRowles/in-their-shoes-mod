#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>

#ifdef GetObject
#    undef GetObject
#endif

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;

namespace InTheirShoes {
    inline std::atomic<std::uint32_t> g_mainThreadId{ 0 };
    inline bool IsMainThread() {
        auto tid = g_mainThreadId.load(std::memory_order_relaxed);
        if (tid == 0) return true;
        return REX::W32::GetCurrentThreadId() == tid;
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
}
