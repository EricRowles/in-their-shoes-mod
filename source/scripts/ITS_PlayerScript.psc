Scriptname ITS_PlayerScript extends ReferenceAlias
; On the Player alias: load-game maintenance. SKI_PlayerLoadGameAlias, on the same alias, reloads the MCM.

ITS_QuestScript Property ITS_Quest Auto

Event OnPlayerLoadGame()
	If ITS_Quest
		ITS_Quest.ITS_Maintenance()
	EndIf
EndEvent
