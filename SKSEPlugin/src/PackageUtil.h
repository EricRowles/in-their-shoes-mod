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

    // A package target's value, copied out so it can be kept and written back. The target union is read and
    // written through the member its type uses.
    struct TargetValue {
        std::int8_t type{ kTargetSpecificReference };
        RE::ObjectRefHandle handle{};
        RE::TESForm* object{ nullptr };

        static TargetValue Of(const RE::PackageTarget* a_targ) {
            TargetValue value{ a_targ->targType };
            if (value.type == kTargetSpecificReference) {
                value.handle = a_targ->target.handle;
            } else {
                value.object = a_targ->target.object;
            }
            return value;
        }
        static TargetValue At(RE::Actor* a_actor) { return { kTargetSpecificReference, a_actor->GetHandle(), nullptr }; }

        void WriteTo(RE::PackageTarget* a_targ) const {
            a_targ->targType = type;
            if (type == kTargetSpecificReference) {
                a_targ->target.handle = handle;
            } else {
                a_targ->target.object = object;
            }
        }

        bool References(RE::Actor* a_actor) const {
            if (!a_actor) return false;
            if (type == kTargetSpecificReference) return handle == a_actor->GetHandle();
            if (type == kTargetObjectID) return object == a_actor || object == a_actor->GetBaseObject();
            return false;
        }
    };

    // A package location's value, like TargetValue
    struct LocationValue {
        RE::PackageLocation::Type type{ RE::PackageLocation::Type::kNearReference };
        RE::ObjectRefHandle handle{};
        RE::TESForm* object{ nullptr };
        std::uint32_t radius{ 0 };

        static LocationValue Of(const RE::PackageLocation* a_loc) {
            LocationValue value{ a_loc->locType.get() };
            if (value.type == RE::PackageLocation::Type::kNearReference) {
                value.handle = a_loc->data.refHandle;
            } else {
                value.object = a_loc->data.object;
            }
            value.radius = a_loc->rad;
            return value;
        }
        // Near a_actor, keeping a_radius
        static LocationValue Near(RE::Actor* a_actor, std::uint32_t a_radius) {
            return { RE::PackageLocation::Type::kNearReference, a_actor->GetHandle(), nullptr, a_radius };
        }

        void WriteTo(RE::PackageLocation* a_loc) const {
            a_loc->locType = type;
            if (type == RE::PackageLocation::Type::kNearReference) {
                a_loc->data.refHandle = handle;
            } else {
                a_loc->data.object = object;
            }
            a_loc->rad = radius;
        }

        bool References(RE::Actor* a_actor) const {
            if (!a_actor) return false;
            if (type == RE::PackageLocation::Type::kNearReference) return handle == a_actor->GetHandle();
            if (type == RE::PackageLocation::Type::kObjectID) return object == a_actor || object == a_actor->GetBaseObject();
            return false;
        }
    };

    inline bool TargetReferencesActor(const RE::PackageTarget* a_targ, RE::Actor* a_actor) {
        return a_targ && TargetValue::Of(a_targ).References(a_actor);
    }

    inline bool LocationReferencesActor(const RE::PackageLocation* a_loc, RE::Actor* a_actor) {
        return a_loc && LocationValue::Of(a_loc).References(a_actor);
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

}
