#include "PCH.h"
#include "IdleAIManager.h"
#include "AliasSwapManager.h"
#include "Session.h"
#include "DialogueManager.h"
#include "Forms.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "RE/P/PlayerControls.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESPackage.h"
#include "RE/T/TESCustomPackageData.h"
#include "RE/I/IPackageData.h"
#include "RE/B/BGSProcedureTreeBranch.h"
#include "RE/B/BGSProcedureTreeProcedure.h"
#include "RE/T/TESQuest.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/B/BGSRefAlias.h"
#include "RE/F/FunctionArguments.h"

namespace InTheirShoes {

    // Game.SetPlayerAIDriven runs through the script VM, so the package is evaluated a moment later
    constexpr float kEvaluateAfterEngageSeconds = 0.25f;

    // Updates aiControlledPackage synchronously so immediate EvaluatePackage calls see the new state,
    // and dispatches Game.SetPlayerAIDriven through the VM to switch the movement controller cleanly.
    // (Calling PlayerCharacter::SetAIDriven by its CommonLib address left the player with no package.)
    static void SetPlayerAIDriven(bool a_enable) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            player->GetPlayerRuntimeData().playerFlags.aiControlledPackage = a_enable;
        }
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return;
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        vm->DispatchStaticCall("Game", "SetPlayerAIDriven", RE::MakeFunctionArguments(std::move(a_enable)), callback);
    }

    // A package's targets and locations: packTarg/packLoc, plus the data inputs of templated packages, which
    // keep them in TESCustomPackageData with packTarg null. Each BGSPackageDataPointerTemplate input stores
    // its PackageTarget* ("SingleRef", "TargetSelector") or PackageLocation* ("Location") at +0x10.
    // E.g. vanilla ForceGreet (0003C1C4) targets the player in its SingleRef input, and its "Forcegreet
    // Distance" Location input (radius 300) is near the player, so left alone the player is always within
    // it and skips the Travel branch.
    template <typename Fn>
    static void ForEachPackageTarget(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        if (!a_pkg) return;
        if (a_pkg->packTarg) {
            a_fn(a_pkg->packTarg);
        }
        const auto* customData = skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data);
        if (!customData || !customData->data.data) return;

        for (std::uint16_t i = 0; i < customData->data.dataSize; ++i) {
            auto* item = customData->data.data[i];
            if (!item) continue;
            const auto& typeName = item->GetTypeName();
            if (!typeName.empty() &&
                (_stricmp(typeName.c_str(), "SingleRef") == 0 || _stricmp(typeName.c_str(), "TargetSelector") == 0)) {
                auto* targ = *reinterpret_cast<RE::PackageTarget* const*>(reinterpret_cast<std::uintptr_t>(item) + 0x10);
                if (targ) {
                    a_fn(targ);
                }
            }
        }
    }

    template <typename Fn>
    static void ForEachPackageLocation(const RE::TESPackage* a_pkg, Fn&& a_fn) {
        if (!a_pkg) return;
        if (a_pkg->packLoc) {
            a_fn(a_pkg->packLoc);
        }
        const auto* customData = skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data);
        if (!customData || !customData->data.data) return;

        for (std::uint16_t i = 0; i < customData->data.dataSize; ++i) {
            auto* item = customData->data.data[i];
            if (!item) continue;
            const auto& typeName = item->GetTypeName();
            if (!typeName.empty() && _stricmp(typeName.c_str(), "Location") == 0) {
                auto* loc = *reinterpret_cast<RE::PackageLocation* const*>(reinterpret_cast<std::uintptr_t>(item) + 0x10);
                if (loc) {
                    a_fn(loc);
                }
            }
        }
    }

    static bool TargetReferencesActor(const RE::PackageTarget* a_targ, RE::Actor* a_actor) {
        if (!a_targ || !a_actor) return false;
        if (a_targ->targType == 0) {
            return a_targ->target.handle == a_actor->GetHandle();
        }
        if (a_targ->targType == 1) {
            return a_targ->target.object == a_actor || a_targ->target.object == a_actor->GetBaseObject();
        }
        return false;
    }

    static bool LocationReferencesActor(const RE::PackageLocation* a_loc, RE::Actor* a_actor) {
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

    static bool PackageTargetsActor(const RE::TESPackage* a_pkg, RE::Actor* a_actor) {
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

    static bool HasProcedureTreeProcedure(const RE::IProcedureTreeItem* a_item, std::uintptr_t a_procVtbl) {
        if (!a_item) return false;
        static const std::uintptr_t kBranchVtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_BGSProcedureTreeBranch[0]).address();
        static const std::uintptr_t kProcItemVtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_BGSProcedureTreeProcedure[0]).address();

        const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_item);
        if (vtbl == kProcItemVtbl) {
            const auto* procItem = static_cast<const RE::BGSProcedureTreeProcedure*>(a_item);
            if (procItem->procedure && *reinterpret_cast<const std::uintptr_t*>(procItem->procedure) == a_procVtbl) {
                return true;
            }
        } else if (vtbl == kBranchVtbl) {
            const auto* branch = static_cast<const RE::BGSProcedureTreeBranch*>(a_item);
            for (const auto* child : branch->conditions) {
                if (HasProcedureTreeProcedure(child, a_procVtbl)) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool HasProcedure(const RE::TESPackage* a_pkg, RE::PACKAGE_PROCEDURE_TYPE a_type, std::uintptr_t a_procVtbl) {
        if (!a_pkg) return false;
        if (a_pkg->procedureType == a_type) {
            return true;
        }
        const auto* customData = skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data);
        if (!customData) return false;
        if (HasProcedureTreeProcedure(customData->procedureTree, a_procVtbl)) {
            return true;
        }
        return customData->templateParent && HasProcedure(customData->templateParent, a_type, a_procVtbl);
    }

    static bool HasForceGreetProcedure(const RE::TESPackage* a_pkg) {
        static const std::uintptr_t kForceGreetVtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_BGSProcedureForceGreet[0]).address();
        return HasProcedure(a_pkg, RE::PACKAGE_PROCEDURE_TYPE::kGreet, kForceGreetVtbl);
    }

    static bool HasDialogueProcedure(const RE::TESPackage* a_pkg) {
        static const std::uintptr_t kDialogueVtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_BGSProcedureDialogue[0]).address();
        return HasProcedure(a_pkg, RE::PACKAGE_PROCEDURE_TYPE::kDialogue, kDialogueVtbl);
    }

    static RE::TESTopic* GetForceGreetTopic(const RE::TESPackage* a_pkg) {
        if (!a_pkg) return nullptr;
        const auto* customData = skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data);
        if (!customData) return nullptr;

        if (customData->data.data) {
            for (std::uint16_t i = 0; i < customData->data.dataSize; ++i) {
                auto* item = customData->data.data[i];
                if (!item) continue;
                const auto& typeName = item->GetTypeName();
                if (!typeName.empty() && _stricmp(typeName.c_str(), "Topic") == 0) {
                    const auto* topicData = reinterpret_cast<const RE::PackageEventAction::TopicData*>(
                        reinterpret_cast<std::uintptr_t>(item) + 0x10);
                    if (topicData->type == RE::PackageEventAction::TopicData::Type::kTopicRef && topicData->topic) {
                        return topicData->topic;
                    }
                }
            }
        }
        return customData->templateParent ? GetForceGreetTopic(customData->templateParent) : nullptr;
    }

    void IdleAIManager::Initialize() {
        if (_initialized) return;
        if (auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton()) {
            inputDispatcher->AddEventSink<RE::InputEvent*>(this);
            SKSE::log::info("IdleAIManager registered input event sink successfully.");
        } else {
            SKSE::log::warn("IdleAIManager failed to get BSInputDeviceManager singleton.");
        }
        InstallHooks();
        _initialized = true;
    }

    void IdleAIManager::InstallHooks() {
        REL::Relocation<std::uintptr_t> forceGreetVtbl{ RE::VTABLE_BGSProcedureForceGreet[0] };
        _origForceGreetInitExecState = forceGreetVtbl.write_vfunc(0x09, &Hook_ForceGreet_InitExecState);
        _origForceGreetExecute = forceGreetVtbl.write_vfunc(0x0C, &Hook_ForceGreet_Execute);
        SKSE::log::info("Hooked BGSProcedureForceGreet (0x09, 0x0C) vfuncs");
    }

    std::uint64_t IdleAIManager::Hook_ForceGreet_InitExecState(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState) {
        auto ret = _origForceGreetInitExecState(a_this, a_execState);
        auto& session = Session::GetSingleton();
        if (session.IsActive() && a_execState && a_execState->context &&
            a_execState->context->actor == RE::PlayerCharacter::GetSingleton() && a_execState->nodeState) {
            if (auto* puppet = session.GetPuppet()) {
                // BGSProcedureForceGreet hardcodes BGSProcedureDialogueExecState::targetHandle (+0x20)
                // to the player's handle, which makes the player spin in a circle targeting itself.
                *reinterpret_cast<RE::ObjectRefHandle*>(reinterpret_cast<std::uintptr_t>(a_execState->nodeState) + 0x20) = puppet->GetHandle();
                IdleAIManager::GetSingleton()._forceGreetProcedureActive = true;
            }
        }
        return ret;
    }

    std::uint64_t IdleAIManager::Hook_ForceGreet_Execute(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState) {
        auto& session = Session::GetSingleton();
        bool isPlayerForceGreet = session.IsActive() && a_execState && a_execState->context &&
                                  a_execState->context->actor == RE::PlayerCharacter::GetSingleton() && a_execState->nodeState;
        if (isPlayerForceGreet) {
            if (auto* puppet = session.GetPuppet()) {
                *reinterpret_cast<RE::ObjectRefHandle*>(reinterpret_cast<std::uintptr_t>(a_execState->nodeState) + 0x20) = puppet->GetHandle();
                IdleAIManager::GetSingleton()._forceGreetProcedureActive = true;
            }
        }
        auto ret = _origForceGreetExecute(a_this, a_execState);
        if (isPlayerForceGreet) {
            // BGSProcedureForceGreet::Execute sets MenuTopicManager::forceGoodbye (+0xB2 = true) every
            // frame while pathing toward its target, and only clears it if the engine's internal NPC->player
            // ForceGreet start completes. Leaving forceGoodbye set causes MenuTopicManager::StartConversation
            // to set shutMenu = true and immediately close self-dialogue on the next frame.
            if (auto* topicMgr = RE::MenuTopicManager::GetSingleton()) {
                topicMgr->forceGoodbye = false;
            }
        }
        return ret;
    }

    void IdleAIManager::Update() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->IsInCombat()) {
            if (_aiActive) {
                StopIdleAI();
            }
        } else {
            CheckIdleAI();
        }

        if (!_aiActive || !Session::GetSingleton().IsActive()) return;

        if (!_evaluatedAfterEngage && SecondsSinceEngage() >= kEvaluateAfterEngageSeconds) {
            _evaluatedAfterEngage = true;
            player->EvaluatePackage(true, true);
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - _lastPackageSyncTime).count() >= 1.5f) {
            _lastPackageSyncTime = now;
            SyncDonorPackagesToPlayer();
        }
        LogRunningPackage();
        CheckForceGreetReachedPuppet();
    }

    float IdleAIManager::SecondsSinceEngage() const {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - _aiStartTime).count();
    }

    void IdleAIManager::LogRunningPackage() {
        // Report the first package seen after engaging (or that there is none), then every change
        if (SecondsSinceEngage() < 1.0f) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pkg = player ? player->GetCurrentPackage() : nullptr;
        if (_runningPackageLogged && pkg == _loggedPackage) return;
        _runningPackageLogged = true;
        _loggedPackage = pkg;
        _forceGreetTriggered = false;
        _forceGreetProcedureActive = false;
        if (pkg) {
            SKSE::log::info("Player is running package {:08X}", pkg->GetFormID());
        } else {
            SKSE::log::info("Player has no current package");
        }
    }

    void IdleAIManager::CheckForceGreetReachedPuppet() {
        // A donor package that would ForceGreet or talk to the Dragonborn has been retargeted to the puppet.
        // When the player arrives within greeting distance, open self-dialogue once for this approach.
        // Packages that only travel to or follow the puppet (e.g. a follower's Follow package on a swapped
        // alias) don't count: following stays in range, so they opened dialogue every time idle AI engaged.
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = Session::GetSingleton().GetPuppet();
        auto* curPkg = player ? player->GetCurrentPackage() : nullptr;
        if (!puppet || !curPkg) {
            _forceGreetTriggered = false;
            _forceGreetProcedureActive = false;
            return;
        }

        bool isForceGreetPkg = _forceGreetProcedureActive || HasForceGreetProcedure(curPkg);
        bool talksToPuppet = isForceGreetPkg || (HasDialogueProcedure(curPkg) && PackageTargetsActor(curPkg, puppet));
        if (!talksToPuppet) {
            _forceGreetTriggered = false;
            return;
        }
        if (_forceGreetTriggered) return;

        float greetDist = isForceGreetPkg ? 300.0f : 250.0f;
        float dist = player->GetDistance(puppet, false, false);
        if (dist <= greetDist) {
            auto* topic = GetForceGreetTopic(curPkg);
            SKSE::log::info("Player package {:08X} reached puppet target (dist: {:.1f}, topic: {:08X}) - opening self-dialogue.",
                curPkg->GetFormID(), dist, topic ? topic->GetFormID() : 0);
            _forceGreetTriggered = true;
            DialogueManager::GetSingleton().OpenSelfDialogue(puppet, topic);
        }
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            bool isUserAction = false;
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* button = static_cast<const RE::ButtonEvent*>(event);
                isUserAction = button->IsPressed() || button->IsDown() || button->IsHeld();
            } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                const auto* stick = static_cast<const RE::ThumbstickEvent*>(event);
                isUserAction = std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f;
            }
            if (isUserAction) {
                OnUserInput();
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnUserInput() {
        _lastInputTime = std::chrono::steady_clock::now();
        if (_aiActive) {
            StopIdleAI();
        }
    }

    void IdleAIManager::CheckIdleAI() {
        if (!_enabled || !Session::GetSingleton().IsActive()) {
            if (_aiActive) {
                StopIdleAI();
            }
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        const char* blockedBy = nullptr;
        // While AI-driven, the engine treats the player like an NPC in scenes and waits for the player's AI to
        // run the scene's package, which idle AI never does: the player's scene lines stalled until pathing ended
        auto* scene = player ? player->GetCurrentScene() : nullptr;
        bool inScene = false;
        if (!player || player->IsInCombat()) {
            blockedBy = "combat";
        } else if (IsBlockingMenuOpen()) {
            blockedBy = "menu";
        } else if (scene && scene->isPlaying) {
            blockedBy = "scene";
            inScene = true;
        }
        if (blockedBy != _blockedBy) {
            if (inScene) {
                SKSE::log::info("Idle AI waiting: scene {:08X} '{}'", scene->GetFormID(), scene->GetFormEditorID());
            } else if (blockedBy) {
                SKSE::log::info("Idle AI waiting: {}", blockedBy);
            }
            _blockedBy = blockedBy;
        }
        if (blockedBy) {
            _lastInputTime = std::chrono::steady_clock::now();
            if (_aiActive) {
                StopIdleAI();
            }
            return;
        }

        float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - _lastInputTime).count();
        if (elapsed >= _idleTimeoutSeconds && !_aiActive) {
            StartIdleAI();
        }
    }

    void IdleAIManager::StartIdleAI() {
        if (!Session::GetSingleton().IsActive() || !_enabled) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat() || IsBlockingMenuOpen()) return;

        if (auto* pc = RE::PlayerControls::GetSingleton()) {
            _savedUserRunning = pc->data.running;
        }

        _aiActive = true;
        _forceGreetTriggered = false;
        _forceGreetProcedureActive = false;
        _runningPackageLogged = false;
        _evaluatedAfterEngage = false;
        _aiStartTime = std::chrono::steady_clock::now();
        _lastPackageSyncTime = _aiStartTime;

        SyncDonorPackagesToPlayer();
        player->AllowBleedoutDialogue(true);
        SetPlayerAIDriven(true);
        SKSE::log::info("Player idle timeout reached - engaging autonomous AI.");
    }

    void IdleAIManager::StopIdleAI() {
        if (!_aiActive) return;
        _aiActive = false;
        _forceGreetProcedureActive = false;

        SetPlayerAIDriven(false);
        ClearPlayerPackages();
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            player->EvaluatePackage(false, false);
        }
        if (auto* pc = RE::PlayerControls::GetSingleton()) {
            pc->data.autoMove = false;
            pc->data.moveInputVec = { 0.0f, 0.0f };
            pc->data.running = _savedUserRunning;
        }
        _loggedPackage = nullptr;
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("Disengaged autonomous AI.");
    }

    void IdleAIManager::OnImpersonationChanged(bool a_active) {
        if (!a_active) {
            StopIdleAI();
        } else if (auto* pc = RE::PlayerControls::GetSingleton()) {
            _savedUserRunning = pc->data.running;
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager: impersonation active = {}", a_active);
    }

    bool IdleAIManager::RetargetPackageToPuppet(RE::TESPackage* a_pkg, RE::Actor* a_player, RE::Actor* a_puppet) {
        if (!a_pkg || !a_player || !a_puppet) return false;
        bool retargeted = false;

        ForEachPackageTarget(a_pkg, [&](RE::PackageTarget* a_targ) {
            if (!TargetReferencesActor(a_targ, a_player)) return;
            auto it = std::find_if(_retargetedTargets.begin(), _retargetedTargets.end(),
                [a_targ](const RetargetedPackageTarget& a_rec) { return a_rec.target == a_targ; });
            if (it == _retargetedTargets.end()) {
                RetargetedPackageTarget rec;
                rec.target = a_targ;
                rec.origType = a_targ->targType;
                rec.origHandle = a_targ->target.handle;
                rec.origObject = a_targ->target.object;
                _retargetedTargets.push_back(rec);
            }
            a_targ->targType = 0;
            a_targ->target.handle = a_puppet->GetHandle();
            retargeted = true;
        });

        ForEachPackageLocation(a_pkg, [&](RE::PackageLocation* a_loc) {
            if (!LocationReferencesActor(a_loc, a_player)) return;
            auto it = std::find_if(_retargetedLocations.begin(), _retargetedLocations.end(),
                [a_loc](const RetargetedPackageLocation& a_rec) { return a_rec.location == a_loc; });
            if (it == _retargetedLocations.end()) {
                RetargetedPackageLocation rec;
                rec.location = a_loc;
                rec.origType = static_cast<std::uint8_t>(a_loc->locType.underlying());
                rec.origHandle = a_loc->data.refHandle;
                rec.origObject = a_loc->data.object;
                rec.origRad = a_loc->rad;
                _retargetedLocations.push_back(rec);
            }
            a_loc->locType = RE::PackageLocation::Type::kNearReference;
            a_loc->data.refHandle = a_puppet->GetHandle();
            // ForceGreet packages often have a preliminary Travel branch whose Trigger Location radius is
            // 500+ units around the player, while the ForceGreet branch only activates within 300 units.
            // Clamp the radius so the Travel branch brings the impersonator within greeting distance.
            a_loc->rad = std::min<std::uint32_t>(a_loc->rad, 150u);
            retargeted = true;
        });

        if (retargeted) {
            SKSE::log::info("Retargeted package {:08X} from player to puppet {:08X}", a_pkg->GetFormID(), a_puppet->GetFormID());
        }
        return retargeted;
    }

    void IdleAIManager::RetargetAliasPackagesToPuppet() {
        // Packages on the donor's swapped aliases apply to the player now. Any that target or locate
        // relative to the player (force-greets, "approach the Dragonborn") would make the player target
        // itself, so point their targets and locations at the puppet.
        auto& session = Session::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = session.GetPuppet();
        auto* aliasArray = player ? player->extraList.GetByType<RE::ExtraAliasInstanceArray>() : nullptr;
        if (!puppet || !aliasArray) return;

        RE::BSReadLockGuard locker(aliasArray->lock);
        for (auto* inst : aliasArray->aliases) {
            if (!inst || !inst->quest || !inst->alias || !inst->instancedPackages) continue;
            if (!AliasSwapManager::GetSingleton().IsSwappedAlias(inst->quest->GetFormID(), inst->alias->aliasID)) continue;

            for (auto* pkg : *inst->instancedPackages) {
                if (!pkg || pkg->GetFormType() != RE::FormType::Package) continue;
                RetargetPackageToPuppet(pkg, player, puppet);
            }
        }
    }

    void IdleAIManager::SyncDonorPackagesToPlayer() {
        auto& session = Session::GetSingleton();
        auto* donor = session.GetDonor();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = session.GetPuppet();
        if (!session.IsActive() || !donor || !player || !_aiActive) return;

        auto* donorBase = donor->GetActorBase();
        auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
        if (!donorBase || !playerBase) return;

        RetargetAliasPackagesToPuppet();

        // The donor's own (base record) packages go on the player's base record. The swapped aliases
        // apply their packages to the player directly. ForceGreet packages on the donor's base record
        // are also retargeted to the puppet and synced; non-ForceGreet player-targeted base packages
        // (such as follower Follow packages) are skipped.
        std::vector<RE::TESPackage*> donorPkgs;
        for (auto* pkg : donorBase->aiPackages.packages) {
            if (!pkg) continue;
            if (HasForceGreetProcedure(pkg) && puppet) {
                RetargetPackageToPuppet(pkg, player, puppet);
            } else if (PackageTargetsActor(pkg, player)) {
                continue;
            }
            if (std::find(donorPkgs.begin(), donorPkgs.end(), pkg) == donorPkgs.end()) {
                donorPkgs.push_back(pkg);
            }
        }
        // The fallback when nothing else applies
        if (auto* defaultSandbox = Forms::defaultSandboxPackage) {
            if (std::find(donorPkgs.begin(), donorPkgs.end(), defaultSandbox) == donorPkgs.end()) {
                donorPkgs.push_back(defaultSandbox);
            }
        }

        std::vector<RE::TESPackage*> playerPkgs;
        for (auto* pkg : playerBase->aiPackages.packages) {
            if (pkg) {
                playerPkgs.push_back(pkg);
            }
        }

        if (donorPkgs != playerPkgs) {
            playerBase->aiPackages.packages.clear();
            for (auto it = donorPkgs.rbegin(); it != donorPkgs.rend(); ++it) {
                playerBase->aiPackages.packages.push_front(*it);
            }
            player->EvaluatePackage(false, false);
            std::string ids;
            for (auto* pkg : donorPkgs) {
                ids += fmt::format(" {:08X}", pkg->GetFormID());
            }
            SKSE::log::info("Synchronized {} donor AI packages to player:{}", donorPkgs.size(), ids);
        }
    }

    void IdleAIManager::ClearPlayerPackages() {
        for (const auto& entry : _retargetedTargets) {
            auto* targ = entry.target;
            if (!targ) continue;
            targ->targType = entry.origType;
            if (entry.origType == 0) {
                targ->target.handle = entry.origHandle;
            } else {
                targ->target.object = entry.origObject;
            }
        }
        for (const auto& entry : _retargetedLocations) {
            auto* loc = entry.location;
            if (!loc) continue;
            loc->locType = static_cast<RE::PackageLocation::Type>(entry.origType);
            if (loc->locType == RE::PackageLocation::Type::kNearReference) {
                loc->data.refHandle = entry.origHandle;
            } else {
                loc->data.object = entry.origObject;
            }
            loc->rad = entry.origRad;
        }
        if (!_retargetedTargets.empty() || !_retargetedLocations.empty()) {
            SKSE::log::info("ClearPlayerPackages: Restored {} retargeted targets and {} retargeted locations",
                _retargetedTargets.size(), _retargetedLocations.size());
        }
        _retargetedTargets.clear();
        _retargetedLocations.clear();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        if (auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase())) {
            playerBase->aiPackages.packages.clear();
        }
    }

}
