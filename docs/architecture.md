# Architecture

In Their Shoes is an SKSE plugin (C++, CommonLibSSE-NG) with a thin Papyrus layer. The plugin does the work; Papyrus handles spells, fades, actor positions, quest aliases and the MCM.

## The three actors

| Actor | Role while impersonating |
|---|---|
| **Player** | Looks, sounds and fights like the donor (appearance, voice, stats, spells, items). Holds the donor's quest aliases and answers the donor's conditions. |
| **Puppet** (`ITS_TempPlayer`) | Looks like the Dragonborn and follows the player. Talking to it opens the donor's dialogue. |
| **Donor** | Parked out of sight in a holding cell. Conditions and scripts that ask where the donor is get the player's location. |

## What happens

- **Impersonate:** the Dragonborn's appearance, stats and spells are saved. The puppet gets the Dragonborn's look and gear, the player gets the donor's, and the donor is parked.
- **Switch donor:** the first donor gets the player's current state back and takes the player's place, then the new donor is applied. The puppet is unchanged.
- **Revert:** the donor gets the player's current items, spells and stats. The player gets the Dragonborn back.
- **While impersonating:**
  - The player answers the donor's conditions and holds its quest aliases, so the donor's quests, scenes and idle lines play through the player.
  - With the player idle, the player runs the donor's AI packages. A package that would greet the Dragonborn walks to the puppet instead and opens dialogue.
  - The player voices every line the donor would say. See [dialogue_system.md](dialogue_system.md).
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
| `OutfitManager` | Stops impersonated donors' outfits being re-added |
| `DialogueManager` | Self-dialogue, the player voicing lines |
| `IdleChatter` | Picks the donor's idle lines for the player to say |
| `QuestConditions` | Evaluates a condition list in its quest's context |
| `IdleAIManager` | Runs the donor's packages on the idle player |
| `FaceManager`, `StatManager`, `InventoryManager` | Copy appearance, stats, and items/spells between actors |
| `Forms` | Every form looked up by ID |
| `Serialization` | Co-save records |
| `Papyrus` | `ITS_NG` natives, and location natives that answer for the parked donor |

Rules the code relies on:
- Engine and Papyrus calls run on the main thread. Hooks that run on other threads read only `Session` and lock-protected lists.
- Managers don't call each other for lifecycle work; that goes through `Lifecycle`.
- Base records aren't changed permanently; per-actor runtime state and hooks are used instead.

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
