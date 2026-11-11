Scriptname ITS_MCMScript extends SKI_ConfigBase

ITS_QuestScript Property ITS_Quest Auto
GlobalVariable Property ITS_PuppetFollows Auto	; gates the follow package on the PlayerPuppet alias

; MCM Property Variables
Bool Property IdleAIEnabled = True Auto
Float Property IdleAITimeout = 10.0 Auto
Int Property IdleAIPackage = 0 Auto
Int Property IdleTravelDestination = 0 Auto
Float Property IdleChatterTime = 60.0 Auto
Bool Property NPCVoiceEnabled = True Auto
Bool Property NativeRedirectEnabled = True Auto
Bool Property PuppetFollowsPlayer = True Auto

; Option IDs
Int _oid_idleAI
Int _oid_idleTimeout
Int _oid_idlePackage
Int _oid_travelDestination
Int _oid_idleChatterTime
Int _oid_npcVoice
Int _oid_nativeRedirect
Int _oid_puppetFollows
Int _oid_revert
Int _oid_scene
Int _oid_tempFactions
Int _oid_tempAliases
Bool _pendingRevert = False

Int Function GetVersion()
	Return 1
EndFunction

; Pushes every MCM setting to the native plugin. The settings are not saved natively, so this runs
; on first init, on version update and on every game load (via ITS_QuestScript.ITS_Maintenance).
Function PushConfig()
	; Below the slider's minimum, from an old save
	If IdleChatterTime < 10.0
		IdleChatterTime = 60.0
	EndIf
	If IdleAIPackage < 0 || IdleAIPackage >= GetIdlePackageNames().Length
		IdleAIPackage = 0
	EndIf
	If IdleTravelDestination < 0 || IdleTravelDestination >= GetTravelDestinationNames().Length
		IdleTravelDestination = 0
	EndIf

	ITS_NG.SetIdleAIEnabled(IdleAIEnabled)
	ITS_NG.SetIdleAITimeout(IdleAITimeout)
	; Destination first, so a Travel package filled by SetIdleAIPackage already has it
	ITS_NG.SetIdleTravelDestination(IdleTravelDestination)
	ITS_NG.SetIdleAIPackage(IdleAIPackage)
	ITS_NG.SetIdleChatterTime(IdleChatterTime)
	ITS_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
	ITS_NG.SetNativeRedirectEnabled(NativeRedirectEnabled)
	ApplyPuppetFollows()
EndFunction

Function ApplyPuppetFollows()
	If ITS_PuppetFollows
		If PuppetFollowsPlayer
			ITS_PuppetFollows.SetValue(1.0)
		Else
			ITS_PuppetFollows.SetValue(0.0)
		EndIf
	EndIf
	ITS_NG.SetPuppetFollowsPlayer(PuppetFollowsPlayer)
EndFunction

; Default, then the ITS_PackageManager aliases in alias ID order
String[] Function GetIdlePackageNames()
	String[] names = new String[4]
	names[0] = "Default"
	names[1] = "Sandbox (wide)"
	names[2] = "Sandbox (close)"
	names[3] = "Travel"
	Return names
EndFunction

Int Property IDLE_PACKAGE_TRAVEL = 3 AutoReadOnly

; In the plugin's order (Forms::travelDestinations)
String[] Function GetTravelDestinationNames()
	String[] names = new String[9]
	names[0] = "Whiterun"
	names[1] = "Solitude"
	names[2] = "Windhelm"
	names[3] = "Riften"
	names[4] = "Markarth"
	names[5] = "Falkreath"
	names[6] = "Morthal"
	names[7] = "Dawnstar"
	names[8] = "Winterhold"
	Return names
EndFunction

; Each section's options are disabled while its Enabled toggle is off
Int Function IdleOptionFlags()
	If IdleAIEnabled
		Return OPTION_FLAG_NONE
	EndIf
	Return OPTION_FLAG_DISABLED
EndFunction

Int Function TravelDestinationFlags()
	If IdleAIEnabled && IdleAIPackage == IDLE_PACKAGE_TRAVEL
		Return OPTION_FLAG_NONE
	EndIf
	Return OPTION_FLAG_DISABLED
EndFunction

Int Function VoiceOptionFlags()
	If NPCVoiceEnabled
		Return OPTION_FLAG_NONE
	EndIf
	Return OPTION_FLAG_DISABLED
EndFunction

Function UpdateIdleOptionFlags()
	SetOptionFlags(_oid_idleTimeout, IdleOptionFlags(), True)
	SetOptionFlags(_oid_idlePackage, IdleOptionFlags(), True)
	SetOptionFlags(_oid_travelDestination, TravelDestinationFlags())
