#pragma once

namespace InTheirShoes::ActorHooks {

    // Hooks on actor and player virtual functions:
    // - TESNPC::GetObjectVoiceType: the player's and puppet's bases report the donor's voice type
    // - PlayerCharacter editor location (0x3C, 0x3D): the donor's editor position, so its sandbox packages take
    //   the player home; where the player stands when that is the holding cell (which froze the game)
    // - PlayerCharacter::Update (0xAD): drives Lifecycle::OnFrame
    // - RaceSexMenu::ProcessMessage: keeps RaceMenu from opening while impersonating
    void Install();

    // An NPC's own voice type, bypassing the voice type hook
    RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc);

}
