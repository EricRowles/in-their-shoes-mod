; Native bridge for In Their Shoes NG SKSE64 Plugin
Scriptname ITS_NG Hidden

; Removes previously copied donor spells from a_player, returns a_player's inventory back to a_donor,
; and retrieves a_player's original gear from a_storageFollower (if provided).
Bool Function RestoreInventoryFromPlayer(Actor a_player, Actor a_donor, Actor a_storageFollower) Global Native

; Copies all base stats (Health, Magicka, Stamina) and 18 skills from a_fromActor to a_toActor
; without modifying saved player stats.
Bool Function CopyStats(Actor a_fromActor, Actor a_toActor) Global Native

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

; Opens Skyrim's native Dialogue Menu directly with the controlled actor's morphed identity.
Bool Function OpenSelfDialogue(Actor a_speaker = None) Global Native

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
