#pragma once

namespace InTheirShoes::Lifecycle {

    // The one place that tells the managers about plugin, game and session events, in a fixed order.
    // Managers call these instead of calling each other for lifecycle work. Not thread-safe.

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

    // DialogueManager, while self-dialogue runs and as it ends: topic fragments may have put the puppet, which
    // stands in for the donor, into quest aliases. Those go to the player, who holds the donor's aliases.
    // (What fragments' natives do to the puppet is redirected by NativeRedirect.)
    void OnPuppetChangedInSelfDialogue();

}
