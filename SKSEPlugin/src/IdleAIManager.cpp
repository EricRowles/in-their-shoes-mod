#include "PCH.h"
#include "IdleAIManager.h"
#include "AliasSwapManager.h"
#include "Session.h"
#include "DialogueManager.h"
#include "Forms.h"
#include "GameClock.h"
#include "GameUtil.h"
#include "PackageRetarget.h"
#include "PackageUtil.h"
#include "PlayerBusy.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "RE/C/ControlMap.h"
#include "RE/P/PlayerControls.h"
#include "RE/U/UserEvents.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESPackage.h"
#include "RE/T/TESCustomPackageData.h"
#include "RE/I/IPackageData.h"
#include "RE/B/BGSDefaultObjectManager.h"
#include "RE/B/BGSListForm.h"
#include "RE/B/BGSProcedureTreeBranch.h"
#include "RE/B/BGSProcedureTreeProcedure.h"
#include "RE/T/TESQuest.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/B/BGSRefAlias.h"
#include "RE/F/FunctionArguments.h"

namespace InTheirShoes {

    using namespace PackageUtil;

    // How long engaging waits for ITS_IdlePackage.Apply to put the player in its alias before going ahead anyway
    constexpr float kEngageWaitSeconds = 2.0f;
    // How often the donor's packages are synced to the player while idle AI runs
    constexpr float kPackageSyncSeconds = 1.5f;

    // MCM idle package indexes (ITS_MCMScript.GetIdlePackageNames); index n runs ITS_PackageManager's alias n - 1
    constexpr std::int32_t kSandboxWidePackage = 1;
    constexpr std::int32_t kTravelPackage = 3;
    // The ITS_PackageManager alias the Travel package travels to
    constexpr std::string_view kTravelDestinationAlias = "TravelDestination"sv;

