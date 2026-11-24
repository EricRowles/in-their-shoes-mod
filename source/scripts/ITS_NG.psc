; Native bridge for In Their Shoes NG SKSE64 Plugin
Scriptname ITS_NG Hidden

; Impersonate and Revert return "" on success, otherwise why they failed (a message for the player). Both refuse
; while the player is in Werewolf or Vampire Lord form.

; Impersonates a_donor (or switches to a_donor if already impersonating someone else).
; On the first impersonation the puppet takes the Dragonborn's appearance, stats and gear.
; The donor is moved to the storage marker. Position the puppet before calling.
String Function Impersonate(Actor a_donor, Actor a_puppet) Global Native

; Ends impersonation: the donor gets the player's current items, spells and stats, and the player
; gets their own appearance, stats, spells and gear back. Move the actors afterwards.
String Function Revert() Global Native

; Settings, by name: IdleAIEnabled, IdleAITimeout, IdleAIPackage, NPCVoiceEnabled, IdleChatterTime,
; NativeRedirectEnabled, PuppetFollowsPlayer (Settings.h in the plugin). Toggles are 0 or 1. The plugin owns them and
; saves them in its co-save; values are clamped to the setting's range.
Float Function GetSetting(String a_name) Global Native
Function SetSetting(String a_name, Float a_value) Global Native
; The setting's default, minimum and maximum
Float[] Function GetSettingRange(String a_name) Global Native

; Opens the Dialogue Menu with the puppet, showing the donor's topics.
Bool Function OpenSelfDialogue(Actor a_speaker = None) Global Native

; Why the player can't impersonate a_target (a message for the player), or "" if they can.
; Whether the player is transformed is checked by Impersonate.
String Function ValidateImpersonateTarget(Actor a_target) Global Native

; Whether the player is impersonating. The plugin owns this answer; ITS_QuestScript keeps its aliases in line with it.
Bool Function IsImpersonating() Global Native

; The donor and puppet while impersonating, otherwise None
Actor Function GetDonor() Global Native
Actor Function GetPuppet() Global Native

; The donor's name; "None" when not impersonating, "Not Found" when a saved session's donor no longer exists
String Function GetDonorName() Global Native

; The plugin's version, e.g. "0.5.0"
String Function GetPluginVersion() Global Native
