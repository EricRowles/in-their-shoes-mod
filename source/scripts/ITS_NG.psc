; Native bridge for In Their Shoes NG SKSE64 Plugin
Scriptname ITS_NG Hidden

; Impersonates a_donor (or switches to a_donor if already impersonating someone else).
; On the first impersonation the puppet takes the Dragonborn's appearance, stats and gear.
; The donor is moved to the storage marker. Position the puppet before calling.
Bool Function Impersonate(Actor a_donor, Actor a_puppet) Global Native

; Ends impersonation: the donor gets the player's current items, spells and stats, and the player
; gets their own appearance, stats, spells and gear back. Move the actors afterwards.
Bool Function Revert() Global Native

; Enables or disables autonomous idle AI pathing on the player when inactive.
Function SetIdleAIEnabled(Bool a_enabled) Global Native

; Sets the idle timeout in seconds before autonomous AI pathing begins (default 10.0s).
Function SetIdleAITimeout(Float a_seconds) Global Native

; Sets the idle chatter cooldown in seconds between ambient dialogue lines while impersonating an NPC (default 60.0s).
Function SetIdleChatterTime(Float a_seconds) Global Native

; Opens the Dialogue Menu with the puppet, showing the donor's topics.
Bool Function OpenSelfDialogue(Actor a_speaker = None) Global Native

; Enables or disables donor NPC voice comments and voice identity on the player.
Function SetNPCVoiceEnabled(Bool a_enabled) Global Native

; Sets the puppet's teammate status and teleports it to the player when re-enabled.
; The follow package itself is gated by the ITS_PuppetFollows global, which the caller sets.
Function SetPuppetFollowsPlayer(Bool a_follows) Global Native

; Validates whether a_target can be impersonated by the player.
; Returns: 0 = Valid, 1 = Invalid, 2 = Player, 3 = Dead/Dying, 4 = Child, 5 = Non-humanoid, 6 = Mounted, 7 = Ghost.
Int Function ValidateImpersonateTarget(Actor a_target) Global Native
