#pragma once

namespace InTheirShoes::QuestConditions {

    // Evaluates a condition list in the context of its quest, so conditions whose parameters or run-on
    // target are quest aliases resolve. (TESCondition::IsTrue(action, target) passes no quest, and the
    // engine then hands the raw alias index to the condition function as a pointer.)
    // False for lists that need a running package's data, which can't be evaluated here.
    bool Evaluate(const RE::TESCondition& a_conditions, RE::TESQuest* a_quest,
        RE::TESObjectREFR* a_subject, RE::TESObjectREFR* a_target);

}
