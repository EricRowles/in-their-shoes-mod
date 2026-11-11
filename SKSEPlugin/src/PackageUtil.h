#pragma once

#include "RE/I/IPackageData.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESCustomPackageData.h"
#include "RE/T/TESPackage.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace InTheirShoes::PackageUtil {

    // Package target types (PackageTarget::targType): which member of PackageTarget::target is set
    inline constexpr std::int8_t kTargetSpecificReference = 0;  // target.handle
    inline constexpr std::int8_t kTargetObjectID = 1;           // target.object

    // Templated packages keep their targets, locations and topics in TESCustomPackageData inputs (with
    // packTarg/packLoc null). Each BGSPackageDataPointerTemplate input keeps its data at +0x10: a pointer to
    // a PackageTarget ("SingleRef", "TargetSelector") or PackageLocation ("Location"), or a
    // PackageEventAction::TopicData in place ("Topic").
    template <class T>
    T* GetInputData(RE::IPackageData* a_input) {
        constexpr std::uintptr_t kInputDataOffset = 0x10;
        return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(a_input) + kInputDataOffset);
    }

    // Calls a_fn(input) for each of the package's own data inputs whose type is one of a_typeNames
    template <typename Fn>
    void ForEachPackageInput(const RE::TESPackage* a_pkg, std::initializer_list<const char*> a_typeNames, Fn&& a_fn) {
        const auto* customData = a_pkg ? skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data) : nullptr;
        if (!customData || !customData->data.data) return;

        for (std::uint16_t i = 0; i < customData->data.dataSize; ++i) {
            auto* input = customData->data.data[i];
            if (!input) continue;
            const auto& typeName = input->GetTypeName();
            if (typeName.empty()) continue;
            for (const char* name : a_typeNames) {
                if (_stricmp(typeName.c_str(), name) == 0) {
                    a_fn(input);
                    break;
                }
            }
        }
    }

    // A package's targets and locations: packTarg/packLoc, plus the inputs of templated packages.
    // E.g. vanilla ForceGreet (0003C1C4) targets the player in its SingleRef input, and its "Forcegreet
    // Distance" Location input (radius 300) is near the player, so left alone the player is always within
    // it and skips the Travel branch.
    template <typename Fn>
    void ForEachPackageTarget(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        if (!a_pkg) return;
        if (a_pkg->packTarg) {
            a_fn(a_pkg->packTarg);
        }
        ForEachPackageInput(a_pkg, { "SingleRef", "TargetSelector" }, [&](RE::IPackageData* a_input) {
            if (auto* targ = *GetInputData<RE::PackageTarget*>(a_input)) {
                a_fn(targ);
            }
        });
    }

    template <typename Fn>
    void ForEachPackageLocation(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        if (!a_pkg) return;
        if (a_pkg->packLoc) {
            a_fn(a_pkg->packLoc);
        }
        ForEachPackageInput(a_pkg, { "Location" }, [&](RE::IPackageData* a_input) {
            if (auto* loc = *GetInputData<RE::PackageLocation*>(a_input)) {
                a_fn(loc);
            }
        });
    }

    inline bool TargetReferencesActor(const RE::PackageTarget* a_targ, RE::Actor* a_actor) {
        if (!a_targ || !a_actor) return false;
        if (a_targ->targType == kTargetSpecificReference) {
            return a_targ->target.handle == a_actor->GetHandle();
        }
        if (a_targ->targType == kTargetObjectID) {
            return a_targ->target.object == a_actor || a_targ->target.object == a_actor->GetBaseObject();
        }
        return false;
    }

    inline bool LocationReferencesActor(const RE::PackageLocation* a_loc, RE::Actor* a_actor) {
        if (!a_loc || !a_actor) return false;
        auto type = a_loc->locType.get();
        if (type == RE::PackageLocation::Type::kNearReference) {
            return a_loc->data.refHandle == a_actor->GetHandle();
        }
        if (type == RE::PackageLocation::Type::kObjectID) {
            return a_loc->data.object == a_actor || a_loc->data.object == a_actor->GetBaseObject();
        }
        return false;
    }

    inline bool PackageTargetsActor(const RE::TESPackage* a_pkg, RE::Actor* a_actor) {
        bool found = false;
        ForEachPackageTarget(a_pkg, [&](RE::PackageTarget* a_targ) {
            if (TargetReferencesActor(a_targ, a_actor)) {
                found = true;
            }
        });
        if (found) return true;
        ForEachPackageLocation(a_pkg, [&](RE::PackageLocation* a_loc) {
            if (LocationReferencesActor(a_loc, a_actor)) {
                found = true;
            }
        });
        return found;
    }

    // Package targets and locations pointed at another actor at runtime (packages are base records), saved so
    // Restore() can put them back. Each is saved the first time it changes.
    class PackageEdits {
    public:
        void Retarget(RE::PackageTarget* a_targ, RE::Actor* a_actor) {
            if (std::ranges::none_of(_targets, [&](const SavedTarget& a_saved) { return a_saved.target == a_targ; })) {
                _targets.push_back({ a_targ, a_targ->targType, a_targ->target.handle, a_targ->target.object });
            }
            a_targ->targType = kTargetSpecificReference;
            a_targ->target.handle = a_actor->GetHandle();
        }

        void Relocate(RE::PackageLocation* a_loc, RE::Actor* a_actor) {
            if (std::ranges::none_of(_locations, [&](const SavedLocation& a_saved) { return a_saved.location == a_loc; })) {
                _locations.push_back({ a_loc, a_loc->locType.get(), a_loc->data.refHandle, a_loc->data.object, a_loc->rad });
            }
            a_loc->locType = RE::PackageLocation::Type::kNearReference;
            a_loc->data.refHandle = a_actor->GetHandle();
        }

        std::size_t TargetCount() const { return _targets.size(); }
        std::size_t LocationCount() const { return _locations.size(); }

        // The target and location unions are restored through the member their saved type uses
        void Restore() {
            for (const auto& saved : _targets) {
                saved.target->targType = saved.type;
                if (saved.type == kTargetSpecificReference) {
                    saved.target->target.handle = saved.handle;
                } else {
                    saved.target->target.object = saved.object;
                }
            }
            for (const auto& saved : _locations) {
                saved.location->locType = saved.type;
                if (saved.type == RE::PackageLocation::Type::kNearReference) {
                    saved.location->data.refHandle = saved.handle;
                } else {
                    saved.location->data.object = saved.object;
                }
                saved.location->rad = saved.radius;
            }
            _targets.clear();
            _locations.clear();
        }

    private:
        struct SavedTarget {
            RE::PackageTarget* target;
            std::int8_t type;
            RE::ObjectRefHandle handle;
            RE::TESForm* object;
        };
        struct SavedLocation {
            RE::PackageLocation* location;
            RE::PackageLocation::Type type;
            RE::ObjectRefHandle handle;
            RE::TESForm* object;
            std::uint32_t radius;
        };

        std::vector<SavedTarget> _targets;
        std::vector<SavedLocation> _locations;
    };

}