    namespace {
        // Procedures are told apart by vtable (CommonLib has no class for BGSProcedureForceGreet or
        // BGSProcedureDialogue). Branches are matched by RTTI, so sequences (a branch subclass) are searched too (#46).
        bool HasProcedureTreeProcedure(const RE::IProcedureTreeItem* a_item, std::uintptr_t a_procVtbl) {
            if (!a_item) return false;
            if (const auto* procItem = skyrim_cast<const RE::BGSProcedureTreeProcedure*>(a_item)) {
                if (procItem->procedure && *reinterpret_cast<const std::uintptr_t*>(procItem->procedure) == a_procVtbl) {
                    return true;
                }
            } else if (const auto* branch = skyrim_cast<const RE::BGSProcedureTreeBranch*>(a_item)) {
                for (const auto* child : branch->conditions) {
                    if (HasProcedureTreeProcedure(child, a_procVtbl)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool HasProcedure(const RE::TESPackage* a_pkg, RE::PACKAGE_PROCEDURE_TYPE a_type, std::uintptr_t a_procVtbl) {
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

        bool HasForceGreetProcedure(const RE::TESPackage* a_pkg) {
            static const std::uintptr_t kForceGreetVtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_BGSProcedureForceGreet[0]).address();
            return HasProcedure(a_pkg, RE::PACKAGE_PROCEDURE_TYPE::kGreet, kForceGreetVtbl);
        }

        bool HasDialogueProcedure(const RE::TESPackage* a_pkg) {
            static const std::uintptr_t kDialogueVtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_BGSProcedureDialogue[0]).address();
            return HasProcedure(a_pkg, RE::PACKAGE_PROCEDURE_TYPE::kDialogue, kDialogueVtbl);
        }

        RE::TESTopic* GetForceGreetTopic(const RE::TESPackage* a_pkg) {
            const auto* customData = a_pkg ? skyrim_cast<const RE::TESCustomPackageData*>(a_pkg->data) : nullptr;
            if (!customData) return nullptr;

            RE::TESTopic* topic = nullptr;
            ForEachPackageInput<RE::BGSPackageDataTopic>(a_pkg, [&](RE::BGSPackageDataTopic* a_input) {
                // kind 0 names a topic form; others index the engine's named topics
                if (!topic && a_input->kind == 0 && a_input->topic) {
                    topic = a_input->topic;
                }
            });
            if (topic) return topic;
            return customData->templateParent ? GetForceGreetTopic(customData->templateParent) : nullptr;
        }

        // The default package list (DPLT) an NPC runs when none of its own packages apply: its own, its template's
        // (when it inherits it), else the game's DefaultMasterPackageList
        RE::BGSListForm* GetDefaultPackageList(RE::TESActorBase* a_base) {
            using UseFlag = RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG;
            for (auto* npc = skyrim_cast<RE::TESNPC*>(a_base); npc;) {
                if (npc->defaultPackList) return npc->defaultPackList;
                const bool inherits = npc->UsesTemplate() && npc->actorData.templateUseFlags.all(UseFlag::kAIDefPackList);
                npc = inherits && npc->baseTemplateForm ? npc->baseTemplateForm->As<RE::TESNPC>() : nullptr;
            }
            auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
            return defaults ? defaults->GetObject<RE::BGSListForm>(RE::BGSDefaultObjectManager::DefaultObject::kDefaultPackList) : nullptr;
        }

        // Menus that stop running idle AI. Others (inventory, magic, map, journal, favorites, the console) only
        // pause its countdown, so the player can manage equipment while idle AI walks (#45). The container, barter,
        // crafting and book menus open from Activate, which already stops it, unless a script opens them.
        bool IsMenuThatStopsIdleAI() {
            static constexpr std::array kMenus = {
                RE::DialogueMenu::MENU_NAME, RE::LoadingMenu::MENU_NAME, RE::RaceSexMenu::MENU_NAME,
                RE::BarterMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME,
                RE::LockpickingMenu::MENU_NAME, RE::TrainingMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME,
                RE::BookMenu::MENU_NAME,
            };
            auto* ui = RE::UI::GetSingleton();
            return ui && std::ranges::any_of(kMenus, [ui](std::string_view a_menu) { return ui->IsMenuOpen(a_menu); });
        }

        // Whether this ForceGreet procedure is the impersonating player's
        bool IsPlayerForceGreet(const RE::IProcedureTreeItem::ExecStateContext* a_execState) {
            return Session::GetSingleton()->IsActive() && a_execState && a_execState->context &&
                   a_execState->context->actor == RE::PlayerCharacter::GetSingleton() && a_execState->nodeState;
        }

        // Ends a procedure as succeeded, ending its package, as the engine does (AE ID 29479). ExecStateContext extends
        // past CommonLib's: +0x10 done, +0x11 succeeded, +0x12 also set ProcedureExecContext::unk20.
        void MarkProcedureSucceeded(RE::IProcedureTreeItem::ExecStateContext* a_execState) {
            auto* raw = reinterpret_cast<std::uint8_t*>(a_execState);
            raw[0x10] = 1;
            raw[0x11] = 1;
            if (raw[0x12]) {
                a_execState->context->unk20 = true;
            }
        }

        // BGSProcedureForceGreet targets the player (+0x20), which spun in place targeting itself. Points it at the
        // puppet; false if there is none.
        bool RetargetForceGreetToPuppet(RE::IProcedureTreeItem::ExecStateContext* a_execState) {
            constexpr std::uintptr_t kTargetHandleOffset = 0x20;
            auto* puppet = Session::GetSingleton()->GetPuppet();
            if (!puppet) return false;
            *reinterpret_cast<RE::ObjectRefHandle*>(reinterpret_cast<std::uintptr_t>(a_execState->nodeState) + kTargetHandleOffset) =
                puppet->GetHandle();
            return true;
        }
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

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerCheckForCurrentAliasPackage = playerVtbl.write_vfunc(0x49, &Hook_Player_CheckForCurrentAliasPackage);
        SKSE::log::info("Hooked PlayerCharacter::CheckForCurrentAliasPackage (0x49)");
    }

    // Swapped aliases' packages made a player using furniture stand up while not AI-driven (#42), so none is
    // picked unless idle AI runs. Runs on AI threads: reads only Session and an atomic.
    RE::TESPackage* IdleAIManager::Hook_Player_CheckForCurrentAliasPackage(RE::Actor* a_this) {
        if (Session::GetSingleton()->IsActive() && !IdleAIManager::GetSingleton()->_running.load()) {
            return nullptr;
        }
        return _origPlayerCheckForCurrentAliasPackage(a_this);
    }

    std::uint64_t IdleAIManager::Hook_ForceGreet_InitExecState(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState) {
        auto ret = _origForceGreetInitExecState(a_this, a_execState);
        if (IsPlayerForceGreet(a_execState) && RetargetForceGreetToPuppet(a_execState)) {
            IdleAIManager::GetSingleton()->_forceGreetProcedureActive = true;
        }
        return ret;
    }

    std::uint64_t IdleAIManager::Hook_ForceGreet_Execute(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState) {
        const bool isPlayerForceGreet = IsPlayerForceGreet(a_execState);
        if (isPlayerForceGreet) {
            // The procedure never sees the self-dialogue, so it greeted again and its scene never ended (#38). Once
            // greeted, succeed as the engine would.
            auto* context = a_execState->context;
            auto* package = context->package ? context->package->package : nullptr;
            const auto greeted = IdleAIManager::GetSingleton()->_greetedPackage.load();
            if (package && greeted && package->GetFormID() == greeted) {
                MarkProcedureSucceeded(a_execState);
                return 0;
            }
        }
        if (isPlayerForceGreet && RetargetForceGreetToPuppet(a_execState)) {
            IdleAIManager::GetSingleton()->_forceGreetProcedureActive = true;
        }
        auto ret = _origForceGreetExecute(a_this, a_execState);
        if (isPlayerForceGreet) {
            // ForceGreet sets forceGoodbye (+0xB2) while pathing, which would close self-dialogue next frame
            if (auto* topicMgr = RE::MenuTopicManager::GetSingleton()) {
                topicMgr->forceGoodbye = false;
            }
        }
        return ret;
    }

    bool IdleAIManager::CombatStopsIdleAI(RE::PlayerCharacter* a_player) const {
        // Combat stops idle movement, except on the way to a Travel destination: ITS_TravelPackage has
        // Ignore Combat, so the player keeps going. Once there, the sandbox package stops for combat as usual.
        const bool travelling = Settings::GetSingleton()->GetIdlePackage() == kTravelPackage && !_travelArrived;
        return a_player->IsInCombat() && !travelling;
    }

    std::string_view IdleAIManager::ToString(StopReason a_reason) {
        switch (a_reason) {
            case StopReason::kInput: return "player input"sv;
            case StopReason::kCombat: return "combat"sv;
            case StopReason::kMenu: return "menu"sv;
            case StopReason::kSelfDialogue: return "self-dialogue"sv;
            case StopReason::kInactive: return "idle AI off or no impersonation"sv;
            case StopReason::kSessionChanged: return "impersonation changed"sv;
            case StopReason::kLostControl: return "something else turned AI-driven off"sv;
            case StopReason::kOtherControl: return "something else made the player AI-driven"sv;
            default: return "unknown"sv;
        }
    }

    void IdleAIManager::Update() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        HandleRequests(player);

        // Ownership against the engine's state. SetAIDriven (Game.SetPlayerAIDriven) is a plain flag, not a
        // count, so a script that turns it off also ends idle AI's control.
        const auto& flags = player->GetPlayerRuntimeData().playerFlags;
        if (_owned && !flags.aiControlledPackage) {
            _owned = false;
            Disengage(player, StopReason::kLostControl);
        }

        const auto& settings = *Settings::GetSingleton();
        if (!settings.IsIdleAIEnabled() || !Session::GetSingleton()->IsActive()) {
            Disengage(player, StopReason::kInactive);
            _idleSeconds = 0.0f;
            ReleaseGreetedPackage();
            return;
        }
        if (CombatStopsIdleAI(player)) {
            Disengage(player, StopReason::kCombat);
        } else if (IsRunning() && IsMenuThatStopsIdleAI()) {
            Disengage(player, StopReason::kMenu);
        } else if (_state == State::kEngaging && IsPlayerAIControlled(player)) {
            Disengage(player, StopReason::kOtherControl);
        }

        if (IsRunning()) {
            UpdateRunning(player);
        } else {
            UpdateIdleTime(player);
        }
        ReleaseGreetedPackage();
    }

    void IdleAIManager::UpdateIdleTime(RE::PlayerCharacter* a_player) {
        // Idle time is game time (GameClock) while the player could be driven. Menus and the window being in the
        // background pause it (so alt+tab can't use it up, #45); anything that would stop idle AI resets it.
        // Sitting, sleeping or crafting counts as doing something, so idle AI doesn't walk the player away.
        // Combat goes through CombatStopsIdleAI, since the Travel package ignores it.
        const auto blockedBy = CombatStopsIdleAI(a_player) ?
                                   PlayerBusy::kCombat :
                                   WhatBlocks(a_player, BusyChecks(BusyCheck::kAIControlled, BusyCheck::kFurniture,
                                                            BusyCheck::kWindowInactive, BusyCheck::kMenu));
        LogPlayerBusy(_blockedBy, blockedBy, "Idle AI"sv);
        if (blockedBy != PlayerBusy::kNo) {
            if (blockedBy != PlayerBusy::kWindowInactive && blockedBy != PlayerBusy::kMenu) {
                _idleSeconds = 0.0f;
            }
            return;
        }

        _idleSeconds += GameClock::FrameSeconds();
        if (_idleSeconds >= Settings::GetSingleton()->GetIdleTimeout()) {
            BeginEngage(a_player);
        }
    }

    void IdleAIManager::UpdateRunning(RE::PlayerCharacter* a_player) {
        if (_state == State::kEngaging) {
            // AI-driven turns on once the alias is in place, so the player doesn't briefly run the wrong package.
            // A call superseded by a later one counts once that one finishes. If Papyrus is held up, go ahead.
            _engageWaitSeconds += GameClock::FrameSeconds();
            const bool applied = _appliedSequence.load(std::memory_order_acquire) >= _engageSequence;
            if (!applied && _engageWaitSeconds < kEngageWaitSeconds) return;
            if (!applied) {
                SKSE::log::warn("Idle package state {} not applied after {:.1f}s; engaging anyway", _engageSequence, _engageWaitSeconds);
            }
            FinishEngage(a_player);
        }

        auto* scene = GetPlayingScene(a_player);
        const ScenePhase scenePhase{ scene ? scene->GetFormID() : 0, scene ? scene->currentPhaseIndex : 0 };
        if (GameClock::SecondsSince(_lastPackageSyncTime) >= kPackageSyncSeconds || scenePhase != _syncedScenePhase) {
            _syncedScenePhase = scenePhase;
            _lastPackageSyncTime = GameClock::Now();
            SyncDonorPackagesToPlayer();
            CheckTravelArrival();
        }
        LogRunningPackage();
        CheckForceGreetReachedPuppet();
    }

    void IdleAIManager::HandleRequests(RE::PlayerCharacter* a_player) {
        const auto requests = _requests.exchange(0, std::memory_order_acq_rel);
        if (!requests) return;
        if (requests & kPostLoad) {
            HandlePostLoad(a_player);
        }
        if (requests & kSessionChanged) {
            // Switching donors ends and begins a session in one call, so Update never sees it inactive. The
            // packages synced from the last donor go either way.
            _idleSeconds = 0.0f;
            Disengage(a_player, StopReason::kSessionChanged);
            ClearPlayerPackages();
        }
        if (requests & kSelfDialogueEnded) {
            HandleSelfDialogueEnded();
        }
        if (requests & kPackageChanged) {
            const auto index = Settings::GetSingleton()->GetIdlePackage();
            _travelArrived = false;
            SKSE::log::info("Idle AI package: {}", index > 0 ? fmt::format("alias {}", index - 1) : "the donor's own"s);
            ApplyIdlePackage();
        }
        if (requests & kDestinationChanged) {
            _travelArrived = false;
            const auto* destination = Forms::TravelDestination(Settings::GetSingleton()->GetTravelDestination());
            SKSE::log::info("Idle AI travel destination: {}",
                destination && destination->location ? destination->location->GetName() : "none");
            // A running Travel package heads for the new destination
            ApplyIdlePackage();
        }
        if (requests & kActionInput) {
            _idleSeconds = 0.0f;
            Disengage(a_player, StopReason::kInput);
        }
    }

    void IdleAIManager::HandlePostLoad(RE::PlayerCharacter* a_player) {
        // A save made while idle AI ran keeps the player AI-driven and in a package alias (#35), and idle AI isn't
        // running now. Ownership isn't saved, so AI-driven is taken to be idle AI's if the player is impersonating
        // or in a package alias.
        bool inPackageAlias = false;
        if (auto* quest = Forms::idlePackageQuest) {
            for (const auto* alias : quest->aliases) {
                if (alias && quest->GetAliasedRef(alias->aliasID).get().get() == a_player) {
                    inPackageAlias = true;
                    break;
                }
            }
        }
        if (!inPackageAlias && !Session::GetSingleton()->IsActive()) return;
        if (a_player->GetPlayerRuntimeData().playerFlags.aiControlledPackage) {
            a_player->SetAIDriven(false);
            SKSE::log::info("Turned off AI-driven saved while idle AI ran");
        }
        ApplyIdlePackage(true);
    }

    void IdleAIManager::BeginEngage(RE::PlayerCharacter* a_player) {
        if (auto* pc = RE::PlayerControls::GetSingleton()) {
            _savedUserRunning = pc->data.running;
        }

        _state = State::kEngaging;
        _running = true;
        _forceGreetTriggered = false;
        _forceGreetProcedureActive = false;
        _runningPackageLogged = false;
        _travelArrived = false;  // Rechecked at the next package sync
        _scenePackagesRunning = false;
        _lastPackageSyncTime = GameClock::Now();
        if (auto* scene = GetPlayingScene(a_player)) {
            _syncedScenePhase = { scene->GetFormID(), scene->currentPhaseIndex };
        } else {
            _syncedScenePhase = {};
        }

        SyncDonorPackagesToPlayer();
        a_player->AllowBleedoutDialogue(true);
        // Always sent, even if the alias doesn't change, so its completion says when to turn AI-driven on
        _engageSequence = ApplyIdlePackage(true);
        _engageWaitSeconds = 0.0f;
        SKSE::log::info("Player idle timeout reached - engaging autonomous AI.");
    }

    void IdleAIManager::FinishEngage(RE::PlayerCharacter* a_player) {
        // Set here rather than through Game.SetPlayerAIDriven, so ownership changes in the same instant as the
        // engine's flag (#45). The Papyrus function calls this same engine function and nothing else.
        a_player->SetAIDriven(true);
        _owned = true;
        _state = State::kDriving;
        _aiStartTime = GameClock::Now();
        a_player->EvaluatePackage(true, true);
        SKSE::log::info("Player is AI-driven");
    }

    void IdleAIManager::Disengage(RE::PlayerCharacter* a_player, StopReason a_reason) {
        if (!IsRunning()) return;
        _state = State::kManual;
        _running = false;
        _forceGreetProcedureActive = false;
        _scenePackagesRunning = false;

        // Control goes back before the aliases change: each alias call waits a frame, and in between the AI's
        // slope pitch drove the camera (straight up or down when stopping on a slope).
        if (_owned) {
            a_player->SetAIDriven(false);
            _owned = false;
        }
        ApplyIdlePackage();
        ClearPlayerPackages();
        a_player->EvaluatePackage(false, false);
        if (auto* pc = RE::PlayerControls::GetSingleton()) {
            pc->data.autoMove = false;
            pc->data.moveInputVec = { 0.0f, 0.0f };
            pc->data.running = _savedUserRunning;
        }
        _loggedPackage = nullptr;
        _idleSeconds = 0.0f;
        SKSE::log::info("Disengaged autonomous AI: {}", ToString(a_reason));
    }

    void IdleAIManager::StopForSelfDialogue() {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            Disengage(player, StopReason::kSelfDialogue);
        }
    }

