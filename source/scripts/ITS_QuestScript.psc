Scriptname ITS_QuestScript extends Quest  

ITS_MCMScript property ITS_MCM Auto

float property ITS_ModVersion = 1.0 Auto

ReferenceAlias Property Alias_OriginalDonor Auto 	; Original Donor NPC
ReferenceAlias Property Alias_PlayerPuppet Auto 	; Temp Actor currently behaving as player

Spell Property ITS_ImpersonateSpell Auto
Spell Property ITS_RevertSpell Auto

ObjectReference property ITS_StorageMarker Auto		; where to hide the nonused npcs
ObjectReference property ITS_TempMarker Auto		; temp use to mark where an NPC is

actor Property ITS_TempPlayer Auto				; single dragonborn puppet
actor Property PlayerRef Auto					; the player!

actor PlayerPuppetRef
actor OriginalDonorRef

; Initiation Function -----------------------------------------------------------------------------------
Event OnInit()
	ITS_Maintenance()
EndEvent

; Note: Quests do not receive engine OnPlayerLoadGame events; game reloads are
; forwarded to ITS_Maintenance() via ITS_PlayerScript on the Player alias.

; Maintenance Function -----------------------------------------------------------------------------------
function ITS_Maintenance()
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()

	If !PlayerRef.HasSpell(ITS_ImpersonateSpell)
		PlayerRef.AddSpell(ITS_ImpersonateSpell, false)
	EndIf

	If OriginalDonorRef
		If !PlayerRef.HasSpell(ITS_RevertSpell)
			PlayerRef.AddSpell(ITS_RevertSpell, false)
		EndIf
	ElseIf PlayerRef.HasSpell(ITS_RevertSpell)
		PlayerRef.RemoveSpell(ITS_RevertSpell)
	EndIf

	; Synchronize SKSE Native settings from MCM properties on game load
	ITS_NG.SetIdleAIEnabled(ITS_MCM.IdleAIEnabled)
	ITS_NG.SetIdleAITimeout(ITS_MCM.IdleAITimeout)
	float chatterDelay = ITS_MCM.IdleChatterTime
	if chatterDelay < 10.0
		chatterDelay = 60.0
		ITS_MCM.IdleChatterTime = 60.0
	endif
	ITS_NG.SetIdleChatterTime(chatterDelay)
	ITS_NG.SetNPCVoiceEnabled(ITS_MCM.NPCVoiceEnabled)
	ITS_NG.SetPuppetFollowsPlayer(ITS_MCM.PuppetFollowsPlayer)

	If OriginalDonorRef
		ITS_NG.SetControllingNPC(True)
		PlayerPuppetRef = Alias_PlayerPuppet.GetActorReference()
		If PlayerPuppetRef
			ITS_NG.SetPuppetActor(PlayerPuppetRef)
		EndIf
	EndIf
EndFunction

