#pragma once

#include "PackageUtil.h"
#include "Singleton.h"
#include <unordered_set>

namespace InTheirShoes {

    // While impersonating, other actors' AI packages that target or travel to the donor (e.g. a companion's
    // follow package with the donor as its specific reference) target the player, so they follow the player
    // instead of standing by for the parked donor. Packages are base records: the change is runtime only and is
    // undone when the session ends or another save loads (package data isn't saved, but stays in memory).
    // Not thread-safe: called through Lifecycle.
    class PackageRetarget : public Singleton<PackageRetarget> {
    public:
        // Points every package's targets and locations that reference a_donor at a_player
        void RetargetDonorToPlayer(RE::Actor* a_donor, RE::Actor* a_player);
        // Puts them back. Actors running them re-evaluate, except while a save loads (a_reevaluate false).
        void Restore(bool a_reevaluate = true);

    private:
        friend Singleton<PackageRetarget>;
        PackageRetarget() = default;
        ~PackageRetarget() = default;

        PackageUtil::PackageEdits _edits;
        std::unordered_set<RE::TESPackage*> _packages;
    };

}