EndFunction

Event OnVersionUpdate(Int a_version)
	SetUpMenu()
EndEvent

Event OnConfigInit()
	SetUpMenu()
EndEvent

Function SetUpMenu()
	ModName = "In Their Shoes"
	Pages = None
	PushConfig()
EndFunction

Event OnPageReset(String page)
	SetCursorPosition(0)
	SetCursorFillMode(LEFT_TO_RIGHT)

	; Top row: Mod Version (left), Return to Body (right)
	AddTextOption("Mod Version", ITS_NG.GetPluginVersion(), OPTION_FLAG_DISABLED)
	If ITS_Quest.Alias_OriginalDonor.GetActorReference()
		_oid_revert = AddTextOption("Return to Body", "Click to Revert")
	Else
		_oid_revert = AddTextOption("Return to Body", "Already in Body", OPTION_FLAG_DISABLED)
	EndIf

	; Gaps between sections use SetCursorPosition, not AddEmptyOption: SkyUI versions disagree on whether
	; AddEmptyOption returns an Int, and a mismatch logs a Papyrus error every time the page opens.

	; Left column: Idle Movement, then Advanced
	SetCursorPosition(4)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Idle Movement")
	_oid_idleAI = AddToggleOption("Enabled", IdleAIEnabled)
	_oid_idleTimeout = AddSliderOption("Idle Delay", IdleAITimeout, "{1} s", IdleOptionFlags())
	_oid_idlePackage = AddMenuOption("Idle Package", GetIdlePackageNames()[IdleAIPackage], IdleOptionFlags())
	_oid_travelDestination = AddMenuOption("Travel Destination", GetTravelDestinationNames()[IdleTravelDestination], TravelDestinationFlags())

	SetCursorPosition(16)
	AddHeaderOption("Advanced")
	_oid_nativeRedirect = AddToggleOption("Redirect Script Calls", NativeRedirectEnabled)

	; Right column: NPC Voice, then Dragonborn
	SetCursorPosition(5)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("NPC Voice")
	_oid_npcVoice = AddToggleOption("Enabled", NPCVoiceEnabled)
	_oid_idleChatterTime = AddSliderOption("Idle Chatter Delay", IdleChatterTime, "{0} s", VoiceOptionFlags())

	SetCursorPosition(13)
	AddHeaderOption("Dragonborn")
	_oid_puppetFollows = AddToggleOption("Dragonborn Follows You", PuppetFollowsPlayer)

	; Read when the page opens; the game is paused while the menu is open
	SetCursorPosition(17)
	AddHeaderOption("Informational")
	String sceneName = ITS_NG.GetCurrentSceneName()
	If sceneName == ""
		sceneName = "None"
	EndIf
	_oid_scene = AddTextOption("Current Scene", sceneName, OPTION_FLAG_DISABLED)
	_oid_tempFactions = AddTextOption("Temporary Factions", ITS_NG.GetTemporaryFactionCount() as String, OPTION_FLAG_DISABLED)
	_oid_tempAliases = AddTextOption("Temporary Aliases", ITS_NG.GetTemporaryAliasCount() as String, OPTION_FLAG_DISABLED)
EndEvent

Event OnOptionSelect(Int option)
	If option == _oid_idleAI
		IdleAIEnabled = !IdleAIEnabled
		SetToggleOptionValue(_oid_idleAI, IdleAIEnabled, True)
		UpdateIdleOptionFlags()
		ITS_NG.SetIdleAIEnabled(IdleAIEnabled)
	ElseIf option == _oid_npcVoice
		NPCVoiceEnabled = !NPCVoiceEnabled
		SetToggleOptionValue(_oid_npcVoice, NPCVoiceEnabled, True)
		SetOptionFlags(_oid_idleChatterTime, VoiceOptionFlags())
		ITS_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
	ElseIf option == _oid_nativeRedirect
		NativeRedirectEnabled = !NativeRedirectEnabled
		SetToggleOptionValue(_oid_nativeRedirect, NativeRedirectEnabled)
		ITS_NG.SetNativeRedirectEnabled(NativeRedirectEnabled)
	ElseIf option == _oid_puppetFollows
		PuppetFollowsPlayer = !PuppetFollowsPlayer
		SetToggleOptionValue(_oid_puppetFollows, PuppetFollowsPlayer)
		ApplyPuppetFollows()
	ElseIf option == _oid_revert
		If ITS_Quest.Alias_OriginalDonor.GetActorReference()
			Debug.Notification("Close the menu to return to your body.")
			_pendingRevert = True
			SetTextOptionValue(_oid_revert, "Pending Menu Close...")
		EndIf
	EndIf
