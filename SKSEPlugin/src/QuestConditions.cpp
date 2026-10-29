#include "PCH.h"
#include "QuestConditions.h"

#include "RE/T/TESCondition.h"

namespace InTheirShoes::QuestConditions {

    // Skyrim groups OR'd items first: A AND B OR C AND D == A AND (B OR C) AND D.
    bool Evaluate(const RE::TESCondition& a_conditions, RE::TESQuest* a_quest,
        RE::TESObjectREFR* a_subject, RE::TESObjectREFR* a_target)
    {
        RE::ConditionCheckParams params(a_subject, a_target);
        params.quest = a_quest;

        bool groupResult = false;
        bool groupOpen = false;
        for (auto* item = a_conditions.head; item; item = item->next) {
            if (item->data.flags.usePackData) {
                return false;
            }
            if (!groupResult) {
                groupResult = item->IsTrue(params);
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

}
