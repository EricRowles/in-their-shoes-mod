#pragma once

#include "GameUtil.h"
#include "PackageUtil.h"
#include "Singleton.h"
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdint>
#include "RE/I/IProcedureTreeItem.h"

namespace InTheirShoes {

    // Drives the player with the donor's AI packages after a period without input.
    // The force-greet hooks may run on AI threads; they only set _forceGreetProcedureActive.
    class IdleAIManager : public Singleton<IdleAIManager>,
        public RE::BSTEventSink<RE::InputEvent*> {
    public:
        void Initialize();
        void Update();

        // BSTEventSink<RE::InputEvent*>
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        void StartIdleAI();
        void StopIdleAI();
        void OnImpersonationChanged(bool a_active);

        void SetIdleAIEnabled(bool a_enabled) { _enabled = a_enabled; }
        void SetIdleTimeout(float a_seconds) { _idleTimeoutSeconds = a_seconds; }
        // The MCM's idle package: 0 runs the donor's own packages, n puts the player in ITS_PackageManager's
        // alias n - 1 while idle AI runs, whose package outranks the donor's (its quests' too)
        void SetIdlePackage(std::int32_t a_index);
        // Where the Travel package goes: an index into Forms::travelDestinations
        void SetTravelDestination(std::int32_t a_index);
        void OnPostLoadGame();
        bool IsAIActive() const { return _aiActive; }

        void ClearPlayerPackages();

        void Clear() {
            StopIdleAI();
            ClearPlayerPackages();
        }

    private:
        friend Singleton<IdleAIManager>;
        IdleAIManager() = default;
        ~IdleAIManager() override = default;

        void InstallHooks();
        void OnUserInput();
        void CheckIdleAI();
        void SyncDonorPackagesToPlayer();
        bool RetargetPackageToPuppet(RE::TESPackage* a_pkg, RE::Actor* a_player, RE::Actor* a_puppet);
        void RetargetAliasPackagesToPuppet();
        void CheckForceGreetReachedPuppet();
        bool CombatStopsIdleAI(RE::PlayerCharacter* a_player) const;
        void CheckTravelArrival();
        void FillIdlePackageAlias();
        void ReleaseIdlePackageAlias();
        void LogRunningPackage();

        static std::uint64_t Hook_ForceGreet_InitExecState(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState);
        using ForceGreet_InitExecState_t = std::uint64_t(void*, RE::IProcedureTreeItem::ExecStateContext*);
        inline static REL::Relocation<ForceGreet_InitExecState_t> _origForceGreetInitExecState;

        static std::uint64_t Hook_ForceGreet_Execute(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState);
        using ForceGreet_Execute_t = std::uint64_t(void*, RE::IProcedureTreeItem::ExecStateContext*);
        inline static REL::Relocation<ForceGreet_Execute_t> _origForceGreetExecute;

        bool _initialized{ false };
        bool _enabled{ false };
        bool _aiActive{ false };
        bool _forceGreetTriggered{ false };
        std::atomic<bool> _forceGreetProcedureActive{ false };
        float _idleTimeoutSeconds{ 10.0f };
        PlayerBusy _blockedBy{ PlayerBusy::kNo };
        RE::TESPackage* _loggedPackage{ nullptr };
        bool _runningPackageLogged{ false };
        bool _evaluatedAfterEngage{ false };
        std::chrono::steady_clock::time_point _aiStartTime{};
        std::chrono::steady_clock::time_point _lastPackageSyncTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
        bool _savedUserRunning{ true };

        // Package targets and locations redirected from the player to the puppet while autonomous AI is active
        PackageUtil::PackageEdits _packageEdits;

        std::int32_t _idlePackageIndex{ 0 };
        // A scene phase: the scene's form ID and its phase index
        struct ScenePhase {
            RE::FormID scene{ 0 };
            std::uint32_t phase{ 0 };
            bool operator==(const ScenePhase&) const = default;
        };
        // The scene phase the packages were last synced for, so a new phase syncs at once
        ScenePhase _syncedScenePhase{};
        // The player runs scene package actions, so the MCM's idle package alias is released until they end
        bool _scenePackagesRunning{ false };

        std::int32_t _travelDestinationIndex{ 0 };
        bool _travelArrived{ false };  // The Travel package reached its city, so the player sandboxes there
        bool _idlePackageAliasFilled{ false };
    };

}
