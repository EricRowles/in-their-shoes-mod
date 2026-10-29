Scriptname ITS_MCMScript extends SKI_ConfigBase

ITS_QuestScript Property ITS_Quest Auto
GlobalVariable Property ITS_PuppetFollows Auto	; gates the follow package on the PlayerPuppet alias

; MCM Property Variables
Bool Property IdleAIEnabled = True Auto
Float Property IdleAITimeout = 10.0 Auto
Float Property IdleChatterTime = 60.0 Auto
Bool Property NPCVoiceEnabled = True Auto
Bool Property PuppetFollowsPlayer = True Auto

; Option IDs
Int _oid_idleAI
Int _oid_idleTimeout
Int _oid_idleChatterTime
Int _oid_npcVoice
Int _oid_puppetFollows
Int _oid_revert
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

	ITS_NG.SetIdleAIEnabled(IdleAIEnabled)
	ITS_NG.SetIdleAITimeout(IdleAITimeout)
	ITS_NG.SetIdleChatterTime(IdleChatterTime)
	ITS_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
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
	AddTextOption("Mod Version", ITS_NG.GetPluginVersion(), OPTION_FLAG_DISABLED)

	; Left Column: Autonomous AI & Gameplay Settings
	SetCursorPosition(4)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Autonomous AI")
	_oid_idleAI = AddToggleOption("Idle AI Pathing", IdleAIEnabled)
	_oid_idleTimeout = AddSliderOption("Idle AI Delay", IdleAITimeout, "{1} s")

	; Right Column: Puppet & Voice Settings
	SetCursorPosition(5)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Impersonation Settings")
	_oid_npcVoice = AddToggleOption("NPC Voice", NPCVoiceEnabled)
	_oid_idleChatterTime = AddSliderOption("Idle Chatter Delay", IdleChatterTime, "{0} s")
	_oid_puppetFollows = AddToggleOption("Puppet Follows Player", PuppetFollowsPlayer)
	; Leave a gap before Return to Body. (Not AddEmptyOption: SkyUI versions disagree on whether it returns
	; an Int, and a mismatch logs a Papyrus error every time the page opens.)
	SetCursorPosition(15)

	If ITS_Quest.Alias_OriginalDonor.GetActorReference()
		_oid_revert = AddTextOption("Return to Body", "Click to Revert")
	Else
		_oid_revert = AddTextOption("Return to Body", "Already in Body", OPTION_FLAG_DISABLED)
	EndIf
EndEvent

Event OnOptionSelect(Int option)
	If option == _oid_idleAI
		IdleAIEnabled = !IdleAIEnabled
		SetToggleOptionValue(_oid_idleAI, IdleAIEnabled)
		ITS_NG.SetIdleAIEnabled(IdleAIEnabled)
	ElseIf option == _oid_npcVoice
		NPCVoiceEnabled = !NPCVoiceEnabled
		SetToggleOptionValue(_oid_npcVoice, NPCVoiceEnabled)
		ITS_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
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

Event OnOptionHighlight(Int option)
	If option == _oid_idleAI
		SetInfoText("Enables autonomous AI pathing on your player after a few seconds of inactivity.")
	ElseIf option == _oid_idleTimeout
		SetInfoText("Seconds of player inactivity before autonomous AI pathing begins.")
	ElseIf option == _oid_npcVoice
		SetInfoText("Enables donor NPC voice comments and voice identity on the player while impersonating.")
	ElseIf option == _oid_idleChatterTime
		SetInfoText("Seconds between ambient idle dialogue chatter lines while impersonating an NPC (up to 10 minutes).")
	ElseIf option == _oid_puppetFollows
		SetInfoText("Controls whether the Dragonborn puppet follows you as a teammate.")
	ElseIf option == _oid_revert
		SetInfoText("Immediately return control back to your original character.")
	EndIf
EndEvent
