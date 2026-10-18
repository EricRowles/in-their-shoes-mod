Scriptname ITS_ImpersonateScript extends ActiveMagicEffect  
; This script allows the player to morph into the target
ITS_QuestScript property ITS_Quest Auto

Event OnEffectStart(Actor a_donor, Actor a_player)
	ITS_Quest.ITS_ImpersonateComplete(a_donor, a_player)
EndEvent
