#pragma once

namespace InTheirShoes::ConditionHooks {

    // Replaces the condition functions that ask who or where an actor is, so while impersonating:
    // - Identity (GetIsID, GetIsAliasRef, GetIsVoiceType, GetInFaction, GetFactionRank, GetIsReference,
    //   IsInList): the player tests as the donor and the puppet as the Dragonborn (reversed in self-dialogue)
    // - Spatial (GetInCell, GetInCurrentLoc, GetDistance, IsInInterior, GetInSameCell, GetInWorldspace, GetPos,
    //   GetHeadingAngle, GetAngle): the parked donor is where the player is (Session::ResolveSpatialRef)
    // The hooks may run on AI threads and only read Session and the donor's alias factions.
    void Install();

}
