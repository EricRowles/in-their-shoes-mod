Scriptname ITS_RevertScript extends ActiveMagicEffect  
; Ends player impersonation
ITS_QuestScript property ITS_Quest Auto

Event OnEffectStart(Actor a_target, Actor a_player)
	ITS_Quest.ITS_Revert(a_player)
EndEvent

