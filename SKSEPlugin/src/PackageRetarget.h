#pragma once

#include "PackageUtil.h"
#include <unordered_map>
#include <unordered_set>

namespace InTheirShoes {

    // The one owner of runtime edits to AI package targets and locations. Packages are base records shared by
    // every actor that runs them, so edits are runtime only (package data isn't saved, but stays in memory until
    // they are put back). Two rules decide where an edited target points:
    //  - Donor to player: other actors' packages that target or travel to the donor target the player.
    //  - Player to puppet: packages the idle player runs (IdleAIManager) that target the player would make it
    //    target itself, so they target the puppet.
    // Each edited target's original value is kept and its current value computed from the rules, so dropping a
    // rule, or all of them, works in any order. Not thread-safe: called from the main thread.
    class PackageRetarget : public REX::Singleton<PackageRetarget> {
    public:
        // Points every package's targets and locations that reference a_donor at a_player
        void RetargetDonorToPlayer(RE::Actor* a_donor, RE::Actor* a_player);
        // Drops the donor rule. Actors running the packages re-evaluate.
        void RestoreDonorToPlayer();

        // Points a_pkg's targets and locations that reference the player at a_puppet, until RestorePlayerToPuppet.
        // Returns whether any changed.
        bool RetargetPlayerToPuppet(RE::TESPackage* a_pkg, RE::Actor* a_puppet);
        void RestorePlayerToPuppet();

        // Writes every original back (before a save loads)
        void RestoreAll();

    private:
        friend REX::Singleton<PackageRetarget>;
        PackageRetarget() = default;
        ~PackageRetarget() = default;

        template <class Value>
        struct Edit {
            Value original;
            bool toPuppet{ false };
        };

        // Writes a target's value from its original and the rules; false if no rule applies to it
        bool Apply(RE::PackageTarget* a_targ, const Edit<PackageUtil::TargetValue>& a_edit) const;
        bool Apply(RE::PackageLocation* a_loc, const Edit<PackageUtil::LocationValue>& a_edit) const;
        // Rewrites every edited target after a rule changed, forgetting those no rule applies to any more
        void ApplyAll();

        RE::Actor* _donor{ nullptr };
        RE::Actor* _puppet{ nullptr };
        std::unordered_map<RE::PackageTarget*, Edit<PackageUtil::TargetValue>> _targets;
        std::unordered_map<RE::PackageLocation*, Edit<PackageUtil::LocationValue>> _locations;
        // Packages the donor rule changed, whose running actors re-evaluate when it changes
        std::unordered_set<RE::TESPackage*> _donorPackages;
    };

}
