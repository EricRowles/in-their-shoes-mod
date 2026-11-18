#pragma once

#include <cstddef>

namespace InTheirShoes::PlayerMoveSpeed {

    // Whether the AI walks or runs the player (#49). While idle AI drives the player, every walk/run the AI sets for
    // it becomes the player's Always Run setting; otherwise the engine's own choice stands.

    // Trampoline space the hook uses (main.cpp adds it to the plugin's trampoline)
    inline constexpr std::size_t kTrampolineSize = 48;

    // Hooks the engine's "set movement speed", which every walk/run the AI applies goes through
    void Install();

    // Main thread. From now on the AI moves the player at its Always Run setting, including the path under way.
    void Apply(RE::PlayerCharacter* a_player);
    // Main thread. The AI's own walk/run choice again.
    void Release();
    // Main thread, while applied: flips the player's Always Run setting (which it keeps afterwards) and applies it.
    void ToggleAlwaysRun(RE::PlayerCharacter* a_player);

}
