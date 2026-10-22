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
	ITS_MCM.PushConfig()

	If OriginalDonorRef
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

	a_player.StopCombat()
	a_donor.StopCombat()
	
	Game.FadeOutGame(true, true, 0.0, 0.3)
	
	; Ensure puppet alias is filled and vanilla activation is blocked
	Actor currentPuppet = Alias_PlayerPuppet.GetActorReference()
	If !currentPuppet
		currentPuppet = ITS_TempPlayer
		Alias_PlayerPuppet.ForceRefTo(currentPuppet)
		currentPuppet = Alias_PlayerPuppet.GetActorReference()
	EndIf
	PlayerPuppetRef = currentPuppet
	PlayerPuppetRef.BlockActivation(True)

	; Handle donor-to-donor switch vs initial Dragonborn-to-donor impersonation
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()
	If OriginalDonorRef
		ITS_NG.EndImpersonation()
		OriginalDonorRef.moveto(a_donor)
		OriginalDonorRef.StopCombat()
		Utility.Wait(0.1)
		ITS_NG.RestoreInventoryFromPlayer(PlayerRef, OriginalDonorRef, None)
		ITS_NG.CopyStats(PlayerRef, OriginalDonorRef)
		OriginalDonorRef.EvaluatePackage()
		ITS_NG.ApplyDonorProfile(a_donor, PlayerRef, None)
	Else
		; Move puppet into the loaded cell before morphing so its 3D is attached
		PlayerPuppetRef.moveto(a_donor)
		Utility.Wait(0.1)
		ITS_NG.ApplyDonorProfile(a_donor, PlayerRef, PlayerPuppetRef)
	EndIf
	
	Alias_OriginalDonor.ForceRefTo(a_donor)
	a_donor.moveto(ITS_StorageMarker)
	
	If !PlayerRef.HasSpell(ITS_RevertSpell)
		PlayerRef.addspell(ITS_RevertSpell, False)
	EndIf 
	
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

		ITS_NG.EndImpersonation()
		
		ITS_TempMarker.MoveTo(a_player)
		a_player.moveto(PlayerPuppetRef)
		
		a_player.StopCombat()

		OriginalDonorRef.moveto(ITS_TempMarker)
		OriginalDonorRef.StopCombat()
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
