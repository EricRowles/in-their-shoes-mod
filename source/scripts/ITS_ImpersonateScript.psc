Scriptname ITS_ImpersonateScript extends ActiveMagicEffect
; The Impersonate spell: the player takes over the target
ITS_QuestScript Property ITS_Quest Auto

Event OnEffectStart(Actor a_target, Actor a_caster)
	ITS_Quest.ITS_ImpersonateComplete(a_target)
EndEvent