    void IdleAIManager::LogRunningPackage() {
        // Report the first package seen after engaging (or that there is none), then every change
        if (_state != State::kDriving || GameClock::SecondsSince(_aiStartTime) < 1.0f) return;
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
        // Opens self-dialogue once per approach when a retargeted greet package brings the player to the puppet.
        // Travel and follow packages don't count: they stay in range, so they reopened it whenever idle AI engaged.
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = Session::GetSingleton()->GetPuppet();
        auto* curPkg = player ? player->GetCurrentPackage() : nullptr;
        if (_state != State::kDriving || !puppet || !curPkg) {
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
        if (_forceGreetTriggered || curPkg->GetFormID() == _greetedPackage.load()) return;

        float greetDist = isForceGreetPkg ? 300.0f : 250.0f;
        float dist = player->GetDistance(puppet, false, false);
        if (dist <= greetDist) {
            auto* topic = GetForceGreetTopic(curPkg);
            SKSE::log::info("Player package {:08X} reached puppet target (dist: {:.1f}, topic: {:08X}) - opening self-dialogue.",
                curPkg->GetFormID(), dist, topic ? topic->GetFormID() : 0);
            _forceGreetTriggered = true;
            if (DialogueManager::GetSingleton()->OpenSelfDialogue(puppet, topic) && isForceGreetPkg) {
                _greetingPackage = curPkg->GetFormID();
            }
        }
    }

    void IdleAIManager::OnSelfDialogueEnded() {
        Post(kSelfDialogueEnded);
    }

    void IdleAIManager::HandleSelfDialogueEnded() {
        if (!_greetingPackage) return;
        auto* scene = GetPlayingScene(RE::PlayerCharacter::GetSingleton());
        _greetedScene = scene ? scene->GetFormID() : 0;
        _greetedPackage = _greetingPackage;
        _greetingPackage = 0;
        SKSE::log::info("ForceGreet package {:08X} has greeted (scene {:08X}); it now reports done",
            _greetedPackage.load(), _greetedScene);
    }

    void IdleAIManager::ReleaseGreetedPackage() {
        const auto greeted = _greetedPackage.load();
        if (!greeted) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* scene = GetPlayingScene(player);
        auto* pkg = player ? player->GetCurrentPackage() : nullptr;
        const bool done = _greetedScene ? (!scene || scene->GetFormID() != _greetedScene)
                                        : (pkg && pkg->GetFormID() != greeted);
        if (!done) return;
        SKSE::log::info("Released greeted ForceGreet package {:08X}", greeted);
        _greetedPackage = 0;
        _greetedScene = 0;
    }

    bool IdleAIManager::IsActionInput(const RE::InputEvent* a_event) {
        // Only input that asks the player to act in the world takes control back (#45): moving, attacking, drawing
        // a weapon, activating, jumping, sneaking, shouting. Menu keys, hotkeys, the camera, Run/Sprint and keys
        // with no gameplay event (Alt, Tab) don't, so the player can manage equipment while idle AI walks.
        // Matched by user event, so rebinding and controllers work, and only in the gameplay context, where a
        // menu hasn't taken the input.
        static constexpr std::array kActionEvents = {
            &RE::UserEvents::forward, &RE::UserEvents::back, &RE::UserEvents::strafeLeft, &RE::UserEvents::strafeRight,
            &RE::UserEvents::autoMove, &RE::UserEvents::activate, &RE::UserEvents::leftAttack,
            &RE::UserEvents::rightAttack, &RE::UserEvents::readyWeapon, &RE::UserEvents::jump, &RE::UserEvents::sneak,
            &RE::UserEvents::shout,
        };

        auto* controlMap = RE::ControlMap::GetSingleton();
        auto* userEvents = RE::UserEvents::GetSingleton();
        if (!controlMap || !userEvents) return false;
        const auto& contexts = controlMap->GetRuntimeData().contextPriorityStack;
        if (contexts.empty() || contexts.back() != RE::UserEvents::INPUT_CONTEXT_ID::kGameplay) return false;

        switch (a_event->GetEventType()) {
            case RE::INPUT_EVENT_TYPE::kButton: {
                const auto* button = static_cast<const RE::ButtonEvent*>(a_event);
                if (!button->IsPressed()) return false;
                RE::BSFixedString name = button->QUserEvent();
                if (name.empty()) {
                    name = RE::BSFixedString(controlMap->GetUserEventName(button->GetIDCode(), button->GetDevice()));
                }
                return std::ranges::any_of(kActionEvents, [&](auto a_member) { return name == userEvents->*a_member; });
            }
            case RE::INPUT_EVENT_TYPE::kThumbstick: {
                // The left stick moves (Move); the right stick, like the mouse, only moves the camera
                const auto* stick = static_cast<const RE::ThumbstickEvent*>(a_event);
                return stick->IsLeft() && (std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f);
            }
            default:
                return false;
        }
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }
        // Runs on input threads, concurrently with Update, so it only posts the request (#45)
        for (auto* event = *a_event; event; event = event->next) {
            if (IsActionInput(event)) {
                Post(kActionInput);
                break;
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnImpersonationChanged() {
        Post(kSessionChanged);
        SKSE::log::info("Idle AI: impersonation active = {}", Session::GetSingleton()->IsActive());
    }

    void IdleAIManager::RetargetAliasPackagesToPuppet() {
        // Swapped aliases' packages that target the player would make it target itself; point them at the puppet.
        auto& session = *Session::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = session.GetPuppet();
        auto* aliasArray = player ? player->extraList.GetByType<RE::ExtraAliasInstanceArray>() : nullptr;
        if (!puppet || !aliasArray) return;

        RE::BSReadLockGuard locker(aliasArray->lock);
        for (auto* inst : aliasArray->aliases) {
            if (!inst || !inst->quest || !inst->alias || !inst->instancedPackages) continue;
            if (!AliasSwapManager::GetSingleton()->IsSwappedAlias(inst->quest->GetFormID(), inst->alias->aliasID)) continue;

            for (auto* pkg : *inst->instancedPackages) {
                if (!pkg || pkg->GetFormType() != RE::FormType::Package) continue;
                PackageRetarget::GetSingleton()->RetargetPlayerToPuppet(pkg, puppet);
            }
        }
    }

    void IdleAIManager::OnIdlePackageChanged() {
        Post(kPackageChanged);
    }

    void IdleAIManager::OnTravelDestinationChanged() {
        Post(kDestinationChanged);
    }

    void IdleAIManager::OnPostLoadGame() {
        Post(kPostLoad);
    }

    void IdleAIManager::CheckTravelArrival() {
        // Travel only reaches the city's center marker, so sandbox anywhere in the city's Location (walled cities are
        // their own worldspaces, so distance doesn't work).
        const auto& settings = *Settings::GetSingleton();
        if (settings.GetIdlePackage() != kTravelPackage || _travelArrived) return;
        const auto* destination = Forms::TravelDestination(settings.GetTravelDestination());
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!destination || !destination->location || !player) return;
        auto* loc = player->GetCurrentLocation();
        if (loc && (loc == destination->location || destination->location->IsChild(loc))) {
            _travelArrived = true;
            SKSE::log::info("Arrived in {}; sandboxing there", destination->location->GetName());
            ApplyIdlePackage();
        }
    }

    IdleAIManager::IdlePackageState IdleAIManager::DesiredIdlePackageState() const {
        IdlePackageState state{};
        const auto& settings = *Settings::GetSingleton();
        const auto index = settings.GetIdlePackage();
        const auto* quest = Forms::idlePackageQuest;
        // While scene package actions run, the alias is left empty so it doesn't outrank them
        if (!IsRunning() || _scenePackagesRunning || !quest || index <= 0 || static_cast<std::size_t>(index) > quest->aliases.size()) {
            return state;
        }

        const bool travelling = index == kTravelPackage;
        state.aliasID = (travelling && _travelArrived ? kSandboxWidePackage : index) - 1;
        if (travelling && !_travelArrived) {
            // The Travel package travels to the TravelDestination alias, which is filled alongside the player's alias
            const auto* destination = Forms::TravelDestination(settings.GetTravelDestination());
            state.destination = destination ? destination->marker : nullptr;
            for (const auto* alias : quest->aliases) {
                if (alias && alias->aliasName == kTravelDestinationAlias) {
                    state.destinationAliasID = static_cast<std::int32_t>(alias->aliasID);
                    break;
                }
            }
            if (!state.destination || state.destinationAliasID < 0) {
                SKSE::log::error("Travel package has no destination (marker {}, alias {})", state.destination != nullptr,
                    state.destinationAliasID);
            }
        }
        return state;
    }

    std::int32_t IdleAIManager::ApplyIdlePackage(bool a_force) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        const auto state = DesiredIdlePackageState();
        if (!vm || (state == _sentState && !a_force)) return 0;
        _sentState = state;

        // Aliases are filled and cleared in Papyrus (CommonLib can't clear an alias). Everything goes in one
        // call with a sequence number, so calls that run out of order can't leave a stale state behind.
        auto* quest = Forms::idlePackageQuest;
        auto* destination = state.destination;
        const std::int32_t sequence = ++_applySequence;
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        vm->DispatchStaticCall("ITS_IdlePackage", "Apply",
            RE::MakeFunctionArguments(std::move(quest), std::int32_t{ state.aliasID }, std::move(destination),
                std::int32_t{ state.destinationAliasID }, std::int32_t{ sequence }),
            callback);
        SKSE::log::info("Idle package state {}: package alias {}", sequence, state.aliasID);
        return sequence;
    }

    bool IdleAIManager::BeginIdlePackageApply(std::int32_t a_sequence) const {
        if (a_sequence != _applySequence.load(std::memory_order_acquire)) {
            SKSE::log::info("Skipped idle package state {}: superseded", a_sequence);
            return false;
        }
        return true;
    }

    void IdleAIManager::EndIdlePackageApply(std::int32_t a_sequence) {
        auto applied = _appliedSequence.load(std::memory_order_relaxed);
        while (a_sequence > applied && !_appliedSequence.compare_exchange_weak(applied, a_sequence, std::memory_order_acq_rel)) {
        }
    }

    void IdleAIManager::Clear() {
        // On revert, before a save loads, when no frame runs. The player is about to be replaced by the save's, so
        // the engine isn't touched here; HandlePostLoad tidies up what the save holds.
        _requests = 0;
        _state = State::kManual;
        _running = false;
        _owned = false;
        _idleSeconds = 0.0f;
        _forceGreetProcedureActive = false;
        _scenePackagesRunning = false;
        _loggedPackage = nullptr;
        ClearPlayerPackages();
        _travelArrived = false;
        _sentState = {};
        _greetingPackage = 0;
        _greetedPackage = 0;
        _greetedScene = 0;
        // Calls dispatched before the revert never count toward an engage after it
        _appliedSequence = _applySequence.load();
    }

    void IdleAIManager::SyncDonorPackagesToPlayer() {
        auto& session = *Session::GetSingleton();
        auto* donor = session.GetDonor();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = session.GetPuppet();
        if (!session.IsActive() || !donor || !player || !IsRunning()) return;

        auto* donorBase = donor->GetActorBase();
        auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
        if (!donorBase || !playerBase) return;

        RetargetAliasPackagesToPuppet();

        // The donor's base packages go on the player's base record (ForceGreets retargeted to the puppet, other
        // player-targeted ones skipped); swapped aliases apply theirs directly; the MCM's package alias outranks both.
        // The scene's package actions for the player's aliases come first, with the MCM alias released while they run.
        auto& retarget = *PackageRetarget::GetSingleton();
        auto scenePkgs = GetScenePackagesInCurrentPhase(GetPlayingScene(player), player);
        for (auto* pkg : scenePkgs) {
            retarget.RetargetPlayerToPuppet(pkg, puppet);
        }
        const auto scenePkgCount = scenePkgs.size();
        _scenePackagesRunning = scenePkgCount > 0;
        ApplyIdlePackage();

        std::vector<RE::TESPackage*> donorPkgs = std::move(scenePkgs);
        auto addDonorPackage = [&](RE::TESPackage* a_pkg) {
            if (!a_pkg) return;
            if (HasForceGreetProcedure(a_pkg) && puppet) {
                retarget.RetargetPlayerToPuppet(a_pkg, puppet);
            } else if (PackageTargetsActor(a_pkg, player)) {
                return;
            }
            if (!std::ranges::contains(donorPkgs, a_pkg)) {
                donorPkgs.push_back(a_pkg);
            }
        };
        for (auto* pkg : donorBase->aiPackages.packages) {
            addDonorPackage(pkg);
        }
        // The player has no default package list to fall back on, so the donor's goes last, below its own packages
        if (auto* defaultPackList = GetDefaultPackageList(donorBase)) {
            defaultPackList->ForEachForm([&](RE::TESForm* a_form) {
                addDonorPackage(a_form ? a_form->As<RE::TESPackage>() : nullptr);
                return RE::BSContainer::ForEachResult::kContinue;
            });
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
            SKSE::log::info("Synchronized {} AI packages to player ({} from the scene):{}", donorPkgs.size(), scenePkgCount, ids);
        }
    }

    void IdleAIManager::ClearPlayerPackages() {
        PackageRetarget::GetSingleton()->RestorePlayerToPuppet();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        if (auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase())) {
            playerBase->aiPackages.packages.clear();
        }
    }

}
