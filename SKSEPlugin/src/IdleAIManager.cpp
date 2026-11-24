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
#include "PlayerMoveSpeed.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace InTheirShoes {

    using namespace PackageUtil;

    // How often the donor's packages are synced to the player while idle AI runs
    constexpr float kPackageSyncSeconds = 1.5f;

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

        // BGSProcedureForceGreet greets the handle at nodeState+0x20, which its InitExecState (AE ID 28696) fills with
        // the player's (ID 403520) rather than reading a package target, so PackageRetarget can't reach it: the player
        // spun in place greeting itself. Points it at the puppet. (PackageRetarget still handles the package's other
        // inputs that name the player, such as a Travel branch's location.)
        void RetargetForceGreetToPuppet(RE::IProcedureTreeItem::ExecStateContext* a_execState) {
            constexpr std::uintptr_t kTargetHandleOffset = 0x20;
            if (auto* puppet = Session::GetSingleton()->GetPuppet()) {
                *reinterpret_cast<RE::ObjectRefHandle*>(reinterpret_cast<std::uintptr_t>(a_execState->nodeState) + kTargetHandleOffset) =
                    puppet->GetHandle();
            }
        }

        // Input is matched by user event, so rebinding and controllers work, and only in the gameplay context,
        // where a menu hasn't taken it
        bool InGameplayContext() {
            auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) return false;
            const auto& contexts = controlMap->GetRuntimeData().contextPriorityStack;
            return !contexts.empty() && contexts.back() == RE::UserEvents::INPUT_CONTEXT_ID::kGameplay;
        }

        // What ReferenceAlias.Clear does (its native, AE 55286, calls this). The SE ID sits next to ForceRefIntoAlias's,
        // as in AE; it was matched by function layout, not tested on SE.
        void ClearAlias(RE::TESQuest* a_quest, RE::BGSBaseAlias* a_alias) {
            using ClearAlias_t = void(RE::TESQuest*, RE::BGSBaseAlias*);
            static REL::Relocation<ClearAlias_t> clearAlias{ RELOCATION_ID(24522, 25051) };
            clearAlias(a_quest, a_alias);
        }

        RE::BSFixedString UserEventName(const RE::ButtonEvent* a_button) {
            RE::BSFixedString name = a_button->QUserEvent();
            auto* controlMap = RE::ControlMap::GetSingleton();
            if (name.empty() && controlMap) {
                name = RE::BSFixedString(controlMap->GetUserEventName(a_button->GetIDCode(), a_button->GetDevice()));
            }
            return name;
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
        _origForceGreetExecute = forceGreetVtbl.write_vfunc(0x0C, &Hook_ForceGreet_Execute);
        SKSE::log::info("Hooked BGSProcedureForceGreet::Execute (0x0C)");

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

    std::uint64_t IdleAIManager::Hook_ForceGreet_Execute(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState) {
        if (!IsPlayerForceGreet(a_execState)) {
            return _origForceGreetExecute(a_this, a_execState);
        }
        // The procedure never sees the self-dialogue, so it greeted again and its scene never ended (#38). Once
        // greeted, succeed as the engine would.
        auto* context = a_execState->context;
        auto* package = context->package ? context->package->package : nullptr;
        const auto greeted = IdleAIManager::GetSingleton()->_greetedPackage.load();
        if (package && greeted && package->GetFormID() == greeted) {
            MarkProcedureSucceeded(a_execState);
            return 0;
        }
        // Here rather than in InitExecState: Execute (ID 28698) reads the target before acting on it, and a procedure
        // restored from a save runs Execute without InitExecState
        RetargetForceGreetToPuppet(a_execState);
        auto ret = _origForceGreetExecute(a_this, a_execState);
        // ForceGreet sets forceGoodbye (+0xB2) while pathing, which would close self-dialogue next frame
        if (auto* topicMgr = RE::MenuTopicManager::GetSingleton()) {
            topicMgr->forceGoodbye = false;
        }
        return ret;
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
        if (player->IsInCombat()) {
            Disengage(player, StopReason::kCombat);
        } else if (IsRunning() && IsMenuThatStopsIdleAI()) {
            Disengage(player, StopReason::kMenu);
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
        const auto blockedBy = WhatBlocks(a_player, BusyChecks(BusyCheck::kCombat, BusyCheck::kAIControlled, BusyCheck::kFurniture,
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
            Engage(a_player);
        }
    }

    void IdleAIManager::UpdateRunning(RE::PlayerCharacter* a_player) {
        auto* scene = GetPlayingScene(a_player);
        const ScenePhase scenePhase{ scene ? scene->GetFormID() : 0, scene ? scene->currentPhaseIndex : 0 };
        if (GameClock::SecondsSince(_lastPackageSyncTime) >= kPackageSyncSeconds || scenePhase != _syncedScenePhase) {
            _syncedScenePhase = scenePhase;
            _lastPackageSyncTime = GameClock::Now();
            SyncDonorPackagesToPlayer();
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
            SKSE::log::info("Idle AI package: {}", index > 0 ? fmt::format("alias {}", index - 1) : "the donor's own"s);
            ApplyIdlePackage();
        }
        if ((requests & kToggleRun) && IsRunning()) {
            PlayerMoveSpeed::ToggleAlwaysRun(a_player);
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

    void IdleAIManager::Engage(RE::PlayerCharacter* a_player) {
        _running = true;
        _greetAttemptPackage = 0;
        _loggedPackage.Reset();
        _scenePackagesRunning = false;
        _lastPackageSyncTime = GameClock::Now();
        if (auto* scene = GetPlayingScene(a_player)) {
            _syncedScenePhase = { scene->GetFormID(), scene->currentPhaseIndex };
        } else {
            _syncedScenePhase = {};
        }

        // The packages and the package alias are in place before AI-driven turns on, so the player doesn't briefly
        // run the wrong package
        SyncDonorPackagesToPlayer();
        a_player->AllowBleedoutDialogue(true);
        // Set here rather than through Game.SetPlayerAIDriven, so ownership changes in the same instant as the
        // engine's flag (#45). The Papyrus function calls this same engine function and nothing else.
        a_player->SetAIDriven(true);
        _owned = true;
        _aiStartTime = GameClock::Now();
        // Before the package starts, so its first path already has the player's walk/run setting (#49)
        PlayerMoveSpeed::Apply(a_player);
        a_player->EvaluatePackage(true, true);
        SKSE::log::info("Player idle timeout reached: idle AI drives the player");
    }

    void IdleAIManager::Disengage(RE::PlayerCharacter* a_player, StopReason a_reason) {
        if (!IsRunning()) return;
        _running = false;
        _scenePackagesRunning = false;
        PlayerMoveSpeed::Release();

        // Control goes back before the aliases change, or the AI's slope pitch could drive the camera (straight up
        // or down when stopping on a slope)
        if (_owned) {
            a_player->SetAIDriven(false);
            _owned = false;
        }
        ApplyIdlePackage();
        ClearPlayerPackages();
        a_player->EvaluatePackage(false, false);
        _loggedPackage.Reset();
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
        if (!IsRunning() || GameClock::SecondsSince(_aiStartTime) < 1.0f) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pkg = player ? player->GetCurrentPackage() : nullptr;
        if (!_loggedPackage.Changed(pkg)) return;
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
        if (!IsRunning() || !puppet || !curPkg) return;
        const auto pkgID = curPkg->GetFormID();
        if (pkgID == _greetAttemptPackage || pkgID == _greetedPackage.load()) return;

        const bool isForceGreetPkg = HasForceGreetProcedure(curPkg);
        if (!isForceGreetPkg && !(HasDialogueProcedure(curPkg) && PackageTargetsActor(curPkg, puppet))) return;

        float greetDist = isForceGreetPkg ? 300.0f : 250.0f;
        float dist = player->GetDistance(puppet, false, false);
        if (dist <= greetDist) {
            auto* topic = GetForceGreetTopic(curPkg);
            SKSE::log::info("Player package {:08X} reached puppet target (dist: {:.1f}, topic: {:08X}) - opening self-dialogue.",
                curPkg->GetFormID(), dist, topic ? topic->GetFormID() : 0);
            // Opening stops idle AI; this keeps a failed attempt from repeating every frame
            _greetAttemptPackage = pkgID;
            if (DialogueManager::GetSingleton()->OpenSelfDialogue(puppet, topic) && isForceGreetPkg) {
                _greetingPackage = pkgID;
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
        // Toggle Always Run switches its speed instead (#49).
        static constexpr std::array kActionEvents = {
            &RE::UserEvents::forward, &RE::UserEvents::back, &RE::UserEvents::strafeLeft, &RE::UserEvents::strafeRight,
            &RE::UserEvents::autoMove, &RE::UserEvents::activate, &RE::UserEvents::leftAttack,
            &RE::UserEvents::rightAttack, &RE::UserEvents::readyWeapon, &RE::UserEvents::jump, &RE::UserEvents::sneak,
            &RE::UserEvents::shout,
        };

        auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents || !InGameplayContext()) return false;

        switch (a_event->GetEventType()) {
            case RE::INPUT_EVENT_TYPE::kButton: {
                const auto* button = static_cast<const RE::ButtonEvent*>(a_event);
                if (!button->IsPressed()) return false;
                const auto name = UserEventName(button);
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

    bool IdleAIManager::IsToggleRunInput(const RE::InputEvent* a_event) {
        if (a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) return false;
        const auto* button = static_cast<const RE::ButtonEvent*>(a_event);
        auto* userEvents = RE::UserEvents::GetSingleton();
        return button->IsDown() && userEvents && InGameplayContext() && UserEventName(button) == userEvents->toggleRun;
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
            if (IsToggleRunInput(event)) {
                Post(kToggleRun);
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

    void IdleAIManager::OnPostLoadGame() {
        Post(kPostLoad);
    }

    std::int32_t IdleAIManager::DesiredAliasID() const {
        // While scene package actions run, the alias is left empty so it doesn't outrank them
        const auto index = Settings::GetSingleton()->GetIdlePackage();
        const auto* quest = Forms::idlePackageQuest;
        if (!IsRunning() || _scenePackagesRunning || !quest || index <= 0 || static_cast<std::size_t>(index) > quest->aliases.size()) {
            return -1;
        }
        return index - 1;
    }

    void IdleAIManager::ApplyIdlePackage(bool a_force) {
        auto* quest = Forms::idlePackageQuest;
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto aliasID = DesiredAliasID();
        if (!quest || !player || (aliasID == _appliedAliasID && !a_force)) return;
        _appliedAliasID = aliasID;

        if (aliasID >= 0 && !IsQuestRunning(quest)) {
            quest->Start();
        }
        for (auto* alias : quest->aliases) {
            if (alias && static_cast<std::int32_t>(alias->aliasID) != aliasID && quest->GetAliasedRef(alias->aliasID)) {
                ClearAlias(quest, alias);
            }
        }
        if (aliasID >= 0) {
            quest->ForceRefIntoAlias(static_cast<std::uint32_t>(aliasID), player);
        }
        player->EvaluatePackage(false, false);
        SKSE::log::info("Idle package alias: {}", aliasID);
    }

    void IdleAIManager::Clear() {
        // On revert, before a save loads, when no frame runs. The player is about to be replaced by the save's, so
        // the engine isn't touched here; HandlePostLoad tidies up what the save holds.
        _requests = 0;
        _running = false;
        PlayerMoveSpeed::Release();
        _owned = false;
        _idleSeconds = 0.0f;
        _scenePackagesRunning = false;
        _loggedPackage.Reset();
        ClearPlayerPackages();
        _appliedAliasID = -1;
        _greetAttemptPackage = 0;
        _greetingPackage = 0;
        _greetedPackage = 0;
        _greetedScene = 0;
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
