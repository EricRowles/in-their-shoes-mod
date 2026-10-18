Scriptname ITS_PlayerScript extends ReferenceAlias  

ITS_QuestScript Property ITS_Quest Auto
ITS_MCMScript Property ITS_MCM Auto

Event OnPlayerLoadGame()
	If ITS_MCM
		ITS_MCM.RefreshConfig()
	EndIf
	If ITS_Quest
		ITS_Quest.ITS_Maintenance()
	EndIf
EndEvent

