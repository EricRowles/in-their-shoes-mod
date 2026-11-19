#pragma once

#include <algorithm>
#include <vector>

namespace InTheirShoes::PackageUtil {

    // Package target types (PackageTarget::targType): which member of PackageTarget::target is set
    inline constexpr std::int8_t kTargetSpecificReference = 0;  // target.handle
    inline constexpr std::int8_t kTargetObjectID = 1;           // target.object

    // Templated packages keep their targets, locations and topics in TESCustomPackageData inputs (with
    // packTarg/packLoc null): BGSPackageDataRef ("SingleRef") and BGSPackageDataTargetSelector point to a
    // PackageTarget, BGSPackageDataLocation to a PackageLocation, and BGSPackageDataTopic holds a topic.

    // Calls a_fn(input) for each of the package's own data inputs that is an Input
    template <class Input, typename Fn>
    void ForEachPackageInput(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        const auto* customData = a_pkg ? skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data) : nullptr;
        if (!customData || !customData->data.data) return;

        for (std::uint16_t i = 0; i < customData->data.dataSize; ++i) {
            if (auto* input = skyrim_cast<Input*>(customData->data.data[i])) {
                a_fn(input);
            }
        }
    }

    // A package's targets and locations: packTarg/packLoc, plus the inputs of templated packages
    template <typename Fn>
    void ForEachPackageTarget(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        if (!a_pkg) return;
        if (a_pkg->packTarg) {
            a_fn(a_pkg->packTarg);
        }
        ForEachPackageInput<RE::BGSPackageDataRef>(a_pkg, [&](RE::BGSPackageDataRef* a_input) {
            if (a_input->pointer) a_fn(a_input->pointer);
        });
        ForEachPackageInput<RE::BGSPackageDataTargetSelector>(a_pkg, [&](RE::BGSPackageDataTargetSelector* a_input) {
            if (a_input->pointer) a_fn(a_input->pointer);
        });
    }

    template <typename Fn>
    void ForEachPackageLocation(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        if (!a_pkg) return;
        if (a_pkg->packLoc) {
            a_fn(a_pkg->packLoc);
        }
        ForEachPackageInput<RE::BGSPackageDataLocation>(a_pkg, [&](RE::BGSPackageDataLocation* a_input) {
            if (a_input->pointer) a_fn(a_input->pointer);
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