EndEvent

Event OnConfigClose()
	If _pendingRevert
		_pendingRevert = False
		RegisterForSingleUpdate(0.1)
	EndIf
EndEvent

Event OnUpdate()
	If ITS_Quest.Alias_OriginalDonor.GetActorReference()
		ITS_Quest.ITS_Revert()
	EndIf
EndEvent

Event OnOptionKeyMapChange(Int option, Int keyCode, String conflictControl, String conflictName)
EndEvent

Event OnOptionSliderOpen(Int option)
	If option == _oid_idleTimeout
		SetSliderDialogStartValue(IdleAITimeout)
		SetSliderDialogDefaultValue(10.0)
		SetSliderDialogRange(1.0, 120.0)
		SetSliderDialogInterval(1.0)
	ElseIf option == _oid_idleChatterTime
		SetSliderDialogStartValue(IdleChatterTime)
		SetSliderDialogDefaultValue(60.0)
		SetSliderDialogRange(10.0, 600.0)
		SetSliderDialogInterval(10.0)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int option, Float value)
	If option == _oid_idleTimeout
		IdleAITimeout = value
		SetSliderOptionValue(_oid_idleTimeout, IdleAITimeout, "{1} s")
		ITS_NG.SetIdleAITimeout(IdleAITimeout)
	ElseIf option == _oid_idleChatterTime
		IdleChatterTime = value
		SetSliderOptionValue(_oid_idleChatterTime, IdleChatterTime, "{0} s")
		ITS_NG.SetIdleChatterTime(IdleChatterTime)
	EndIf
EndEvent

Event OnOptionMenuOpen(Int option)
	If option == _oid_idlePackage
		SetMenuDialogOptions(GetIdlePackageNames())
		SetMenuDialogStartIndex(IdleAIPackage)
		SetMenuDialogDefaultIndex(0)
	ElseIf option == _oid_travelDestination
		SetMenuDialogOptions(GetTravelDestinationNames())
		SetMenuDialogStartIndex(IdleTravelDestination)
		SetMenuDialogDefaultIndex(0)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int option, Int index)
	If option == _oid_idlePackage && index >= 0
		IdleAIPackage = index
		SetMenuOptionValue(_oid_idlePackage, GetIdlePackageNames()[index])
		SetOptionFlags(_oid_travelDestination, TravelDestinationFlags())
		ITS_NG.SetIdleAIPackage(IdleAIPackage)
	ElseIf option == _oid_travelDestination && index >= 0
		IdleTravelDestination = index
		SetMenuOptionValue(_oid_travelDestination, GetTravelDestinationNames()[index])
		ITS_NG.SetIdleTravelDestination(IdleTravelDestination)
	EndIf
EndEvent

Event OnOptionHighlight(Int option)
	If option == _oid_idleAI
		SetInfoText("Your character moves on its own after a few seconds of inactivity.")
	ElseIf option == _oid_idleTimeout
		SetInfoText("Seconds of player inactivity before idle movement begins.")
	ElseIf option == _oid_idlePackage
		SetInfoText("What you do when idle AI starts. Default runs the NPC's own packages. Any other choice runs instead of them, even while the NPC is a follower.")
	ElseIf option == _oid_travelDestination
		SetInfoText("Where you go when idle movement starts with the Travel package. Once there, you wander the town.")
	ElseIf option == _oid_npcVoice
		SetInfoText("Enables donor NPC voice comments and voice identity on the player while impersonating.")
	ElseIf option == _oid_idleChatterTime
		SetInfoText("Seconds between ambient idle dialogue chatter lines while impersonating an NPC (up to 10 minutes).")
	ElseIf option == _oid_nativeRedirect
		SetInfoText("Papyrus scripts target the player instead of the NPC. SKSE scripts are unaffected.\nDisabling may affect dialogue playback")
	ElseIf option == _oid_puppetFollows
		SetInfoText("Controls whether the Dragonborn puppet follows you as a teammate.")
	ElseIf option == _oid_scene
		SetInfoText("The scene you're in, if any.")
	ElseIf option == _oid_tempFactions
		SetInfoText("The NPC's factions you've joined while impersonating them. They go back to the NPC when you return to your body.")
	ElseIf option == _oid_tempAliases
		SetInfoText("The NPC's quest roles you've taken over while impersonating them. They go back to the NPC when you return to your body.")
	ElseIf option == _oid_revert
		SetInfoText("Immediately return control back to your original character.")
	EndIf
EndEvent
