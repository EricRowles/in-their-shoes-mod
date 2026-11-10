#include "PCH.h"
#include "NativeRedirect.h"
#include "GameUtil.h"
#include "Session.h"

#include <RE/A/ActiveEffect.h>
#include <RE/B/BSScriptObjectBindPolicy.h>

#include <array>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

namespace InTheirShoes::NativeRedirect {

    // Mods reach an actor through properties filled with its reference, not only aliases (e.g. a follower mod's
    // "k101DaegonRef.Say(...)" after the other follower's line, #1), and topic fragments act on the speaker, which
    // in self-dialogue is the puppet. Both mean the donor, whom the player plays.
    // Every native's NativeFunctionBase::MarshallAndDispatch (vfunc 0x16) gets self as a Variable and reads its
    // arguments from the stack frame, so one thunk swaps them whatever the signature. That vfunc is implemented
    // once per native signature (and plugin), so each vtable keeps its own original.
    // Script variables keep holding the donor, so nothing is saved or restored.

    namespace {
        using Variable = RE::BSScript::Variable;
        using StackFrame = RE::BSScript::StackFrame;
        using VM = RE::BSScript::Internal::VirtualMachine;
        using NativeFunction = RE::BSScript::NF_util::NativeFunctionBase;
        using MarshallAndDispatch_t = bool(const NativeFunction*, Variable&, VM&, RE::VMStackID, Variable&, const StackFrame&);
        constexpr std::size_t kMarshallAndDispatchIndex = 0x16;

        // Filled before any vtable is patched, then only read (from VM threads)
        std::unordered_map<std::uintptr_t, MarshallAndDispatch_t*> s_origMarshall;

        // How a native called on the donor (or the puppet) is handled
        enum class Kind {
            kPlayer,       // runs on the player
            kLocation,     // runs on the player, even for scripts on the donor's own body
            kDonor,        // state kept on the donor: the condition hooks and revert read it from there
            kKept,         // left alone: which actor it is, or moving or enabling it (it stays parked)
            kLife,         // killing: runs on the player when the puppet speaks for the donor; the donor is left alone
        };

        Kind GetKind(const RE::BSFixedString& a_name) {
            static const std::array<std::pair<RE::BSFixedString, Kind>, 32> kinds{ {
                // Where it is: the parked donor is where the player is (as the spatial condition hooks answer)
                { "GetParentCell", Kind::kLocation }, { "GetWorldSpace", Kind::kLocation },
                { "GetCurrentLocation", Kind::kLocation }, { "GetPositionX", Kind::kLocation },
                { "GetPositionY", Kind::kLocation }, { "GetPositionZ", Kind::kLocation },
                { "GetDistance", Kind::kLocation }, { "GetHeadingAngle", Kind::kLocation },
                // Teammate status and relationships belong to the donor (relationships are between base records,
                // and the relationship condition hooks ask the donor's). Faction natives run on the player, who
                // holds the donor's factions for the session and hands them back on revert (FactionManager): a
                // recruitment that takes the speaker out of DismissedFollowerFaction must hide the recruit topic.
                { "SetPlayerTeammate", Kind::kDonor }, { "IsPlayerTeammate", Kind::kDonor },
                { "GetRelationshipRank", Kind::kDonor }, { "SetRelationshipRank", Kind::kDonor },
                // Which actor it is
                { "GetActorBase", Kind::kKept }, { "GetLeveledActorBase", Kind::kKept }, { "GetBaseObject", Kind::kKept },
                // Where it is and whether it exists (e.g. a follower mod's "flee" moved its follower to a holding
                // cell, which would have taken the player there)
                { "MoveTo", Kind::kKept }, { "MoveToNode", Kind::kKept }, { "MoveToInteractionLocation", Kind::kKept },
                { "MoveToMyEditorLocation", Kind::kKept }, { "MoveToPackageLocation", Kind::kKept },
                { "SetPosition", Kind::kKept }, { "SetAngle", Kind::kKept }, { "TranslateTo", Kind::kKept },
                { "SplineTranslateTo", Kind::kKept }, { "Enable", Kind::kKept }, { "Disable", Kind::kKept },
                { "EnableNoWait", Kind::kKept }, { "DisableNoWait", Kind::kKept }, { "Delete", Kind::kKept },
                // Whether it lives. A fragment that kills its speaker kills the donor the player is playing (e.g.
                // There Is No Umbra's "May I wield you" kills Daumbra's host), so it kills the player, not the
                // puppet, which is the Dragonborn's body (#30). Scripts that kill the parked donor still leave the
                // player alone.
                { "Kill", Kind::kLife }, { "KillEssential", Kind::kLife }, { "KillSilent", Kind::kLife },
            } };
            for (const auto& [name, kind] : kinds) {
                if (name == a_name) return kind;
            }
            return Kind::kPlayer;
        }

