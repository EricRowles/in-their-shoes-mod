Scriptname ITS_QuestScript extends Quest

ITS_MCMScript Property ITS_MCM Auto

ReferenceAlias Property Alias_OriginalDonor Auto 	; Original Donor NPC
ReferenceAlias Property Alias_PlayerPuppet Auto 	; Temp Actor currently behaving as player

Spell Property ITS_ImpersonateSpell Auto
Spell Property ITS_RevertSpell Auto

ObjectReference Property ITS_StorageMarker Auto		; where to hide the nonused npcs
ObjectReference Property ITS_TempMarker Auto		; temp use to mark where an NPC is

Actor Property ITS_TempPlayer Auto				; single dragonborn puppet
Actor Property PlayerRef Auto					; the player!

; Initiation Function -----------------------------------------------------------------------------------
Event OnInit()
	ITS_Maintenance()
EndEvent

; Note: Quests do not receive engine OnPlayerLoadGame events; game reloads are
; forwarded to ITS_Maintenance() via ITS_PlayerScript on the Player alias.

; Maintenance Function -----------------------------------------------------------------------------------
Function ITS_Maintenance()
	If !PlayerRef.HasSpell(ITS_ImpersonateSpell)
		PlayerRef.AddSpell(ITS_ImpersonateSpell, False)
	EndIf
	; The session (donor, puppet) and the settings are restored natively from the co-save
	SyncWithPlugin()

	; For saves from before the plugin saved its settings
	ITS_MCM.PushConfig()
EndFunction

; The plugin decides whether the player is impersonating (ITS_NG.IsImpersonating). The aliases and the Revert spell
; follow its answer.
Function SyncWithPlugin()
	If ITS_NG.IsImpersonating()
		Actor donor = ITS_NG.GetDonor()
		If Alias_OriginalDonor.GetActorReference() != donor
			Alias_OriginalDonor.ForceRefTo(donor)
		EndIf
		Actor puppet = ITS_NG.GetPuppet()
		If puppet && Alias_PlayerPuppet.GetActorReference() != puppet
			Alias_PlayerPuppet.ForceRefTo(puppet)
			puppet.BlockActivation(True)
		EndIf
	ElseIf Alias_OriginalDonor.GetActorReference()
		; The plugin has no session (e.g. its co-save was lost): put the actors back as a revert would
		ReturnActors(Alias_OriginalDonor.GetActorReference(), Alias_PlayerPuppet.GetActorReference())
	EndIf
	UpdateRevertSpell()
EndFunction

; The player has the Revert spell exactly while impersonating
Function UpdateRevertSpell()
	If ITS_NG.IsImpersonating()
		If !PlayerRef.HasSpell(ITS_RevertSpell)
			PlayerRef.AddSpell(ITS_RevertSpell, False)
		EndIf
	ElseIf PlayerRef.HasSpell(ITS_RevertSpell)
		PlayerRef.RemoveSpell(ITS_RevertSpell)
	EndIf
EndFunction

; Why a target can't be impersonated, from a ValidateImpersonateTarget result (the codes are listed in ITS_NG.psc)
String Function ValidationMessage(Int a_code)
	If a_code == 3
		Return "Cannot impersonate: target is dead or dying."
	ElseIf a_code == 4
		Return "Cannot impersonate: target is a child."
	ElseIf a_code == 5
		Return "Cannot impersonate: target is not humanoid."
	ElseIf a_code == 6
		Return "Cannot impersonate: target is mounted."
	ElseIf a_code == 7
		Return "Cannot impersonate: target is a ghost."
	ElseIf a_code == 8
		Return "Cannot impersonate while transformed."
	ElseIf a_code == 9
		Return "Cannot impersonate: target is a temporary spawned actor."
	EndIf
	Return "Cannot impersonate this target."
EndFunction

; Fades -----------------------------------------------------------------------------------
; Fades with the vanilla FadeToBlackHoldImod, which holds black until removed. Game.FadeOutGame
; didn't hold for the whole swap in large load orders (#24).
ImageSpaceModifier Function FadeHoldImod()
	Return Game.GetFormFromFile(0x000F756E, "Skyrim.esm") as ImageSpaceModifier
EndFunction

Function FadeToBlack()
	FadeHoldImod().ApplyCrossFade(0.3)
	Utility.Wait(0.3)
EndFunction

Function FadeFromBlack()
	ImageSpaceModifier.RemoveCrossFade(0.4)
EndFunction

; Morph Complete Function -----------------------------------------------------------------------------------
Function ITS_ImpersonateComplete(Actor a_donor)
	If !a_donor || a_donor == PlayerRef
		Return
	EndIf

	Bool firstImpersonation = !ITS_NG.IsImpersonating()
	If !firstImpersonation && a_donor == ITS_NG.GetPuppet()
		ITS_Revert()
		Return
	EndIf

	Int validation = ITS_NG.ValidateImpersonateTarget(a_donor)
	If validation != 0
		Debug.Notification(ValidationMessage(validation))
		Return
	EndIf

	PlayerRef.StopCombat()
	a_donor.StopCombat()

	FadeToBlack()

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
	If firstImpersonation
		puppet.MoveTo(PlayerRef)
		Utility.Wait(0.1)
	EndIf

	If ITS_NG.Impersonate(a_donor, puppet)
		; The player takes the donor's spot
		PlayerRef.MoveTo(ITS_TempMarker)
		PlayerRef.StopCombat()
		SyncWithPlugin()
		a_donor.EvaluatePackage()	; the OriginalDonor alias's hold-position package keeps it at the storage marker
		Debug.Notification("Control Swapped")
	Else
		If firstImpersonation
			; Nothing changed natively, so put the puppet away again
			puppet.BlockActivation(False)
			puppet.MoveTo(ITS_StorageMarker)
			Alias_PlayerPuppet.Clear()
		EndIf
		Debug.Notification("Impersonation failed.")
	EndIf
	ITS_TempMarker.MoveTo(ITS_StorageMarker)

	FadeFromBlack()
EndFunction

; Revert Function -----------------------------------------------------------------------------------
Function ITS_Revert()
	If !ITS_NG.IsImpersonating()
		SyncWithPlugin()
		Return
	EndIf
	If ITS_NG.IsPlayerTransformed()
		Debug.Notification("Cannot return to your body while transformed.")
		Return
	EndIf
	Actor donor = ITS_NG.GetDonor()
	Actor puppet = ITS_NG.GetPuppet()

	FadeToBlack()

	; Restore state first so the native donor leash is off before anyone moves
	If !ITS_NG.Revert()
		FadeFromBlack()
		Debug.Notification("Couldn't return to your body.")
		Return
	EndIf
	ReturnActors(donor, puppet)

	FadeFromBlack()
	Debug.Notification("Returned to Body")
EndFunction

; Once the plugin has no session: the player returns to the puppet's spot, the donor takes the player's old spot,
; and the puppet is put away
Function ReturnActors(Actor a_donor, Actor a_puppet)
	ITS_TempMarker.MoveTo(PlayerRef)
	If a_puppet
		PlayerRef.MoveTo(a_puppet)
		a_puppet.BlockActivation(False)
		a_puppet.MoveTo(ITS_StorageMarker)
	EndIf
	PlayerRef.StopCombat()
	If a_donor
		a_donor.MoveTo(ITS_TempMarker)
		a_donor.StopCombat()
	EndIf
	ITS_TempMarker.MoveTo(ITS_StorageMarker)
	Alias_OriginalDonor.Clear()
	Alias_PlayerPuppet.Clear()
	UpdateRevertSpell()
EndFunction
