# Architecture

In Their Shoes is an SKSE plugin (C++, CommonLibSSE-NG) with a thin Papyrus layer. The plugin does the work; Papyrus handles spells, fades, actor positions, quest aliases and the MCM.

## The three actors

| Actor | Role while impersonating |
|---|---|
| **Player** | Looks, sounds and fights like the donor (appearance, voice, stats, spells, items). Holds the donor's quest aliases and answers the donor's conditions. |
| **Puppet** (`ITS_TempPlayer`) | Looks like the Dragonborn and follows the player. Talking to it opens the donor's dialogue. |
| **Donor** | Parked out of sight in a holding cell. Conditions that ask where the donor is get the player's location, and triggers waiting for the donor see it enter or leave when the player does. Other mods' scripts that act on the donor (make it speak, ask where it is) act on the player instead; its factions and teammate status stay on the donor. Scripts on the donor's own body (its magic effects) still act on it. Actors whose AI packages follow or go to the donor follow or go to the player. |

## What happens

- **Impersonate:** the Dragonborn's appearance, stats and spells are saved. The puppet gets the Dragonborn's look and worn gear, the rest of the Dragonborn's items go into a chest in the holding cell, the player gets the donor's look, items and abilities (e.g. a unique NPC's footstep trail), and the donor is parked. The donor's outfit becomes ordinary items. A unique donor never gets its outfit again; others get it back when they next load.
- **Switch donor:** the first donor gets the player's current state back and takes the player's place, then the new donor is applied. The puppet is unchanged.
- **Revert:** the donor gets the player's current items, spells and stats. The player gets the Dragonborn back, with the puppet's items and the chest's.
- **While impersonating:**
  - The player answers the donor's conditions and holds its quest aliases, so the donor's quests, scenes and idle lines play through the player.
  - With the player idle, the player runs the donor's AI packages. A package that would greet the Dragonborn walks to the puppet instead and opens dialogue.
  - The player voices every line the donor would say. See [dialogue_system.md](dialogue_system.md).
  - RaceMenu won't open.
- **Saving:** the session is stored in the SKSE co-save and restored on load.

## Code map (`SKSEPlugin/src/`)

| Module | Responsibility |
|---|---|
| `ImpersonationManager` | Impersonate, switch and revert; keeps the donor parked and the puppet configured |
| `Lifecycle` | Passes plugin, game and session events to the managers in a fixed order |
| `Session` | Who is who (donor, puppet, self-dialogue), readable from any thread |
| `ConditionHooks` | Identity and location conditions answer for the right actor |
| `ActorHooks` | Voice type, editor location and per-frame player update hooks |
| `AliasSwapManager` | Moves the donor's quest aliases to the player and back |
| `DialogueManager` | Self-dialogue, the player voicing lines |
| `IdleChatter` | Picks the donor's idle lines for the player to say |
| `QuestConditions` | Evaluates a condition list in its quest's context |
| `IdleAIManager` | Runs the donor's packages on the idle player |
| `PackageRetarget` | Other actors' packages that target the donor (e.g. a companion following it) target the player |
| `FaceManager`, `StatManager`, `InventoryManager` | Copy appearance, stats, and items/spells between actors |
| `Forms` | Every form looked up by ID |
| `Serialization` | Co-save records |
| `Papyrus` | `ITS_NG` natives, and trigger events that fire for the parked donor |
| `NativeRedirect` | Natives other mods' scripts call on the donor (or on the puppet in self-dialogue) run on the player |

Rules the code relies on:
- Hooks that run in parallel with the game (conditions, voice types, AI procedures) read only `Session` and locked state.
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
