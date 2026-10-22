Scriptname ITS_MCMScript extends SKI_ConfigBase 

ITS_QuestScript property ITS_Quest auto
Actor Property PlayerRef Auto

; MCM Property Variables
bool Property IdleAIEnabled = True Auto
Float Property IdleAITimeout = 10.0 Auto
Float Property IdleChatterTime = 60.0 Auto
bool Property CombatAIEnabled = True Auto
bool Property NPCVoiceEnabled = True Auto
bool Property PuppetFollowsPlayer = True Auto

; Option IDs
int _oid_idleAI
int _oid_idleTimeout
int _oid_idleChatterTime
int _oid_combatAI
int _oid_npcVoice
int _oid_puppetFollows
int _oid_revert
bool _pendingRevert = false

int function GetVersion()
	return 1
endFunction

; Pushes every MCM setting to the native plugin. The settings are not saved natively, so this runs
; on first init, on version update and on every game load (via ITS_QuestScript.ITS_Maintenance).
function PushConfig()
	if IdleChatterTime < 10.0
		IdleChatterTime = 60.0
	endif

	ITS_NG.SetIdleAIEnabled(IdleAIEnabled)
	ITS_NG.SetIdleAITimeout(IdleAITimeout)
	ITS_NG.SetIdleChatterTime(IdleChatterTime)
	ITS_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
	ITS_NG.SetPuppetFollowsPlayer(PuppetFollowsPlayer)
endFunction

event OnVersionUpdate(int a_version)
	ModName = "In Their Shoes"
	Pages = None
	PushConfig()
endEvent

event OnConfigInit()
	ModName = "In Their Shoes"
	Pages = None
	PushConfig()
endEvent

function RefreshConfig()
	OnGameReload()
endFunction

event OnPageReset(string page)
	if IdleChatterTime < 10.0
		IdleChatterTime = 60.0
	endif
	SetCursorPosition(0)
	SetCursorFillMode(LEFT_TO_RIGHT)
	AddTextOption("Mod Version", ITS_Quest.ITS_ModVersion, OPTION_FLAG_DISABLED)
	AddTextOption("Status", "Config Active", OPTION_FLAG_DISABLED)
	AddEmptyOption()

	; Left Column: Autonomous AI & Gameplay Settings
	SetCursorPosition(4)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Autonomous AI")
	_oid_idleAI = AddToggleOption("Idle AI Pathing", IdleAIEnabled)
	_oid_idleTimeout = AddSliderOption("Idle AI Delay", IdleAITimeout, "{1} s")
	_oid_combatAI = AddToggleOption("Autonomous Combat AI", CombatAIEnabled, OPTION_FLAG_HIDDEN)

	; Right Column: Puppet & Voice Settings
	SetCursorPosition(5)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Impersonation Settings")
	_oid_npcVoice = AddToggleOption("NPC Voice", NPCVoiceEnabled)
	_oid_idleChatterTime = AddSliderOption("Idle Chatter Delay", IdleChatterTime, "{0} s")
	_oid_puppetFollows = AddToggleOption("Puppet Follows Player", PuppetFollowsPlayer)
	AddEmptyOption()

	If ITS_Quest.Alias_OriginalDonor.GetActorReference()
		_oid_revert = AddTextOption("Return to Body", "Click to Revert")
	Else
		_oid_revert = AddTextOption("Return to Body", "Already in Body", OPTION_FLAG_DISABLED)
	EndIf
endEvent

event OnOptionSelect(int option)
	if option == _oid_idleAI
		IdleAIEnabled = !IdleAIEnabled
		SetToggleOptionValue(_oid_idleAI, IdleAIEnabled)
		ITS_NG.SetIdleAIEnabled(IdleAIEnabled)
	elseif option == _oid_combatAI
		CombatAIEnabled = !CombatAIEnabled
		SetToggleOptionValue(_oid_combatAI, CombatAIEnabled)
	elseif option == _oid_npcVoice
		NPCVoiceEnabled = !NPCVoiceEnabled
		SetToggleOptionValue(_oid_npcVoice, NPCVoiceEnabled)
		ITS_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
	elseif option == _oid_puppetFollows
		PuppetFollowsPlayer = !PuppetFollowsPlayer
		SetToggleOptionValue(_oid_puppetFollows, PuppetFollowsPlayer)
		ITS_NG.SetPuppetFollowsPlayer(PuppetFollowsPlayer)
	elseif option == _oid_revert
		if ITS_Quest.Alias_OriginalDonor.GetActorReference()
			Debug.Notification("Close the menu to return to your body.")
			_pendingRevert = true
			SetTextOptionValue(_oid_revert, "Pending Menu Close...")
		endif
	endif
endEvent

event OnConfigClose()
	if _pendingRevert
		_pendingRevert = false
		RegisterForSingleUpdate(0.1)
	endif
endEvent

event OnUpdate()
	if ITS_Quest.Alias_OriginalDonor.GetActorReference()
		ITS_Quest.ITS_Revert(PlayerRef)
	endif
endEvent

event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
endEvent

event OnOptionSliderOpen(int option)
	if option == _oid_idleTimeout
		SetSliderDialogStartValue(IdleAITimeout)
		SetSliderDialogDefaultValue(10.0)
		SetSliderDialogRange(1.0, 120.0)
		SetSliderDialogInterval(1.0)
	elseif option == _oid_idleChatterTime
		SetSliderDialogStartValue(IdleChatterTime)
		SetSliderDialogDefaultValue(60.0)
		SetSliderDialogRange(10.0, 600.0)
		SetSliderDialogInterval(10.0)
	endif
endEvent

event OnOptionSliderAccept(int option, float value)
	if option == _oid_idleTimeout
		IdleAITimeout = value
		SetSliderOptionValue(_oid_idleTimeout, IdleAITimeout, "{1} s")
		ITS_NG.SetIdleAITimeout(IdleAITimeout)
	elseif option == _oid_idleChatterTime
		IdleChatterTime = value
		SetSliderOptionValue(_oid_idleChatterTime, IdleChatterTime, "{0} s")
		ITS_NG.SetIdleChatterTime(IdleChatterTime)
	endif
endEvent

event OnOptionHighlight(int option)
	if option == _oid_idleAI
		SetInfoText("Enables autonomous AI pathing on your player after a few seconds of inactivity.")
	elseif option == _oid_idleTimeout
		SetInfoText("Seconds of player inactivity before autonomous AI pathing begins.")
	elseif option == _oid_combatAI
		SetInfoText("Enables autonomous combat AI (attacking, blocking, distance closing) while idle in combat.")
	elseif option == _oid_npcVoice
		SetInfoText("Enables donor NPC voice comments and voice identity on the player while impersonating.")
	elseif option == _oid_idleChatterTime
		SetInfoText("Seconds between ambient idle dialogue chatter lines while impersonating an NPC (up to 10 minutes).")
	elseif option == _oid_puppetFollows
		SetInfoText("Controls whether the Dragonborn puppet follows you as a teammate.")
	elseif option == _oid_revert
		SetInfoText("Immediately return control back to your original character.")
	endif
endEvent
