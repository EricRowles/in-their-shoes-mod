#pragma once

namespace InTheirShoes::ConditionHooks {

    // Hooks condition evaluation so while impersonating:
    // - References: any condition that reaches the parked donor from another actor (through its run-on
    //   reference, its target or a reference parameter) is asked of the player. A few answers stay the donor's,
    //   e.g. GetPlayerTeammate.
    // - Identity (GetIsID, GetIsAliasRef, GetIsVoiceType, GetInFaction, GetFactionRank, GetIsReference,
    //   IsInList): the player tests as the donor and the puppet as the Dragonborn (reversed in self-dialogue).
    //   Faction conditions asked of the donor as the subject ask the player, who holds its factions.
    // - Spatial and state (GetInCell, GetDistance, GetSitting, ..., GetHealthPercentage, IsInCombat,
    //   GetCombatState, IsSneaking, and IsBleedingOut with its own rule): the donor as the subject, and the puppet
    //   in self-dialogue, are where the player is and in its state (Session::ResolveSpatialRef)
    // The hooks may run on AI threads and only read Session and the donor's alias factions.
    void Install();

    // Trampoline space the condition evaluation hook uses (main.cpp adds it to the plugin's trampoline)
    inline constexpr std::size_t kTrampolineSize = 64;

}
