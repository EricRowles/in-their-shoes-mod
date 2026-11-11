; Fills the ITS_PackageManager aliases, whose packages run on the player during idle AI. Called by the
; plugin (IdleAIManager), which passes the quest. The quest's priority puts its packages ahead of other
; quests' alias packages, such as a follower's Follow.
Scriptname ITS_IdlePackage Hidden

; Puts the player in alias a_aliasID only. a_destination goes in alias a_destinationAliasID (TravelDestination),
; which the Travel package travels to; pass None or -1 to leave it empty.
Function Fill(Quest a_quest, Int a_aliasID, ObjectReference a_destination, Int a_destinationAliasID) Global
	If !a_quest.IsRunning()
		a_quest.Start()
	EndIf
	ClearAliases(a_quest)
	If a_destination && a_destinationAliasID >= 0
		ReferenceAlias destinationAlias = a_quest.GetAlias(a_destinationAliasID) as ReferenceAlias
		If destinationAlias
			destinationAlias.ForceRefTo(a_destination)
		EndIf
	EndIf
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
