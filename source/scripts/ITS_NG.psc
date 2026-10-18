; Native bridge for In Their Shoes NG SKSE64 Plugin
Scriptname ITS_NG Hidden

; Instantly copies full facial structure, headparts, hair color, race, skin, and tints from a_donor to a_target.
; Automatically saves a_target's previous appearance for restoration.
Bool Function CloneAppearance(Actor a_donor, Actor a_target) Global Native

; Instantly copies full facial structure, headparts, hair color, race, skin, and tints from a_donor to a_target.
; Does not record or modify the saved player appearance.
Bool Function CopyAppearance(Actor a_donor, Actor a_target) Global Native

; Restores a_target's original appearance from before the last CloneAppearance call.
Bool Function RevertAppearance(Actor a_target, Bool a_update3D = true) Global Native

; Stores a_player's current inventory into a_storageFollower (if provided) then transfers a_donor's
; inventory + dynamic spells to a_player. Pass None for a_storageFollower if DragonbornFollower is not used.
Bool Function TransferInventoryToPlayer(Actor a_player, Actor a_donor, Actor a_storageFollower) Global Native

; Removes previously copied donor spells from a_player, returns a_player's inventory back to a_donor,
; and retrieves a_player's original gear from a_storageFollower (if provided).
Bool Function RestoreInventoryFromPlayer(Actor a_player, Actor a_donor, Actor a_storageFollower) Global Native

; Copies all base stats (Health, Magicka, Stamina) and 18 skills from a_donor to a_player,
; saving a_player's original stats for restoration.
Bool Function TransferStats(Actor a_donor, Actor a_player) Global Native

; Copies all base stats and 18 skills from a_fromActor to a_toActor without modifying saved player stats.
Bool Function CopyStats(Actor a_fromActor, Actor a_toActor) Global Native

; Restores a_player's original base stats and skills.
Bool Function RestoreStats(Actor a_player) Global Native

; Atomically applies donor appearance, stats, and transfers inventory to player in a single native operation.
Bool Function ApplyDonorProfile(Actor a_donor, Actor a_player, Actor a_StorageFollower = None) Global Native

; Atomically restores player appearance, stats, and restores original inventory in a single native operation.
Bool Function RestorePlayerProfile(Actor a_player, Actor a_donor = None, Actor a_StorageFollower = None) Global Native

; Enables or disables autonomous idle AI pathing on the player when inactive.
Function SetIdleAIEnabled(Bool a_enabled) Global Native

; Sets the idle timeout in seconds before autonomous AI pathing begins (default 10.0s).
Function SetIdleAITimeout(Float a_seconds) Global Native

; Sets the idle chatter cooldown in seconds between ambient dialogue lines while impersonating an NPC (default 60.0s).
Function SetIdleChatterTime(Float a_seconds) Global Native

; Sets whether the player is currently actively controlling an alternate NPC.
Function SetControllingNPC(Bool a_controlling) Global Native

; Updates idle timer and initiates autonomous pathing if inactive threshold is met.
Function UpdateIdleAI() Global Native

; Opens Skyrim's native Dialogue Menu directly with the controlled actor's morphed identity.
Bool Function OpenSelfDialogue(Actor a_speaker = None) Global Native

; Triggers an immediate on-demand voice line / greeting from the controlled actor's morphed voice.
Bool Function TriggerVoiceLine(Actor a_speaker = None) Global Native

; Returns the base biological sex of the actor (0 = Male, 1 = Female).
Int Function GetActorSex(Actor a_actor) Global Native

; Initiates full impersonation of a_donor by a_player (conditions, aliases, packages, scripts).
Int Function BeginImpersonation(Actor a_donor, Actor a_player) Global Native

; Ends active impersonation and restores original targets and properties.
Int Function EndImpersonation() Global Native

; Enables or disables donor NPC voice comments and voice identity on the player.
Function SetNPCVoiceEnabled(Bool a_enabled) Global Native

; Sets whether the Dragonborn puppet follows the player as a teammate.
Function SetPuppetFollowsPlayer(Bool a_follows) Global Native

; Registers the active puppet actor holding the player's true appearance/identity.
Function SetPuppetActor(Actor a_puppet) Global Native

; Returns the player's true original character name.
String Function GetOriginalPlayerName() Global Native

; Validates whether a_target can be impersonated by the player.
; Returns: 0 = Valid, 1 = Invalid, 2 = Player, 3 = Dead/Dying, 4 = Child, 5 = Non-humanoid, 6 = Mounted, 7 = Ghost.
Int Function ValidateImpersonateTarget(Actor a_target) Global Native


