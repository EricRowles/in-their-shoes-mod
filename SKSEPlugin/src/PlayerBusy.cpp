#include "PCH.h"
#include "PlayerBusy.h"
#include "GameUtil.h"
#include "PlayerVoice.h"
#include "Session.h"

#include "RE/M/Main.h"

namespace InTheirShoes {

    namespace {
        constexpr bool Has(BusyCheck a_checks, BusyCheck a_check) {
            return (static_cast<std::uint32_t>(a_checks) & static_cast<std::uint32_t>(a_check)) != 0;
        }

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

    PlayerBusy WhatBlocks(RE::PlayerCharacter* a_player, BusyCheck a_checks) {
        if (Has(a_checks, BusyCheck::kSelfDialogue) && Session::GetSingleton().IsInSelfDialogue()) {
            return PlayerBusy::kSelfDialogue;
        }
        if (Has(a_checks, BusyCheck::kCombat) && a_player->IsInCombat()) {
            return PlayerBusy::kCombat;
        }
        if (Has(a_checks, BusyCheck::kSpeaking) && (PlayerVoice::GetSingleton().IsPlaying() || PlayerVoice::IsVoiceActive(a_player))) {
            return PlayerBusy::kSpeaking;
        }
        if (Has(a_checks, BusyCheck::kAIControlled) && IsPlayerAIControlled(a_player)) {
            return PlayerBusy::kAIControlled;
        }
        if (Has(a_checks, BusyCheck::kFurniture) && IsInFurniture(a_player)) {
            return PlayerBusy::kFurniture;
        }
        if (Has(a_checks, BusyCheck::kScene) && GetPlayingScene(a_player)) {
            return PlayerBusy::kScene;
        }
        if (Has(a_checks, BusyCheck::kWindowInactive)) {
            if (auto* main = RE::Main::GetSingleton(); main && !main->GetRuntimeData().gameActive) {
                return PlayerBusy::kWindowInactive;
            }
        }
        if (Has(a_checks, BusyCheck::kMenu) && IsBlockingMenuOpen()) {
            return PlayerBusy::kMenu;
        }
        return PlayerBusy::kNo;
    }

    void LogPlayerBusy(PlayerBusy& a_last, PlayerBusy a_now, std::string_view a_who) {
        if (a_now == a_last) return;
        if (a_now != PlayerBusy::kNo) {
            SKSE::log::info("{} waiting: {}", a_who, ToString(a_now));
        }
        a_last = a_now;
    }

    bool IsBlockingMenuOpen() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
        return ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() ||
               ui->IsApplicationMenuOpen() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsMenuOpen("Console"sv) ||
               ui->IsMenuOpen("Loading Menu"sv) || ui->IsMenuOpen("Crafting Menu"sv) || ui->IsMenuOpen("RaceSex Menu"sv) ||
               ui->IsMenuOpen("Barter Menu"sv) || ui->IsMenuOpen("Container Menu"sv) || ui->IsMenuOpen("Journal Menu"sv) ||
               ui->IsMenuOpen("MapMenu"sv) || ui->IsMenuOpen("StatsMenu"sv) || ui->IsMenuOpen("InventoryMenu"sv) ||
               ui->IsMenuOpen("MagicMenu"sv) || ui->IsMenuOpen("TweenMenu"sv) || ui->IsMenuOpen("Book Menu"sv);
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
