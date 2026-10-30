#pragma once

#include "GameUtil.h"
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

        // A package target redirected from the player to the puppet while autonomous AI is active
        struct RetargetedPackageTarget {
            RE::PackageTarget* target{ nullptr };
            std::int8_t origType{ 0 };
            RE::ObjectRefHandle origHandle;
            RE::TESForm* origObject{ nullptr };
        };

        // A package location redirected from the player to the puppet while autonomous AI is active
        struct RetargetedPackageLocation {
            RE::PackageLocation* location{ nullptr };
            std::uint8_t origType{ 0 };
            RE::ObjectRefHandle origHandle;
            RE::TESForm* origObject{ nullptr };
            std::uint32_t origRad{ 0 };
        };

        std::vector<RetargetedPackageTarget> _retargetedTargets;
        std::vector<RetargetedPackageLocation> _retargetedLocations;
    };

}