        // Natives of another class with the same signature can share a patched vtable
        bool IsRedirectedClass(const RE::BSFixedString& a_className) {
            static const RE::BSFixedString objectReference{ "ObjectReference" };
            static const RE::BSFixedString actor{ "Actor" };
            return a_className == objectReference || a_className == actor;
        }

        // The actor a_var holds if it stands for the donor (Session::StandsForDonor)
        RE::Actor* GetStandIn(const Variable& a_var, const Session& a_session) {
            for (auto* actor : { a_session.GetDonor(), a_session.GetPuppet() }) {
                if (HoldsForm(a_var, actor) && a_session.StandsForDonor(actor)) return actor;
            }
            return nullptr;
        }

        // The calling script's frame: the native's frame, or the one before if the native has its own. None for
        // calls from C++.
        // Actor.psc and ObjectReference.psc define some functions in Papyrus that call natives on themselves
        // (KillEssential calls Kill, ModAV calls ModActorValue). Their frames run on the actor, so they looked
        // like a script on its body and were never redirected; the script that called the wrapper is the caller
        // (#30: a fragment's akSpeaker.KillEssential() killed the puppet, not the player).
        bool IsBaseClassWrapper(const StackFrame& a_frame) {
            const char* name = a_frame.owningObjectType->GetName();
            return _stricmp(name, "Actor") == 0 || _stricmp(name, "ObjectReference") == 0 || _stricmp(name, "Form") == 0;
        }

        const StackFrame* GetCallingFrame(const NativeFunction* a_native, const StackFrame& a_frame) {
            const auto* caller = a_frame.owningFunction.get() == a_native ? a_frame.previousFrame : &a_frame;
            while (caller && caller->owningObjectType && IsBaseClassWrapper(*caller) && caller->previousFrame &&
                   caller->previousFrame->owningObjectType) {
                caller = caller->previousFrame;
            }
            return caller && caller->owningObjectType ? caller : nullptr;
        }

        // Our ITS_ scripts park and dress the donor and puppet themselves
        bool IsOurScript(const StackFrame& a_caller) {
            return _strnicmp(a_caller.owningObjectType->GetName(), "ITS_", 4) == 0;
        }

        // Whether the calling script is on a_actor's own body: attached to it, or a magic effect on it (e.g. a
        // speed boost effect on the donor set the player's SpeedMult; heels and helmet mods' effects changed the
        // player's spells)
        bool IsOnBody(const StackFrame& a_caller, const RE::Actor* a_actor) {
            if (HoldsForm(a_caller.self, a_actor)) return true;
            auto object = a_caller.self.IsObject() ? a_caller.self.GetObject() : nullptr;
            auto* effect = object ? static_cast<RE::ActiveEffect*>(object->Resolve(RE::ActiveEffect::VMTYPEID)) : nullptr;
            return effect && effect->GetTargetActor() == a_actor;
        }

        // a_actor as a script object of a_className (as Papyrus does when it first passes a form as a class)
        bool GetObjectFor(VM& a_vm, RE::Actor* a_actor, const char* a_className, Variable& a_out) {
            auto* handlePolicy = a_vm.GetObjectHandlePolicy();
            if (!a_actor || !handlePolicy) return false;
            const auto handle = handlePolicy->GetHandleForObject(a_actor->GetFormType(), a_actor);
            RE::BSTSmartPointer<RE::BSScript::Object> object;
            if (!a_vm.FindBoundObject(handle, a_className, object) || !object) {
                auto* bindPolicy = a_vm.GetObjectBindPolicy();
                if (!bindPolicy || !a_vm.CreateObject(a_className, object) || !object) return false;
                bindPolicy->BindObject(object, handle);
            }
            a_out.SetObject(object);
            return true;
        }

        // The class of the object a_var holds, if it's one the player has without a script attached. A variable
        // typed as a mod's own actor script holds that script's object, which the player doesn't have.
        const char* GetNativeClass(const Variable& a_var) {
            auto object = a_var.GetObject();
            const auto* type = object ? object->GetTypeInfo() : nullptr;
            if (!type) return nullptr;
            const char* name = type->GetName();
            return _stricmp(name, "Actor") == 0 || _stricmp(name, "ObjectReference") == 0 ? name : nullptr;
        }

        void LogOnce(const NativeFunction* a_native, const StackFrame& a_caller, std::string_view a_what) {
            static std::mutex lock;
            static std::set<std::string> logged;
            auto line = fmt::format("{}.{} from {}: {}", a_native->GetObjectTypeName().c_str(), a_native->GetName().c_str(),
                a_caller.owningObjectType->GetName(), a_what);
            std::scoped_lock guard(lock);
            if (logged.insert(line).second) {
                SKSE::log::info("Redirected {}", line);
            }
        }

