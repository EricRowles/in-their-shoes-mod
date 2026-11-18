#pragma once

namespace InTheirShoes::ConditionHooks {

    // Replaces the condition functions that ask who or where an actor is, so while impersonating:
    // - Identity (GetIsID, GetIsAliasRef, GetIsVoiceType, GetInFaction, GetFactionRank, GetIsReference,
    //   IsInList): the player tests as the donor and the puppet as the Dragonborn (reversed in self-dialogue)
    // - Spatial (GetInCell, GetInCurrentLoc, GetDistance, IsInInterior, GetInSameCell, GetInWorldspace, GetPos,
    //   GetHeadingAngle, GetAngle, GetSitting, GetSleeping): the parked donor, and the puppet in self-dialogue,
    //   are where the player is and sit or sleep when it does (Session::ResolveSpatialRef)
    // The hooks may run on AI threads and only read Session and the donor's alias factions.
    void Install();

}