; Morph Complete Function -----------------------------------------------------------------------------------
function ITS_ImpersonateComplete(Actor a_donor, Actor a_player)
	if !a_donor || a_donor == a_player
		return
	endif

	if a_donor == Alias_PlayerPuppet.GetActorReference()
		ITS_Revert(a_player)
		return
	EndIf

	int validation = ITS_NG.ValidateImpersonateTarget(a_donor)
	if validation != 0
		if validation == 3
			Debug.Notification("Cannot impersonate: target is dead or dying.")
		elseif validation == 4
			Debug.Notification("Cannot impersonate: target is a child.")
		elseif validation == 5
			Debug.Notification("Cannot impersonate: target is not humanoid.")
		elseif validation == 6
			Debug.Notification("Cannot impersonate: target is mounted.")
		elseif validation == 7
			Debug.Notification("Cannot impersonate: target is a ghost.")
		else
			Debug.Notification("Cannot impersonate this target.")
		endif
		return
	endif

	; Capture original player name before any appearance or trait cloning occurs
	String origPlayerName = ITS_NG.GetOriginalPlayerName()
	If origPlayerName == ""
		origPlayerName = PlayerRef.GetActorBase().GetName()
	EndIf

	a_player.StopCombat()
	a_donor.StopCombat()
	
	Game.FadeOutGame(true, true, 0.0, 0.3)
	
	; Dynamically select puppet actor
	Actor currentPuppet = Alias_PlayerPuppet.GetActorReference()
	If !currentPuppet
		currentPuppet = ITS_TempPlayer
		Alias_PlayerPuppet.ForceRefTo(currentPuppet)
		currentPuppet = Alias_PlayerPuppet.GetActorReference()
	EndIf
	PlayerPuppetRef = currentPuppet

	; Configure the puppet's sex, name, and appearance BEFORE moving into the player's loaded cell
	PlayerPuppetRef.SetDisplayName(origPlayerName, True)
	PlayerPuppetRef.BlockActivation(True)
	ITS_NG.SetPuppetActor(PlayerPuppetRef)

	; Transfer traits from PC back to orignpc before getting new traits if we were already controlling an NPC
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()
	If OriginalDonorRef
		ITS_NG.EndImpersonation()
		OriginalDonorRef.moveto(a_donor)
		OriginalDonorRef.StopCombat()
		Utility.Wait(0.1)
		ITS_NG.RestoreInventoryFromPlayer(PlayerRef, OriginalDonorRef, None)
		ITS_NG.CopyStats(PlayerRef, OriginalDonorRef)
		OriginalDonorRef.EvaluatePackage()
	Else
		PlayerPuppetRef.moveto(a_donor)
		Utility.Wait(0.1) ; Allow 3D node attachment in loaded cell
	EndIf
	
	; Make the player look and have stats like the donor NPC
	If OriginalDonorRef
		ITS_NG.ApplyDonorProfile(a_donor, PlayerRef, None)
	Else
		ITS_NG.CopyStats(PlayerRef, PlayerPuppetRef)	
		ITS_NG.ApplyDonorProfile(a_donor, PlayerRef, PlayerPuppetRef)
	EndIf
	
	Alias_OriginalDonor.ForceRefTo(a_donor)
	a_donor.moveto(ITS_StorageMarker)
	
	If !PlayerRef.HasSpell(ITS_RevertSpell)
		PlayerRef.addspell(ITS_RevertSpell, False)
	EndIf 
	
	ITS_NG.SetControllingNPC(True)
	ITS_NG.BeginImpersonation(a_donor, PlayerRef)

	Game.FadeOutGame(false, true, 0.0, 0.4)

	Debug.Notification("Control Swapped")
EndFunction

; Revert Function -----------------------------------------------------------------------------------
function ITS_Revert(actor a_player)
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()
	if OriginalDonorRef
		PlayerPuppetRef = Alias_PlayerPuppet.GetActorReference()

		Game.FadeOutGame(true, true, 0.0, 0.3)

		ITS_NG.SetControllingNPC(False)
		ITS_NG.EndImpersonation()		
		
		ITS_TempMarker.MoveTo(a_player)
		a_player.moveto(PlayerPuppetRef)
		
		a_player.StopCombat()

		if OriginalDonorRef
			OriginalDonorRef.moveto(a_player)
			OriginalDonorRef.StopCombat()
		EndIf
		a_player.RemoveSpell(ITS_RevertSpell)
		
		ITS_NG.RestorePlayerProfile(PlayerRef, OriginalDonorRef, PlayerPuppetRef)

		PlayerPuppetRef.BlockActivation(False)
		ITS_NG.SetPuppetActor(None)
		PlayerPuppetRef.moveto(ITS_StorageMarker)
		ITS_TempMarker.moveto(ITS_StorageMarker)
		Alias_OriginalDonor.Clear()
		Alias_PlayerPuppet.Clear()

		Game.FadeOutGame(false, true, 0.0, 0.4)
		
		Debug.Notification("Returned to Body")
	EndIf
EndFunction

