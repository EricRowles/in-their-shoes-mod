#pragma once

namespace InTheirShoes::Lifecycle {

    // The one place that tells the managers about plugin, game and session events, in a fixed order.
    // Managers call these instead of calling each other for lifecycle work. All run on the main thread.

    // SKSE messages
    void OnInputLoaded();
    void OnDataLoaded();
    void OnPostLoadGame();

    // Before a save loads or a new game starts (the co-save revert callback)
    void OnGameRevert();

    // Every player update
    void OnFrame();

    // ImpersonationManager, once the player holds the donor's aliases, and as the donor gets them back
    void OnSessionBegin();
    void OnSessionEnd();

    // DialogueManager, while self-dialogue runs and as it ends: topic fragments may have changed the puppet,
    // which stands in for the donor. Factions go to the donor, and quest aliases go to the player, who holds
    // the donor's aliases.
    void OnPuppetChangedInSelfDialogue();

}
