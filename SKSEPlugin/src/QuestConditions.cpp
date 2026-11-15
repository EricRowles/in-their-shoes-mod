#include "PCH.h"
#include "QuestConditions.h"

namespace InTheirShoes::QuestConditions {

    bool Evaluate(const RE::TESCondition& a_conditions, RE::TESQuest* a_quest,
        RE::TESObjectREFR* a_subject, RE::TESObjectREFR* a_target)
    {
        for (auto* item = a_conditions.head; item; item = item->next) {
            if (item->data.flags.usePackData) {
                return false;
            }
        }
        RE::ConditionCheckParams params(a_subject, a_target);
        params.quest = a_quest;
        return EvaluateItems(a_conditions, [&](RE::TESConditionItem& a_item) { return a_item.IsTrue(params); });
    }

}
