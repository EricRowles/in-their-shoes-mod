#pragma once

#include "GameUtil.h"
#include "Singleton.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include "RE/I/IProcedureTreeItem.h"

namespace InTheirShoes {

    // Drives the player with the donor's AI packages after a period without input.
    // The force-greet hooks may run on AI threads; they only set _forceGreetProcedureActive and read _greetedPackage.
    class IdleAIManager : public Singleton<IdleAIManager>,
        public RE::BSTEventSink<RE::InputEvent*> {
    public:
        void Initialize();
        void Update();

        // BSTEventSink<RE::InputEvent*>
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        void StopIdleAI();
        void OnImpersonationChanged(bool a_active);
        // A ForceGreet package that opened the conversation has greeted: its procedure now reports done
        void OnSelfDialogueEnded();

        // The MCM's idle package (Settings::GetIdlePackage): 0 runs the donor's own packages, n puts the player in
        // ITS_PackageManager's alias n - 1 while idle AI runs, whose package outranks the donor's (its quests' too)
        void OnIdlePackageChanged();
        // Where the Travel package goes (Settings::GetTravelDestination)
        void OnTravelDestinationChanged();
        void OnPostLoadGame();

        // ITS_IdlePackage.Apply asks this first, from a Papyrus thread. Whether call a_sequence is the latest sent
        // (an older one arriving late is skipped), and whether it must switch Game.SetPlayerAIDriven to a_aiDriven.
        enum class ApplyCheck : std::int32_t { kSkip = 0, kApply = 1, kApplyAndSetAIDriven = 2 };
        ApplyCheck BeginIdlePackageApply(std::int32_t a_sequence, bool a_aiDriven);

        void ClearPlayerPackages();

        void Clear();

    private:
        friend Singleton<IdleAIManager>;
        IdleAIManager() = default;
        ~IdleAIManager() override = default;

        void InstallHooks();
        void OnUserInput();
        void CheckIdleAI();
        void StartIdleAI();
        void SyncDonorPackagesToPlayer();
        void RetargetAliasPackagesToPuppet();
        void CheckForceGreetReachedPuppet();
        void ReleaseGreetedPackage();
        bool CombatStopsIdleAI(RE::PlayerCharacter* a_player) const;
        void CheckTravelArrival();
        void LogRunningPackage();

        // What ITS_IdlePackage.Apply sets: whether the player is AI-driven, the ITS_PackageManager alias it is in
        // (-1 for none), and the Travel package's destination and its alias
        struct IdlePackageState {
            bool aiDriven{ false };
            std::int32_t aliasID{ -1 };
            RE::TESObjectREFR* destination{ nullptr };
            std::int32_t destinationAliasID{ -1 };
            bool operator==(const IdlePackageState&) const = default;
        };
        IdlePackageState DesiredIdlePackageState() const;
        // Sends the desired state to Papyrus if it differs from the last sent (or always, with a_force)
        void ApplyIdlePackage(bool a_force = false);

        static std::uint64_t Hook_ForceGreet_InitExecState(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState);
        using ForceGreet_InitExecState_t = std::uint64_t(void*, RE::IProcedureTreeItem::ExecStateContext*);
        inline static REL::Relocation<ForceGreet_InitExecState_t> _origForceGreetInitExecState;

        static std::uint64_t Hook_ForceGreet_Execute(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState);
        using ForceGreet_Execute_t = std::uint64_t(void*, RE::IProcedureTreeItem::ExecStateContext*);
        inline static REL::Relocation<ForceGreet_Execute_t> _origForceGreetExecute;

        // The player's alias packages (the donor's swapped aliases) only count while idle AI drives it; see the definition
        static RE::TESPackage* Hook_Player_CheckForCurrentAliasPackage(RE::Actor* a_this);
        using CheckForCurrentAliasPackage_t = RE::TESPackage*(RE::Actor*);
        inline static REL::Relocation<CheckForCurrentAliasPackage_t> _origPlayerCheckForCurrentAliasPackage;

        bool _initialized{ false };
        bool _aiActive{ false };
        // Whether the player is set AI-driven (ApplyIdlePackage). Read by the alias package hook on AI threads.
        std::atomic<bool> _playerAIDriven{ false };
        bool _forceGreetTriggered{ false };
        std::atomic<bool> _forceGreetProcedureActive{ false };
        // The ForceGreet package whose arrival opened the current self-dialogue
        RE::FormID _greetingPackage{ 0 };
        // A ForceGreet package that has had its conversation, and the scene it runs in (0 for none). Its
        // procedure reports done until the scene ends or the player runs another package. The hooks read it.
        std::atomic<RE::FormID> _greetedPackage{ 0 };
        RE::FormID _greetedScene{ 0 };
        PlayerBusy _blockedBy{ PlayerBusy::kNo };
        RE::TESPackage* _loggedPackage{ nullptr };
        bool _runningPackageLogged{ false };
        bool _evaluatedAfterEngage{ false };
        std::chrono::steady_clock::time_point _aiStartTime{};
        std::chrono::steady_clock::time_point _lastPackageSyncTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
        bool _savedUserRunning{ true };

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

        bool _travelArrived{ false };  // The Travel package reached its city, so the player sandboxes there

        // The state last sent to ITS_IdlePackage.Apply, and its sequence number
        IdlePackageState _sentState{};
        std::atomic<std::int32_t> _applySequence{ 0 };
        // What Papyrus last passed to Game.SetPlayerAIDriven; unknown after a load. Papyrus threads read it.
        std::mutex _appliedLock;
        std::optional<bool> _appliedAIDriven;
    };

}
