# InTheirShoes.esp

The ESP holds the few records the plugin and scripts need. It is ESL-flagged, so in game its IDs show as `FExxx…`; the IDs below are as stored in the plugin. The plugin looks records up by these IDs in `SKSEPlugin/src/Forms.cpp`, so update it there if you renumber one.

`tools/build_and_deploy.py` copies the repo's ESP over the modlist copies, so save ESP edits into the repo.

| Record | ID | Purpose |
|---|---|---|
| `ITS_Quest` | `D92` | Always-running controller quest (`ITS_QuestScript`, `ITS_MCMScript`) |
| ↳ alias `Player` (0) | | The player (`ITS_PlayerScript`, and SkyUI's `SKI_PlayerLoadGameAlias`, which reloads the MCM on each load) |
| ↳ alias `PlayerPuppet` (1) | | The puppet (`ITS_NPCScript`). Packages: `ITS_PuppetFollowPackage`, then vanilla `DefaultSandboxCurrentLocation1024` when following is off |
| ↳ alias `OriginalDonor` (2) | | The current donor |
| `ITS_PuppetFollows` | `878` | Global set by the MCM; gates the follow package |
| `ITS_PuppetFollowPackage` | `87B` | Follow the player. A copy of vanilla `HowlSummonedWolfFollowPlayer`, since a templated package needs all of its template's data inputs (check with `scratch/dump_packages.py`) |
| `ITS_ImpersonateSpell` / `ITS_ImpersonateEffect` | `D69` / `D6B` | Impersonate (`ITS_ImpersonateScript`) |
| `ITS_RevertSpell` / `ITS_RevertEffect` | `802` / `801` | Return to body (`ITS_RevertScript`) |
| `ITS_TempPlayer` | NPC `D93`, ref `D96` | The puppet |
| `ITS_StorageCell` | `D6D` | Holding cell |
| `ITS_StorageMarker` | `807` | Where the donor is parked |
| `ITS_TempMarker` | `D97` | Holds a spot while actors swap places |
