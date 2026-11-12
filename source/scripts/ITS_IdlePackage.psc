; Sets idle AI's state for the player: whether it is AI-driven, and which ITS_PackageManager alias it is in.
; Called by the plugin (IdleAIManager), which passes the quest. The quest's priority puts its packages ahead of
; other quests' alias packages, such as a follower's Follow.
Scriptname ITS_IdlePackage Hidden

; Puts the player in alias a_aliasID only (none if -1), and a_destination in alias a_destinationAliasID
; (TravelDestination), which the Travel package travels to (left empty if None or -1). Each call carries the whole
; state and a sequence number: a call that runs after a later one was sent does nothing, so the order the calls
; run in doesn't matter.
Function Apply(Quest a_quest, Bool a_aiDriven, Int a_aliasID, ObjectReference a_destination, Int a_destinationAliasID, Int a_sequence) Global
	Int check = ITS_NG.BeginIdlePackageApply(a_sequence, a_aiDriven)
	If check == 0
		Return
	EndIf

	Actor player = Game.GetPlayer()
	If a_quest
		If a_aliasID >= 0 && !a_quest.IsRunning()
			a_quest.Start()
		EndIf
		ClearAliases(a_quest)
		If a_aliasID >= 0
			If a_destination && a_destinationAliasID >= 0
				ReferenceAlias destinationAlias = a_quest.GetAlias(a_destinationAliasID) as ReferenceAlias
				If destinationAlias
					destinationAlias.ForceRefTo(a_destination)
				EndIf
			EndIf
			ReferenceAlias chosen = a_quest.GetAlias(a_aliasID) as ReferenceAlias
			If chosen
				chosen.ForceRefTo(player)
			EndIf
		EndIf
	EndIf
	; Only when it changes: the plugin tracks what was last passed
	If check == 2
		Game.SetPlayerAIDriven(a_aiDriven)
	EndIf
	player.EvaluatePackage()
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
