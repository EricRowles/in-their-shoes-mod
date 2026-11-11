; Fills the ITS_PackageManager aliases, whose packages run on the player during idle AI. Called by the
; plugin (IdleAIManager), which passes the quest. The quest's priority puts its packages ahead of other
; quests' alias packages, such as a follower's Follow.
Scriptname ITS_IdlePackage Hidden

; Puts the player in alias a_aliasID only
Function Fill(Quest a_quest, Int a_aliasID) Global
	If !a_quest.IsRunning()
		a_quest.Start()
	EndIf
	ClearAliases(a_quest)
	ReferenceAlias chosen = a_quest.GetAlias(a_aliasID) as ReferenceAlias
	If chosen
		chosen.ForceRefTo(Game.GetPlayer())
	EndIf
	Game.GetPlayer().EvaluatePackage()
EndFunction

; Takes the player out of every alias
Function Release(Quest a_quest) Global
	ClearAliases(a_quest)
	Game.GetPlayer().EvaluatePackage()
EndFunction

Function ClearAliases(Quest a_quest) Global
	Int i = a_quest.GetNumAliases()
	While i > 0
		i -= 1
		ReferenceAlias packageAlias = a_quest.GetNthAlias(i) as ReferenceAlias
		If packageAlias
			packageAlias.Clear()
		EndIf
	EndWhile
EndFunction
