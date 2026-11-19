#include "PCH.h"
#include "PackageRetarget.h"
#include "PackageUtil.h"

#include <algorithm>

namespace InTheirShoes {

    using namespace PackageUtil;

    namespace {
        // Clamped radius for a retargeted ForceGreet's Travel branch, so it brings the player within greeting range
        constexpr std::uint32_t kPuppetLocationMaxRadius = 150;

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

        // The edit for a_key, created from its current (unedited) value if there is none
        template <class Map, class Key>
        auto& EditFor(Map& a_map, Key* a_key) {
            using Value = decltype(Map::mapped_type::original);
            return a_map.try_emplace(a_key, typename Map::mapped_type{ Value::Of(a_key) }).first->second;
        }

        // The value a_key would have without edits
        template <class Map, class Key>
        auto OriginalOf(const Map& a_map, Key* a_key) {
            using Value = decltype(Map::mapped_type::original);
            const auto it = a_map.find(a_key);
            return it != a_map.end() ? it->second.original : Value::Of(a_key);
        }
    }

    bool PackageRetarget::Apply(RE::PackageTarget* a_targ, const Edit<TargetValue>& a_edit) const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto value = a_edit.original;
        bool applies = false;
        if (_donor && value.References(_donor)) {
            value = TargetValue::At(player);
            applies = true;
        }
        if (a_edit.toPuppet && _puppet && value.References(player)) {
            value = TargetValue::At(_puppet);
            applies = true;
        }
        value.WriteTo(a_targ);
        return applies;
    }

    bool PackageRetarget::Apply(RE::PackageLocation* a_loc, const Edit<LocationValue>& a_edit) const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto value = a_edit.original;
        bool applies = false;
        if (_donor && value.References(_donor)) {
            value = LocationValue::Near(player, value.radius);
            applies = true;
        }
        if (a_edit.toPuppet && _puppet && value.References(player)) {
            value = LocationValue::Near(_puppet, std::min(value.radius, kPuppetLocationMaxRadius));
            applies = true;
        }
        value.WriteTo(a_loc);
        return applies;
    }

    void PackageRetarget::ApplyAll() {
        std::erase_if(_targets, [&](const auto& a_entry) { return !Apply(a_entry.first, a_entry.second); });
        std::erase_if(_locations, [&](const auto& a_entry) { return !Apply(a_entry.first, a_entry.second); });
    }

    void PackageRetarget::RetargetDonorToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !a_donor || !a_player) return;

        // Packages the previous donor rule changed re-evaluate too, now that they target the donor again
        auto reevaluate = std::move(_donorPackages);
        _donorPackages.clear();
        _donor = a_donor;

        for (auto* pkg : dataHandler->GetFormArray<RE::TESPackage>()) {
            if (!pkg) continue;
            bool changed = false;
            ForEachPackageTarget(pkg, [&](RE::PackageTarget* a_targ) {
                if (!OriginalOf(_targets, a_targ).References(a_donor)) return;
                Apply(a_targ, EditFor(_targets, a_targ));
                changed = true;
            });
            ForEachPackageLocation(pkg, [&](RE::PackageLocation* a_loc) {
                if (!OriginalOf(_locations, a_loc).References(a_donor)) return;
                Apply(a_loc, EditFor(_locations, a_loc));
                changed = true;
            });
            if (changed) {
                _donorPackages.insert(pkg);
                SKSE::log::info("Retargeted package {:08X} '{}' from donor {:08X} to the player", pkg->GetFormID(),
                    pkg->GetFormEditorID(), a_donor->GetFormID());
            }
        }
        // Targets of the previous donor that aren't the new one's go back
        ApplyAll();
        reevaluate.insert(_donorPackages.begin(), _donorPackages.end());
        ReevaluateActorsRunning(reevaluate);
    }

    void PackageRetarget::RestoreDonorToPlayer() {
        _donor = nullptr;
        ApplyAll();
        if (_donorPackages.empty()) return;
        SKSE::log::info("Restored {} packages' targets to the donor", _donorPackages.size());
        auto packages = std::move(_donorPackages);
        _donorPackages.clear();
        ReevaluateActorsRunning(packages);
    }

    bool PackageRetarget::RetargetPlayerToPuppet(RE::TESPackage* a_pkg, RE::Actor* a_puppet) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!a_pkg || !a_puppet || !player) return false;
        _puppet = a_puppet;
        bool changed = false;

        // Checked against the current value: a target the donor rule points at the player counts too
        ForEachPackageTarget(a_pkg, [&](RE::PackageTarget* a_targ) {
            if (!TargetReferencesActor(a_targ, player)) return;
            auto& edit = EditFor(_targets, a_targ);
            edit.toPuppet = true;
            Apply(a_targ, edit);
            changed = true;
        });
        ForEachPackageLocation(a_pkg, [&](RE::PackageLocation* a_loc) {
            if (!LocationReferencesActor(a_loc, player)) return;
            auto& edit = EditFor(_locations, a_loc);
            edit.toPuppet = true;
            Apply(a_loc, edit);
            changed = true;
        });

        if (changed) {
            SKSE::log::info("Retargeted package {:08X} from player to puppet {:08X}", a_pkg->GetFormID(), a_puppet->GetFormID());
        }
        return changed;
    }

    void PackageRetarget::RestorePlayerToPuppet() {
        std::size_t targets = 0;
        std::size_t locations = 0;
        for (auto& [targ, edit] : _targets) {
            targets += std::exchange(edit.toPuppet, false);
        }
        for (auto& [loc, edit] : _locations) {
            locations += std::exchange(edit.toPuppet, false);
        }
        _puppet = nullptr;
        ApplyAll();
        if (targets || locations) {
            SKSE::log::info("Restored {} package targets and {} package locations retargeted to the puppet", targets, locations);
        }
    }

    void PackageRetarget::RestoreAll() {
        for (const auto& [targ, edit] : _targets) {
            edit.original.WriteTo(targ);
        }
        for (const auto& [loc, edit] : _locations) {
            edit.original.WriteTo(loc);
        }
        _targets.clear();
        _locations.clear();
        _donorPackages.clear();
        _donor = nullptr;
        _puppet = nullptr;
    }

}
