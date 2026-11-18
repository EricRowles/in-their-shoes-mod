#pragma once

#include "RE/T/TESCondition.h"

namespace InTheirShoes::QuestConditions {

    // Whether a condition list passes, with a_isTrue(item) answering each item. Skyrim groups OR'd items first:
    // A AND B OR C AND D == A AND (B OR C) AND D. Items after one that settles its group aren't asked.
    template <class IsTrue>
    bool EvaluateItems(const RE::TESCondition& a_conditions, IsTrue&& a_isTrue) {
        bool groupResult = false;
        bool groupOpen = false;
        for (auto* item = a_conditions.head; item; item = item->next) {
            if (!groupResult) {
                groupResult = a_isTrue(*item);
            }
            groupOpen = item->data.flags.isOR;
            if (!groupOpen) {
                if (!groupResult) return false;
                groupResult = false;
            }
        }
        // A trailing OR item closes its group at the end of the list
        return !groupOpen || groupResult;
    }

    // Evaluates a condition list in the context of its quest, so conditions whose parameters or run-on
    // target are quest aliases resolve. (TESCondition::IsTrue(action, target) passes no quest, and the
    // engine then hands the raw alias index to the condition function as a pointer.)
    // False for lists that need a running package's data, which can't be evaluated here.
    bool Evaluate(const RE::TESCondition& a_conditions, RE::TESQuest* a_quest,
        RE::TESObjectREFR* a_subject, RE::TESObjectREFR* a_target);

}
