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

; Initiation Function -----------------------------------------------------------------------------------
Event OnInit()
	ITS_Maintenance()
EndEvent

; Note: Quests do not receive engine OnPlayerLoadGame events; game reloads are
; forwarded to ITS_Maintenance() via ITS_PlayerScript on the Player alias.

; Maintenance Function -----------------------------------------------------------------------------------
function ITS_Maintenance()
	If !PlayerRef.HasSpell(ITS_ImpersonateSpell)
		PlayerRef.AddSpell(ITS_ImpersonateSpell, false)
	EndIf
	UpdateRevertSpell()

	; Synchronize SKSE Native settings from MCM properties on game load.
	; The impersonation session itself (donor, puppet) is restored natively from the co-save.
	ITS_MCM.PushConfig()
EndFunction

; The player has the Revert spell exactly while impersonating (while the OriginalDonor alias is filled)
function UpdateRevertSpell()
	If Alias_OriginalDonor.GetActorReference()
		If !PlayerRef.HasSpell(ITS_RevertSpell)
			PlayerRef.AddSpell(ITS_RevertSpell, false)
		EndIf
	ElseIf PlayerRef.HasSpell(ITS_RevertSpell)
		PlayerRef.RemoveSpell(ITS_RevertSpell)
	EndIf
EndFunction

; Why a target can't be impersonated, from a ValidateImpersonateTarget result (the codes are listed in ITS_NG.psc)
string function ValidationMessage(int a_code)
	if a_code == 3
		return "Cannot impersonate: target is dead or dying."
	elseif a_code == 4
		return "Cannot impersonate: target is a child."
	elseif a_code == 5
		return "Cannot impersonate: target is not humanoid."
	elseif a_code == 6
		return "Cannot impersonate: target is mounted."
	elseif a_code == 7
		return "Cannot impersonate: target is a ghost."
	elseif a_code == 8
		return "Cannot impersonate while transformed."
	endif
	return "Cannot impersonate this target."
EndFunction

; Morph Complete Function -----------------------------------------------------------------------------------
function ITS_ImpersonateComplete(Actor a_donor)
	if !a_donor || a_donor == PlayerRef
		return
	endif

	if a_donor == Alias_PlayerPuppet.GetActorReference()
		ITS_Revert()
		return
	EndIf

	int validation = ITS_NG.ValidateImpersonateTarget(a_donor)
	if validation != 0
		Debug.Notification(ValidationMessage(validation))
		return
	endif

	PlayerRef.StopCombat()
	a_donor.StopCombat()
	
	Game.FadeOutGame(true, true, 0.0, 0.3)
	
	; Ensure puppet alias is filled and vanilla activation is blocked
	Actor puppet = Alias_PlayerPuppet.GetActorReference()
	If !puppet
		Alias_PlayerPuppet.ForceRefTo(ITS_TempPlayer)
		puppet = Alias_PlayerPuppet.GetActorReference()
	EndIf
	puppet.BlockActivation(True)

	; Mark the donor's spot before Impersonate parks the donor at the storage marker
	ITS_TempMarker.MoveTo(a_donor)

	; On the first impersonation, the puppet takes the player's spot (in the loaded cell, so its 3D is attached
	; before morphing). On a donor-to-donor switch the puppet stays where it is and the old donor takes the player's spot.
	If !Alias_OriginalDonor.GetActorReference()
		puppet.moveto(PlayerRef)
		Utility.Wait(0.1)
	EndIf

	If ITS_NG.Impersonate(a_donor, puppet)
		; The player takes the donor's spot
		PlayerRef.moveto(ITS_TempMarker)
		PlayerRef.StopCombat()
		Alias_OriginalDonor.ForceRefTo(a_donor)
		UpdateRevertSpell()
		Debug.Notification("Control Swapped")
	Else
		Debug.Notification("Impersonation failed.")
	EndIf
	ITS_TempMarker.moveto(ITS_StorageMarker)

	Game.FadeOutGame(false, true, 0.0, 0.4)
EndFunction

; Revert Function -----------------------------------------------------------------------------------
function ITS_Revert()
	Actor donor = Alias_OriginalDonor.GetActorReference()
	if donor
		if ITS_NG.IsPlayerTransformed()
			Debug.Notification("Cannot return to your body while transformed.")
			return
		endif
		Actor puppet = Alias_PlayerPuppet.GetActorReference()

		Game.FadeOutGame(true, true, 0.0, 0.3)

		; Restore state first so the native donor leash is off before anyone moves
		ITS_TempMarker.MoveTo(PlayerRef)
		ITS_NG.Revert()

		; The player returns to the puppet's spot and the donor takes the player's old spot
		PlayerRef.moveto(puppet)
		PlayerRef.StopCombat()
		donor.moveto(ITS_TempMarker)
		donor.StopCombat()

		puppet.BlockActivation(False)
		puppet.moveto(ITS_StorageMarker)
		ITS_TempMarker.moveto(ITS_StorageMarker)
		Alias_OriginalDonor.Clear()
		Alias_PlayerPuppet.Clear()
		UpdateRevertSpell()

		Game.FadeOutGame(false, true, 0.0, 0.4)
		
		Debug.Notification("Returned to Body")
	EndIf
EndFunction
