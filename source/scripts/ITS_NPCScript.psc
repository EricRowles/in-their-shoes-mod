Scriptname ITS_NPCScript extends ReferenceAlias
; On the PlayerPuppet alias: the player activating the puppet opens self-dialogue

Event OnActivate(ObjectReference a_actionRef)
	If a_actionRef == Game.GetPlayer()
		ITS_NG.OpenSelfDialogue(GetActorReference())
	EndIf
EndEvent
