; Native bridge for In Their Shoes NG SKSE64 Plugin
Scriptname ITS_NG Hidden

; Impersonates a_donor (or switches to a_donor if already impersonating someone else).
; On the first impersonation the puppet takes the Dragonborn's appearance, stats and gear.
; The donor is moved to the storage marker. Position the puppet before calling.
Bool Function Impersonate(Actor a_donor, Actor a_puppet) Global Native

; Ends impersonation: the donor gets the player's current items, spells and stats, and the player
; gets their own appearance, stats, spells and gear back. Move the actors afterwards.
Bool Function Revert() Global Native

; Settings, by name: IdleAIEnabled, IdleAITimeout, IdleAIPackage, NPCVoiceEnabled, IdleChatterTime,
; NativeRedirectEnabled, PuppetFollowsPlayer (Settings.h in the plugin). Toggles are 0 or 1. The plugin owns them and
; saves them in its co-save; values are clamped to the setting's range.
Float Function GetSetting(String a_name) Global Native
Function SetSetting(String a_name, Float a_value) Global Native
; The setting's default, minimum and maximum
Float[] Function GetSettingRange(String a_name) Global Native

; The scene the player is in: its editor ID (or form ID, when editor IDs aren't loaded), or "" when there is none
String Function GetCurrentSceneName() Global Native

; How many of the donor's factions the player was given for the session (not counting the aliases' factions)
Int Function GetTemporaryFactionCount() Global Native

; How many of the donor's quest aliases the player holds for the session
Int Function GetTemporaryAliasCount() Global Native

; Opens the Dialogue Menu with the puppet, showing the donor's topics.
Bool Function OpenSelfDialogue(Actor a_speaker = None) Global Native

; Validates whether a_target can be impersonated by the player.
; Returns: 0 = Valid, 1 = Invalid, 2 = Player, 3 = Dead/Dying, 4 = Child, 5 = Non-humanoid, 6 = Mounted, 7 = Ghost,
; 8 = Player is transformed (see IsPlayerTransformed), 9 = Spawned actor (a temporary FF reference).
Int Function ValidateImpersonateTarget(Actor a_target) Global Native

; Whether the player is in Werewolf or Vampire Lord form, or changing into or out of it.
; Impersonate and Revert refuse while it is true.
Bool Function IsPlayerTransformed() Global Native

; Whether the player is impersonating. The plugin owns this answer; ITS_QuestScript keeps its aliases in line with it.
Bool Function IsImpersonating() Global Native

; The donor and puppet while impersonating, otherwise None
Actor Function GetDonor() Global Native
Actor Function GetPuppet() Global Native

; The donor's name; "None" when not impersonating, "Not Found" when a saved session's donor no longer exists
String Function GetDonorName() Global Native

; The plugin's version, e.g. "0.5.0"
String Function GetPluginVersion() Global Native
