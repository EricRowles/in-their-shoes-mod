#pragma once

namespace InTheirShoes::ActorHooks {

    // Hooks on actor and player virtual functions:
    // - TESNPC::GetObjectVoiceType: the player's and puppet's bases report the donor's voice type
    // - PlayerCharacter editor location (0x3C, 0x3D): while impersonating, the player's editor location is
    //   where they stand, so the donor's sandbox packages anchor locally (the donor's own is the holding cell,
    //   and sandboxing around that unconnected cell froze the game)
    // - PlayerCharacter::Update (0xAD): drives Lifecycle::OnFrame
    void Install();

    // An NPC's own voice type, bypassing the voice type hook
    RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc);

}
