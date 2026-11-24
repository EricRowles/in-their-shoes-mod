Scriptname ITS_MCMScript extends SKI_ConfigBase

ITS_QuestScript Property ITS_Quest Auto

; The settings live in the plugin (ITS_NG.GetSetting / SetSetting), which saves them in its co-save

; Option IDs
Int _oid_idleAI
Int _oid_idleTimeout
Int _oid_idlePackage
Int _oid_idleChatterTime
Int _oid_npcVoice
Int _oid_nativeRedirect
Int _oid_puppetFollows
Int _oid_revert
Bool _pendingRevert = False

Int Function GetVersion()
	Return 2
EndFunction

; Default, then the ITS_PackageManager aliases in alias ID order (the plugin's IdleAIPackage range matches)
String[] Function GetIdlePackageNames()
	String[] names = new String[3]
	names[0] = "Default"
	names[1] = "Sandbox (wide)"
	names[2] = "Sandbox (close)"
	Return names
EndFunction

Bool Function IsOn(String a_setting)
	Return ITS_NG.GetSetting(a_setting) != 0.0
EndFunction

; Flips a toggle setting and shows its new value. Pass a_noUpdate only when another option update without
; it follows: SkyUI doesn't redraw until then, so a lone toggle would look unchanged until the cursor moves.
Function Toggle(Int a_option, String a_setting, Bool a_noUpdate = False)
	Bool on = !IsOn(a_setting)
	If on
		ITS_NG.SetSetting(a_setting, 1.0)
	Else
		ITS_NG.SetSetting(a_setting, 0.0)
	EndIf
	SetToggleOptionValue(a_option, on, a_noUpdate)
EndFunction

; A section's options are disabled while its Enabled toggle is off
Int Function FlagsFor(String a_toggle)
	If IsOn(a_toggle)
		Return OPTION_FLAG_NONE
	EndIf
	Return OPTION_FLAG_DISABLED
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
EndFunction

Event OnPageReset(String page)
	SetCursorPosition(0)
	SetCursorFillMode(LEFT_TO_RIGHT)

	; Top: Mod Version (left), Return to Body (right)
	AddTextOption("Mod Version", ITS_NG.GetPluginVersion(), OPTION_FLAG_DISABLED)
	If ITS_NG.IsImpersonating()
		_oid_revert = AddTextOption("Impersonating: " + ITS_NG.GetDonorName(), "Click to Revert")
	Else
		_oid_revert = AddTextOption("Impersonating:", "Not Impersonating", OPTION_FLAG_DISABLED)
	EndIf

	; Gaps between sections use SetCursorPosition, not AddEmptyOption: SkyUI versions disagree on whether
	; AddEmptyOption returns an Int, and a mismatch logs a Papyrus error every time the page opens.

	; Left column: Idle Movement, then Advanced
	SetCursorPosition(4)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Idle Movement")
	_oid_idleAI = AddToggleOption("Enabled", IsOn("IdleAIEnabled"))
	_oid_idleTimeout = AddSliderOption("Idle Delay", ITS_NG.GetSetting("IdleAITimeout"), "{1} s", FlagsFor("IdleAIEnabled"))
	_oid_idlePackage = AddMenuOption("Idle Package", GetIdlePackageNames()[ITS_NG.GetSetting("IdleAIPackage") as Int], FlagsFor("IdleAIEnabled"))

	SetCursorPosition(14)
	AddHeaderOption("Advanced")
	_oid_nativeRedirect = AddToggleOption("Redirect Script Calls", IsOn("NativeRedirectEnabled"))

	; Right column: NPC Voice, then Dragonborn
	SetCursorPosition(5)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("NPC Voice")
	_oid_npcVoice = AddToggleOption("Enabled", IsOn("NPCVoiceEnabled"))
	_oid_idleChatterTime = AddSliderOption("Idle Chatter Delay", ITS_NG.GetSetting("IdleChatterTime"), "{0} s", FlagsFor("NPCVoiceEnabled"))

	SetCursorPosition(13)
	AddHeaderOption("Dragonborn")
	_oid_puppetFollows = AddToggleOption("Dragonborn Follows You", IsOn("PuppetFollowsPlayer"))
EndEvent

Event OnOptionSelect(Int option)
	If option == _oid_idleAI
		Toggle(option, "IdleAIEnabled", True)
		SetOptionFlags(_oid_idleTimeout, FlagsFor("IdleAIEnabled"), True)
		SetOptionFlags(_oid_idlePackage, FlagsFor("IdleAIEnabled"))
	ElseIf option == _oid_npcVoice
		Toggle(option, "NPCVoiceEnabled", True)
		SetOptionFlags(_oid_idleChatterTime, FlagsFor("NPCVoiceEnabled"))
	ElseIf option == _oid_nativeRedirect
		Toggle(option, "NativeRedirectEnabled")
	ElseIf option == _oid_puppetFollows
		Toggle(option, "PuppetFollowsPlayer")
	ElseIf option == _oid_revert
		If ITS_NG.IsImpersonating()
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
	If ITS_NG.IsImpersonating()
		ITS_Quest.ITS_Revert()
	EndIf
EndEvent

Event OnOptionKeyMapChange(Int option, Int keyCode, String conflictControl, String conflictName)
EndEvent

; The slider's setting, or "" for another option
String Function SliderSetting(Int a_option)
	If a_option == _oid_idleTimeout
		Return "IdleAITimeout"
	ElseIf a_option == _oid_idleChatterTime
		Return "IdleChatterTime"
	EndIf
	Return ""
EndFunction

Event OnOptionSliderOpen(Int option)
	String setting = SliderSetting(option)
	If setting != ""
		Float[] range = ITS_NG.GetSettingRange(setting)
		SetSliderDialogStartValue(ITS_NG.GetSetting(setting))
		SetSliderDialogDefaultValue(range[0])
		SetSliderDialogRange(range[1], range[2])
		If option == _oid_idleTimeout
			SetSliderDialogInterval(1.0)
		Else
			SetSliderDialogInterval(10.0)
		EndIf
	EndIf
EndEvent

Event OnOptionSliderAccept(Int option, Float value)
	String setting = SliderSetting(option)
	If setting != ""
		ITS_NG.SetSetting(setting, value)
		If option == _oid_idleTimeout
			SetSliderOptionValue(option, ITS_NG.GetSetting(setting), "{1} s")
		Else
			SetSliderOptionValue(option, ITS_NG.GetSetting(setting), "{0} s")
		EndIf
	EndIf
EndEvent

Event OnOptionMenuOpen(Int option)
	If option == _oid_idlePackage
		SetMenuDialogOptions(GetIdlePackageNames())
		SetMenuDialogStartIndex(ITS_NG.GetSetting("IdleAIPackage") as Int)
		SetMenuDialogDefaultIndex(0)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int option, Int index)
	If option == _oid_idlePackage && index >= 0
		ITS_NG.SetSetting("IdleAIPackage", index)
		SetMenuOptionValue(_oid_idlePackage, GetIdlePackageNames()[index])
	EndIf
EndEvent

Event OnOptionHighlight(Int option)
	If option == _oid_idleAI
		SetInfoText("Your character moves on its own after a few seconds of inactivity.")
	ElseIf option == _oid_idleTimeout
		SetInfoText("Seconds of player inactivity before idle movement begins.")
	ElseIf option == _oid_idlePackage
		SetInfoText("What you do when idle AI starts. Default runs the NPC's own packages. Any other choice runs instead of them, even while the NPC is a follower.")
	ElseIf option == _oid_npcVoice
		SetInfoText("Enables donor NPC voice comments and voice identity on the player while impersonating.")
	ElseIf option == _oid_idleChatterTime
		SetInfoText("Seconds between ambient idle dialogue chatter lines while impersonating an NPC (up to 10 minutes).")
	ElseIf option == _oid_nativeRedirect
		SetInfoText("Papyrus scripts target the player instead of the NPC. SKSE scripts are unaffected.\nDisabling may affect dialogue playback")
	ElseIf option == _oid_puppetFollows
		SetInfoText("Controls whether the Dragonborn follows you as a teammate.")
	ElseIf option == _oid_revert
		SetInfoText("Immediately return control back to your original character.")
	EndIf
EndEvent