        // Swaps arguments that stand for the donor for the player while the native runs
        class ArgumentSwap {
        public:
            ArgumentSwap(const NativeFunction* a_native, const StackFrame& a_frame, const StackFrame& a_caller, Kind a_kind,
                VM& a_vm, const Session& a_session) {
                const auto page = a_frame.GetPageForFrame();
                for (std::uint32_t i = 0; i < a_native->GetParamCount(); ++i) {
                    auto& arg = a_frame.GetStackFrameVariable(i, page);
                    auto* standIn = GetStandIn(arg, a_session);
                    const char* className = standIn ? GetNativeClass(arg) : nullptr;
                    if (!className || (a_kind != Kind::kLocation && IsOnBody(a_caller, standIn))) continue;
                    Variable player;
                    if (!GetObjectFor(a_vm, Session::GetPlayer(), className, player)) continue;
                    _swapped.emplace_back(&arg, arg);
                    arg = player;
                    LogOnce(a_native, a_caller, "argument to the player");
                }
            }
            ~ArgumentSwap() {
                for (auto& [arg, original] : _swapped) {
                    *arg = original;
                }
            }
            ArgumentSwap(const ArgumentSwap&) = delete;
            ArgumentSwap& operator=(const ArgumentSwap&) = delete;

        private:
            std::vector<std::pair<Variable*, Variable>> _swapped;
        };

        bool MarshallAndDispatch(const NativeFunction* a_this, Variable& a_self, VM& a_vm, RE::VMStackID a_stackID,
            Variable& a_result, const StackFrame& a_frame) {
            auto* orig = s_origMarshall.at(*reinterpret_cast<const std::uintptr_t*>(a_this));
            auto call = [&](Variable& a_callSelf) { return orig(a_this, a_callSelf, a_vm, a_stackID, a_result, a_frame); };

            const auto& session = Session::GetSingleton();
            if (!session.IsNativeRedirectEnabled() || !session.IsActive() || !IsRedirectedClass(a_this->GetObjectTypeName())) {
                return call(a_self);
            }
            const auto* caller = GetCallingFrame(a_this, a_frame);
            if (!caller || IsOurScript(*caller)) return call(a_self);

            const auto kind = GetKind(a_this->GetName());
            ArgumentSwap arguments(a_this, a_frame, *caller, kind, a_vm, session);

            auto* standIn = GetStandIn(a_self, session);
            if (!standIn || kind == Kind::kKept || (kind != Kind::kLocation && IsOnBody(*caller, standIn))) {
                return call(a_self);
            }
            const char* className = a_this->GetObjectTypeName().c_str();
            auto* donor = session.GetDonor();
            Variable target;
            switch (kind) {
                case Kind::kDonor:
                    if (standIn == donor || !GetObjectFor(a_vm, donor, className, target)) return call(a_self);
                    LogOnce(a_this, *caller, "puppet to the donor");
                    return call(target);
                case Kind::kLife:
                    if (standIn == donor) return call(a_self);
                    [[fallthrough]];
                default:
                    if (!GetObjectFor(a_vm, Session::GetPlayer(), className, target)) return call(a_self);
                    LogOnce(a_this, *caller, standIn == donor ? "donor to the player" : "puppet to the player");
                    return call(target);
            }
        }
    }

    void Install() {
        static bool s_installed = false;
        if (s_installed) return;
        auto* vm = VM::GetSingleton();
        if (!vm) {
            SKSE::log::warn("No Papyrus VM; natives called on the donor won't be redirected.");
            return;
        }

        // Natives other plugins register later get new function objects (and their own vtables), which this misses
        std::size_t natives = 0;
        for (const auto* className : { "ObjectReference", "Actor" }) {
            RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> type;
            if (!vm->GetScriptObjectType(className, type) || !type) {
                SKSE::log::warn("Couldn't load script type {}; its natives won't be redirected for the donor.", className);
                continue;
            }
            auto* funcs = type->GetMemberFuncIter();
            for (std::uint32_t i = 0; i < type->GetNumMemberFuncs(); ++i) {
                auto* func = funcs[i].func.get();
                if (!func || !func->GetIsNative() || func->GetIsStatic()) continue;
                const auto vtable = *reinterpret_cast<const std::uintptr_t*>(func);
                s_origMarshall.try_emplace(vtable, reinterpret_cast<MarshallAndDispatch_t* const*>(vtable)[kMarshallAndDispatchIndex]);
                ++natives;
            }
        }
        for (const auto& [vtable, orig] : s_origMarshall) {
            REL::safe_write(vtable + kMarshallAndDispatchIndex * sizeof(std::uintptr_t), reinterpret_cast<std::uintptr_t>(&MarshallAndDispatch));
        }
        s_installed = true;
        SKSE::log::info("Redirecting {} ObjectReference/Actor natives for the donor ({} vtables patched).", natives,
            s_origMarshall.size());
    }

}
