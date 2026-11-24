#include "PCH.h"
#include "PlayerBusy.h"
#include "GameUtil.h"
#include "PlayerVoice.h"
#include "Session.h"

#include <algorithm>
#include <array>

namespace InTheirShoes {

    namespace {
        constexpr std::string_view ToString(PlayerBusy a_busy) {
            switch (a_busy) {
                case PlayerBusy::kSelfDialogue: return "self-dialogue"sv;
                case PlayerBusy::kCombat: return "combat"sv;
                case PlayerBusy::kSpeaking: return "the player is speaking"sv;
                case PlayerBusy::kAIControlled: return "AI-controlled"sv;
                case PlayerBusy::kFurniture: return "furniture"sv;
                case PlayerBusy::kScene: return "scene"sv;
                case PlayerBusy::kWindowInactive: return "game window in the background"sv;
                case PlayerBusy::kMenu: return "menu"sv;
                default: return "not busy"sv;
            }
        }
    }

    PlayerBusy WhatBlocks(RE::PlayerCharacter* a_player, BusyChecks a_checks) {
        if (a_checks.any(BusyCheck::kSelfDialogue) && Session::GetSingleton()->IsInSelfDialogue()) {
            return PlayerBusy::kSelfDialogue;
        }
        if (a_checks.any(BusyCheck::kCombat) && a_player->IsInCombat()) {
            return PlayerBusy::kCombat;
        }
        if (a_checks.any(BusyCheck::kSpeaking) && (PlayerVoice::GetSingleton()->IsPlaying() || PlayerVoice::IsVoiceActive(a_player))) {
            return PlayerBusy::kSpeaking;
        }
        if (a_checks.any(BusyCheck::kAIControlled) && IsPlayerAIControlled(a_player)) {
            return PlayerBusy::kAIControlled;
        }
        if (a_checks.any(BusyCheck::kFurniture) && IsInFurniture(a_player)) {
            return PlayerBusy::kFurniture;
        }
        if (a_checks.any(BusyCheck::kScene) && GetPlayingScene(a_player)) {
            return PlayerBusy::kScene;
        }
        if (a_checks.any(BusyCheck::kWindowInactive)) {
            if (auto* main = RE::Main::GetSingleton(); main && !main->GetRuntimeData().gameActive) {
                return PlayerBusy::kWindowInactive;
            }
        }
        if (a_checks.any(BusyCheck::kMenu) && IsBlockingMenuOpen()) {
            return PlayerBusy::kMenu;
        }
        return PlayerBusy::kNo;
    }

    void LogPlayerBusy(LogOnChange<PlayerBusy>& a_last, PlayerBusy a_now, std::string_view a_who) {
        if (a_last.Changed(a_now) && a_now != PlayerBusy::kNo) {
            SKSE::log::info("{} waiting: {}", a_who, ToString(a_now));
        }
    }

    bool IsBlockingMenuOpen() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
        // The engine's names (the barter and container menus have no space: "Barter Menu" never matched, #46)
        static constexpr std::array kMenus = {
            RE::DialogueMenu::MENU_NAME, RE::Console::MENU_NAME, RE::LoadingMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME,
            RE::RaceSexMenu::MENU_NAME, RE::BarterMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::JournalMenu::MENU_NAME,
            RE::MapMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME, RE::MagicMenu::MENU_NAME,
            RE::TweenMenu::MENU_NAME, RE::BookMenu::MENU_NAME,
        };
        return ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() || ui->IsApplicationMenuOpen() ||
               std::ranges::any_of(kMenus, [ui](std::string_view a_menu) { return ui->IsMenuOpen(a_menu); });
    }

    bool IsPlayerAIControlled(RE::PlayerCharacter* a_player) {
        const auto& flags = a_player->GetPlayerRuntimeData().playerFlags;
        return flags.aiControlledPackage || flags.aiControlledToPos || flags.aiControlledFromPos;
    }

    bool IsInFurniture(RE::PlayerCharacter* a_player) {
        const auto* state = a_player->AsActorState();
        return state && state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal && !a_player->IsOnMount();
    }

}
