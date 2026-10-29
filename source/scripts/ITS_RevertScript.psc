Scriptname ITS_RevertScript extends ActiveMagicEffect
; The Revert spell: ends the impersonation
ITS_QuestScript property ITS_Quest Auto

Event OnEffectStart(Actor a_target, Actor a_caster)
	ITS_Quest.ITS_Revert()
EndEvent
