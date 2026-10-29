# Overall design
There are three main actors in this mod
- The donor (Original NPC selected for impersonation)
- The impersonator (The user's character, acting as the donor)
- The puppet (A temporary NPC, acting as the player)

## Impersonation
The user has access to an "Impersonate" spell that allows them to impersonate an NPC.
While impersonating, they have access to a "Revert" spell that allows them to return all involved actors to their original form
If the "Impersonate" spell is cast on the puppet, the "Revert" script will be run.
If the "Impersonate" spell is cast on a second donor:
- The first donor will be restored
- The player will impersonate the second donor
- The second donor will be selected as the targeted donor
- The puppet will remain unchanged

## On Impersonation
At the moment the spell is cast, the following shall be copied from the impersonator to the puppet, then from the donor to the impersonator:
- Inventory
- Spells/Shouts
- Appearance
- Stats and Attributes
- Name

## On Revert
On revert, the donor shall be returned to their original form, with the following copied from the impersonator's current state rather than the donor's original state:
- Current Inventory
- Current Spells/Shouts
- Current Stats and Attributes
The player shall be returned to their original form, with their baseline appearance, inventory, spells, and stats restored.

## State Persistence
- All impersonation states, swapped aliases, appearance snapshots, faction changes, and baseline Dragonborn stats shall be serialized into the SKSE co-save file and persist across game save and load cycles.

### Impersonation - Idle Dialogue
The impersonator shall be treated as the quest target for idle dialogue. When the donor would normally say a line, the impersonator shall say it instead.
All conditions that would require the donor to say a line (in location, near reference, is reference, in quest alias etc.) will be applied to the player, so that the player can speak idle lines without the donor.
Conditions that require the player to be nearby will still target the impersonator.

### Impersonation - packages
The donor's original packages shall apply to the player as configured in the MCM menu.

This includes quest packages, so that the impersonator can move and speak in scripted quest scenes as the donor normally would.

If the donor would normally open dialogue with the player, instead the impersonator will open dialogue with the puppet, as described below.

### Impersonation - Combat
Impersonation in combat is not necessary at this time

### Puppet
While impersonating, a puppet will follow the impersonator. This may be toggled in the MCM. If toggled off, the puppet may either stand still or disappear. When toggled on, the puppet shall teleport to the player.

The puppet shall have no voice lines, dialogue, or packages other than those necessary to fake as the player

### Puppet - Dialogue
When interacting with the puppet, instead of opening the normal dialogue menu, the player shall open a dialogue menu with the donor.
The user will be able to select any of the topics that would normally be visible on the donor's dialogue menu. This may require checking donor aliases or factions.
The impersonator shall speak the responses to the lines, not the puppet or the donor.

### Donor
The donor shall remain hidden from the player. If scripting would make the donor visible, the donor shall be moved back to their cell and the scripted packages shall be applied to the impersonating player instead

## Configuration
- The user shall be able to configure these options:
  - Idle pathing (toggle)
  - Idle time before pathing (seconds)
  - Autonomous combat AI (toggle)
  - NPC voice (toggle)
  - NPC voice - interval between idle lines
  - Puppet follows player (toggle)
  - Return to body (action)
