#pragma once

namespace InTheirShoes::ConditionHooks {

    // Hooks condition evaluation so while impersonating:
    // - Every condition about the donor (its reference, the target, a parameter, or the subject in a quest's
    //   context, such as an alias's fill conditions) is asked of its stand-in: the player, or the puppet in
    //   self-dialogue for the identity and relationship conditions below. The puppet as the subject in
    //   self-dialogue is the player for the rest. The parked donor's own spells and AI stay its own, as do a few
    //   answers, e.g. GetPlayerTeammate.
    // - Identity (GetIsID, GetIsAliasRef, GetIsVoiceType, GetInFaction, GetFactionRank, GetIsReference,
    //   IsInList, ...): the player tests as the donor and the puppet as the Dragonborn (reversed in self-dialogue)
    // - Relationships: the Dragonborn's first, then the donor's
    // - IsBleedingOut: the player is down at no health as well as in essential bleedout
    // The hooks may run on AI threads and only read Session and the donor's alias factions.
    void Install();

}
