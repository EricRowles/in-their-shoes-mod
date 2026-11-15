#pragma once

#include <cstdint>
#include <string_view>

namespace InTheirShoes {

    // Why the player can't be given something to do right now (idle AI, idle chatter)
    enum class PlayerBusy {
        kNo,
        kSelfDialogue,
        kCombat,
        kSpeaking,        // The player's voice is playing a line
        kAIControlled,    // The player is AI-driven, or the engine is walking it to a position (such as furniture)
        kFurniture,       // Sitting, sleeping or using a crafting station
        kScene,
        kWindowInactive,  // The game window is in the background
        kMenu,
    };

    // Which reasons a caller checks (WhatBlocks), as bits
    enum class BusyCheck : std::uint32_t {
        kSelfDialogue = 1 << 0,
        kCombat = 1 << 1,
        kSpeaking = 1 << 2,
        kAIControlled = 1 << 3,
        kFurniture = 1 << 4,
        kScene = 1 << 5,
        kWindowInactive = 1 << 6,
        kMenu = 1 << 7,
    };
    constexpr BusyCheck operator|(BusyCheck a_lhs, BusyCheck a_rhs) {
        return static_cast<BusyCheck>(static_cast<std::uint32_t>(a_lhs) | static_cast<std::uint32_t>(a_rhs));
    }

    // The first reason in a_checks that holds, in PlayerBusy's order, or kNo
    PlayerBusy WhatBlocks(RE::PlayerCharacter* a_player, BusyCheck a_checks);

    // Logs when what keeps a_who from acting changes, and remembers it in a_last
    void LogPlayerBusy(PlayerBusy& a_last, PlayerBusy a_now, std::string_view a_who);

    // A menu is open that pauses the game or takes the player's attention (inventory, map, dialogue, ...)
    bool IsBlockingMenuOpen();

    // Whether the player is AI-driven, by anything: Game.SetPlayerAIDriven (PlayerCharacter::SetAIDriven sets
    // aiControlledPackage), or the engine walking the player to or from a position, such as furniture
    bool IsPlayerAIControlled(RE::PlayerCharacter* a_player);

    // Sitting, sleeping, or using a crafting station, or getting in or out of one (not riding, which uses the
    // sitting state too)
    bool IsInFurniture(RE::PlayerCharacter* a_player);

}
