#include "PCH.h"
#include "PackageRetarget.h"
#include "PackageUtil.h"

#include "RE/P/ProcessLists.h"

namespace InTheirShoes {

    using namespace PackageUtil;

    namespace {
        // Actors already running a changed package keep the target they resolved when it started; re-evaluating
        // starts it again with the new one
        void ReevaluateActorsRunning(const std::unordered_set<RE::TESPackage*>& a_packages) {
            auto* processLists = RE::ProcessLists::GetSingleton();
            if (!processLists || a_packages.empty()) return;
            processLists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (a_actor && a_packages.contains(a_actor->GetCurrentPackage())) {
                    a_actor->EvaluatePackage(true, true);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
    }

    void PackageRetarget::RetargetDonorToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !a_donor || !a_player) return;
        Restore();

        for (auto* pkg : dataHandler->GetFormArray<RE::TESPackage>()) {
            if (!pkg) continue;
            bool changed = false;
            ForEachPackageTarget(pkg, [&](RE::PackageTarget* a_targ) {
                if (!TargetReferencesActor(a_targ, a_donor)) return;
                _edits.Retarget(a_targ, a_player);
                changed = true;
            });
            ForEachPackageLocation(pkg, [&](RE::PackageLocation* a_loc) {
                if (!LocationReferencesActor(a_loc, a_donor)) return;
                _edits.Relocate(a_loc, a_player);
                changed = true;
            });
            if (changed) {
                _packages.insert(pkg);
                SKSE::log::info("Retargeted package {:08X} '{}' from donor {:08X} to the player", pkg->GetFormID(),
                    pkg->GetFormEditorID(), a_donor->GetFormID());
            }
        }
        ReevaluateActorsRunning(_packages);
    }

    void PackageRetarget::Restore(bool a_reevaluate) {
        _edits.Restore();
        if (_packages.empty()) return;
        SKSE::log::info("Restored {} packages' targets to the donor", _packages.size());
        auto packages = std::move(_packages);
        _packages.clear();
        if (a_reevaluate) {
            ReevaluateActorsRunning(packages);
        }
    }

}
