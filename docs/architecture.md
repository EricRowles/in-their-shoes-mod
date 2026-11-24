# Architecture

In Their Shoes is an SKSE plugin (C++, CommonLibSSE-NG) with a thin Papyrus layer. The plugin does the work; Papyrus handles spells, fades, actor positions, quest aliases and the MCM.

## The three actors

| Actor | Role while impersonating |
|---|---|
| **Player** | Looks, sounds and fights like the donor (appearance, voice, stats, spells, items). Belongs to the donor's factions, holds its quest aliases and answers its conditions. |
| **Puppet** (`ITS_TempPlayer`) | Looks like the Dragonborn and follows the player. Talking to it opens the donor's dialogue. |
| **Donor** | Parked out of sight in a holding cell. Conditions that ask about the donor (another actor's package checking its health, a spell checking its factions, where it is) get the player's answer, except its teammate status, and triggers waiting for the donor see it enter or leave when the player does. With the MCM's script redirect on, other mods' scripts that act on the donor (make it speak, ask where it is, change its factions) act on the player instead; its teammate status and relationships stay on the donor, and scripts on the donor's own body (its magic effects) still act on it. Scripts that reach the donor through its quest aliases (which the player holds) act on the player, except moving it to its package's location, which does nothing; changing its outfit sets the donor's outfit and dresses the player in it. Actors whose AI packages follow or go to the donor follow or go to the player. |

## What happens

- **Impersonate:** the Dragonborn's appearance, stats and spells are saved. The puppet gets the Dragonborn's look and worn gear, the rest of the Dragonborn's items go into a chest in the holding cell, the player gets the donor's look and race (with the race's abilities and powers), items and abilities (e.g. a unique NPC's footstep trail), and the donor is parked. Spawned actors (temporary `FF` references) can't be impersonated: they may be deleted while parked. The donor's outfit becomes ordinary items. A unique donor never gets its outfit again; others get it back when they next load.
- **Switch donor:** the first donor gets the player's current state back and takes the player's place, then the new donor is applied. The puppet is unchanged.
- **Revert:** the donor gets the player's current items, spells and stats. The player gets the Dragonborn back, with the puppet's items and the chest's. Skill and attribute gains made while impersonating stay with the donor. The Dragonborn's level, level and skill XP, perk points and perks go back to what they were, so level-ups made as the donor are lost.
- **While impersonating:**
  - The player answers the donor's conditions and holds its quest aliases, so the donor's quests, scenes and idle lines play through the player. This includes aliases that a quest fills with the donor during the session, such as a follower mod's quest that starts on a location change.
  - The player is in the donor's factions instead of the Dragonborn's (the Dragonborn keeps PlayerFaction). The donor's faction allies and enemies react to the player as they would to the donor, and faction-owned beds and goods are the player's to use. When the session ends, the donor gets the player's factions as they are then.
  - The player has the donor's linked references (e.g. the idle marker its package uses) instead of the Dragonborn's. When the session ends, the donor gets the player's linked references as they are then.
  - The player keeps the Dragonborn's relationships (followers, spouse). Where the Dragonborn has none with an actor, the donor's apply, so the donor's family, friends and enemies treat the player as the donor. The puppet keeps the Dragonborn's. Class checks answer with the donor's class.
  - With the player idle, the player runs the donor's AI packages, its default ones (such as DefaultMasterPackage) included, and sandboxes around the donor's home rather than where it stands. Idle time is game time: menus and the game window being in the background pause it. Not while the player is in furniture it used itself (a chair, a crafting station), or while something else (a scene, another mod) has the player AI-controlled; idle AI never takes that control away. Only input that acts in the world takes control back: moving, attacking, drawing a weapon, activating, jumping, sneaking or shouting. Other keys (menus, hotkeys, the camera, Run) don't, so the player can manage equipment while idle AI walks. A package that would greet the Dragonborn walks to the puppet instead and opens dialogue. A package picked in the MCM comes from a high-priority quest alias, so it outranks the donor's packages, its quest packages (such as a follower's Follow) included.
  - Scripts don't see the items that change hands when a session begins or ends: the donor's inventory becomes the player's, not lost or gained. The donor's alias scripts also don't see items the player moves through a container, barter or gift menu, which is the Dragonborn's doing (a follower mod that takes back outfit pieces leaving the follower lets the player fill its outfit chest).
  - The player voices every line the donor would say. See [dialogue_system.md](dialogue_system.md).
  - The donor starts with no bounty. Bounty run up as the donor is dropped when the session ends, and the Dragonborn's comes back.
  - RaceMenu won't open.
- **Saving:** the session and the MCM's settings are stored in the SKSE co-save and restored on load. The MCM shows the donor's name, or Not Found if a saved session's donor no longer exists. Saves made while impersonating with a version before 0.9 aren't read: revert before updating.

## Code map (`SKSEPlugin/src/`)

| Module | Responsibility |
|---|---|
| `ImpersonationManager` | Impersonate, switch and revert; keeps the donor parked and the puppet configured |
| `Lifecycle` | Passes plugin, game and session events to the managers in a fixed order |
| `Session` | Who is who (donor, puppet, self-dialogue), readable from any thread. Source of truth for whether the player is impersonating |
| `Settings` | The MCM's settings, which the plugin owns and the MCM reads and sets by name; saved in the co-save, readable from any thread |
| `GameClock` | Time the game has been running, without pauses, loading screens or hitches. Gameplay timers use it |
| `PlayerBusy` | Why the player can't be given something to do (combat, a menu, a scene, ...), for idle AI and idle chatter |
| `ConditionHooks` | Identity, relationship, location and state (health, combat, ...) conditions answer for the right actor |
| `ActorHooks` | Voice type, editor location, per-frame player update and RaceMenu refusal hooks |
| `AliasSwapManager` | Moves the donor's quest aliases to the player and back |
| `DialogueManager` | Self-dialogue, routing the engine's lines to the player |
| `PlayerVoice` | The player voicing one line at a time, including keeping a stand-in silent while the player voices its line |
| `IdleChatter` | Picks the donor's idle lines for the player to say |
| `QuestConditions` | Evaluates a condition list in its quest's context |
| `IdleAIManager` | Runs the donor's packages, or the package picked in the MCM, on the idle player |
| `PlayerMoveSpeed` | Makes the AI walk or run the idle player as its Always Run setting says |
| `PackageRetarget` | Owns every runtime package edit: other actors' packages that target the donor (e.g. a companion following it) target the player, and packages the idle player runs that target the player target the puppet |
| `FaceManager`, `StatManager`, `InventoryManager` | Copy appearance, stats, and items/spells between actors |
| `SessionStash` | What the player takes from the donor for the session and hands back, setting the Dragonborn's aside: factions, linked references and bounty |
| `ScriptEventFilter` | Keeps item and equip events from the plugin's transfers, and the player's menu transfers, from the donor's scripts |
| `Forms` | Every form looked up by ID |
| `Serialization` | Co-save records (each `SessionStash` writes its own) |
| `Papyrus` | `ITS_NG` natives, and trigger events that fire for the parked donor |
| `NativeRedirect` | When enabled in the MCM, natives other mods' scripts call on the donor (or on the puppet in self-dialogue) run on the player |

Rules the code relies on:
- Hooks that run in parallel with the game (conditions, voice types, AI procedures) read only `Session`, `Settings` and locked state.
- Managers don't call each other for lifecycle work; that goes through `Lifecycle`.
- Base records aren't changed permanently; per-actor runtime state and hooks are used instead. (One exception: unique donors' outfits, see `ImpersonationManager.cpp`.)

Implementation details live in comments in the code.

## Papyrus (`source/scripts/`)

| Script | Role |
|---|---|
| `ITS_NG` | Native function bindings |
| `ITS_QuestScript` | Fades, actor positions, aliases and spells; calls `Impersonate`/`Revert` |
| `ITS_ImpersonateScript`, `ITS_RevertScript` | Spell effects |
| `ITS_NPCScript` | On the puppet: activating it opens self-dialogue |
| `ITS_PlayerScript` | Load-game maintenance |
| `ITS_MCMScript` | SkyUI settings |
