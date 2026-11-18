; Sets which ITS_PackageManager alias the player is in while idle AI runs. Called by the plugin (IdleAIManager),
; which passes the quest. The quest's priority puts its packages ahead of other quests' alias packages, such as a
; follower's Follow. The plugin turns AI-driven on and off itself; it turns it on once this call has finished.
Scriptname ITS_IdlePackage Hidden

; Puts the player in alias a_aliasID only (none if -1), and a_destination in alias a_destinationAliasID
; (TravelDestination), which the Travel package travels to (left empty if None or -1). Each call carries the whole
; state and a sequence number: a call that runs after a later one was sent does nothing, so the order the calls
; run in doesn't matter.
Function Apply(Quest a_quest, Int a_aliasID, ObjectReference a_destination, Int a_destinationAliasID, Int a_sequence) Global
	If !ITS_NG.BeginIdlePackageApply(a_sequence)
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
	player.EvaluatePackage()
	ITS_NG.EndIdlePackageApply(a_sequence)
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
