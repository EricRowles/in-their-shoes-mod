Scriptname ITS_NPCScript extends ReferenceAlias  

ITS_QuestScript property ITS_Quest Auto

Actor PlayerPuppetRef

Event OnActivate(ObjectReference a_actionRef)
	Debug.Trace("ITS_NPCScript: OnActivate triggered by " + a_actionRef)
	Actor player = Game.GetPlayer()
	If a_actionRef == player || (ITS_Quest && a_actionRef == ITS_Quest.PlayerRef)
		PlayerPuppetRef = GetActorReference()
		If !PlayerPuppetRef && ITS_Quest && ITS_Quest.Alias_PlayerPuppet
			PlayerPuppetRef = ITS_Quest.Alias_PlayerPuppet.GetActorReference()
		EndIf
		Debug.Trace("ITS_NPCScript: Opening self-dialogue with puppet: " + PlayerPuppetRef)
		Bool opened = ITS_NG.OpenSelfDialogue(PlayerPuppetRef)
		Debug.Trace("ITS_NPCScript: OpenSelfDialogue returned " + opened)
	EndIf
EndEvent
