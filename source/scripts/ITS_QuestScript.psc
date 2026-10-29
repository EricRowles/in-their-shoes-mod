Scriptname ITS_QuestScript extends Quest  

ITS_MCMScript property ITS_MCM Auto

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

	; Synchronize SKSE Native settings from MCM properties on game load.
	; The impersonation session itself (donor, puppet) is restored natively from the co-save.
	ITS_MCM.PushConfig()
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
		elseif validation == 8
			Debug.Notification("Cannot impersonate while transformed.")
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
		Alias_PlayerPuppet.ForceRefTo(ITS_TempPlayer)
		currentPuppet = Alias_PlayerPuppet.GetActorReference()
	EndIf
	PlayerPuppetRef = currentPuppet
	PlayerPuppetRef.BlockActivation(True)

	; Mark the donor's spot before Impersonate parks the donor at the storage marker
	ITS_TempMarker.MoveTo(a_donor)

	; On the first impersonation, the puppet takes the player's spot (in the loaded cell, so its 3D is attached
	; before morphing). On a donor-to-donor switch the puppet stays where it is and the old donor takes the player's spot.
	If !Alias_OriginalDonor.GetActorReference()
		PlayerPuppetRef.moveto(a_player)
		Utility.Wait(0.1)
	EndIf

	If ITS_NG.Impersonate(a_donor, PlayerPuppetRef)
		; The player takes the donor's spot
		a_player.moveto(ITS_TempMarker)
		a_player.StopCombat()
		Alias_OriginalDonor.ForceRefTo(a_donor)
		If !PlayerRef.HasSpell(ITS_RevertSpell)
			PlayerRef.addspell(ITS_RevertSpell, False)
		EndIf
		Debug.Notification("Control Swapped")
	Else
		Debug.Notification("Impersonation failed.")
	EndIf
	ITS_TempMarker.moveto(ITS_StorageMarker)

	Game.FadeOutGame(false, true, 0.0, 0.4)
EndFunction

; Revert Function -----------------------------------------------------------------------------------
function ITS_Revert(actor a_player)
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()
	if OriginalDonorRef
		if ITS_NG.IsPlayerTransformed()
			Debug.Notification("Cannot return to your body while transformed.")
			return
		endif
		PlayerPuppetRef = Alias_PlayerPuppet.GetActorReference()

		Game.FadeOutGame(true, true, 0.0, 0.3)

		; Restore state first so the native donor leash is off before anyone moves
		ITS_TempMarker.MoveTo(a_player)
		ITS_NG.Revert()
		a_player.RemoveSpell(ITS_RevertSpell)

		; The player returns to the puppet's spot and the donor takes the player's old spot
		a_player.moveto(PlayerPuppetRef)
		a_player.StopCombat()
		OriginalDonorRef.moveto(ITS_TempMarker)
		OriginalDonorRef.StopCombat()

		PlayerPuppetRef.BlockActivation(False)
		PlayerPuppetRef.moveto(ITS_StorageMarker)
		ITS_TempMarker.moveto(ITS_StorageMarker)
		Alias_OriginalDonor.Clear()
		Alias_PlayerPuppet.Clear()

		Game.FadeOutGame(false, true, 0.0, 0.4)
		
		Debug.Notification("Returned to Body")
	EndIf
EndFunction
