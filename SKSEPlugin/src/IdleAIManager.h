#pragma once

#include "GameClock.h"
#include "PlayerBusy.h"
#include <atomic>
#include <cstdint>

namespace InTheirShoes {

    // Drives the player with the donor's AI packages after a period without input.
    //
    // Threading (#45): the state is the main thread's. Only Update() reads and changes it, plus Clear(), which runs
    // during a load when no frame does. Other threads (the input sink, Papyrus natives, menu events) post requests
    // that the next Update() handles. The force-greet and alias package hooks run on AI threads and read only atomics.
    //
    // Ownership: idle AI only turns AI-driven off if it turned it on itself. While something else (a scene, the
    // engine walking the player to furniture, another mod) has the player AI-driven, idle AI leaves it alone.
    class IdleAIManager : public REX::Singleton<IdleAIManager>,
        public RE::BSTEventSink<RE::InputEvent*> {
    public:
        void Initialize();
        // Every player update, on the main thread, after GameClock::Tick
        void Update();

        // BSTEventSink<RE::InputEvent*>: any thread
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        // Requests from any thread, handled in the next Update()
        void OnImpersonationChanged();
        // A ForceGreet package that opened the conversation has greeted: its procedure now reports done
        void OnSelfDialogueEnded();
        // The MCM's idle package (Settings::GetIdlePackage): 0 runs the donor's own packages, n puts the player in
        // ITS_PackageManager's alias n - 1 while idle AI runs, whose package outranks the donor's (its quests' too)
        void OnIdlePackageChanged();
        void OnPostLoadGame();

        // Main thread only: self-dialogue is opening, so idle AI stops now rather than next frame
        void StopForSelfDialogue();

        // Whether idle AI drives the player. Any thread (condition hooks read it).
        bool IsDrivingPlayer() const { return _running.load(); }

        void Clear();

    private:
        friend REX::Singleton<IdleAIManager>;
        IdleAIManager() = default;
        ~IdleAIManager() override = default;

        enum class StopReason {
            kInput,         // The player pressed a key that acts in the world
            kCombat,
            kMenu,          // A menu idle AI can't run through (dialogue, loading, ...)
            kSelfDialogue,
            kInactive,      // Idle AI turned off, or no impersonation
            kSessionChanged, // Impersonation began, ended or moved to another donor
            kLostControl,   // Something else turned AI-driven off
        };
        static std::string_view ToString(StopReason a_reason);

        // Requests other threads post (_requests bits)
        enum Request : std::uint32_t {
            kActionInput = 1 << 0,
            kSessionChanged = 1 << 1,
            kPackageChanged = 1 << 2,
            kPostLoad = 1 << 3,
            kSelfDialogueEnded = 1 << 4,
            kToggleRun = 1 << 5,
        };
        void Post(Request a_request) { _requests.fetch_or(a_request, std::memory_order_acq_rel); }
        void HandleRequests(RE::PlayerCharacter* a_player);
        void HandlePostLoad(RE::PlayerCharacter* a_player);
        void HandleSelfDialogueEnded();

        bool IsRunning() const { return _running.load(); }
        void InstallHooks();
        // Whether a_event asks the player to act in the world (move, attack, activate, ...); see the definition
        static bool IsActionInput(const RE::InputEvent* a_event);
        // Whether a_event is a press of Toggle Always Run in gameplay
        static bool IsToggleRunInput(const RE::InputEvent* a_event);

        void Engage(RE::PlayerCharacter* a_player);
        void Disengage(RE::PlayerCharacter* a_player, StopReason a_reason);
        void UpdateIdleTime(RE::PlayerCharacter* a_player);
        void UpdateRunning(RE::PlayerCharacter* a_player);

        void SyncDonorPackagesToPlayer();
        void ClearPlayerPackages();
        void RetargetAliasPackagesToPuppet();
        void CheckForceGreetReachedPuppet();
        void ReleaseGreetedPackage();
        void LogRunningPackage();

        // The ITS_PackageManager alias the player should be in (-1 for none)
        std::int32_t DesiredAliasID() const;
        // Puts the player in the desired alias only, if that changed (or always, with a_force)
        void ApplyIdlePackage(bool a_force = false);

        static std::uint64_t Hook_ForceGreet_InitExecState(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState);
        using ForceGreet_InitExecState_t = std::uint64_t(void*, RE::IProcedureTreeItem::ExecStateContext*);
        inline static REL::Relocation<ForceGreet_InitExecState_t> _origForceGreetInitExecState;

        static std::uint64_t Hook_ForceGreet_Execute(void* a_this, RE::IProcedureTreeItem::ExecStateContext* a_execState);
        using ForceGreet_Execute_t = std::uint64_t(void*, RE::IProcedureTreeItem::ExecStateContext*);
        inline static REL::Relocation<ForceGreet_Execute_t> _origForceGreetExecute;

        // The player's alias packages (the donor's swapped aliases) only count while idle AI runs; see the definition
        static RE::TESPackage* Hook_Player_CheckForCurrentAliasPackage(RE::Actor* a_this);
        using CheckForCurrentAliasPackage_t = RE::TESPackage*(RE::Actor*);
        inline static REL::Relocation<CheckForCurrentAliasPackage_t> _origPlayerCheckForCurrentAliasPackage;

        bool _initialized{ false };

        // --- Requests (any thread) ---
        std::atomic<std::uint32_t> _requests{ 0 };

        // --- Read by hooks on AI threads ---
        // Whether idle AI drives the player. Only the main thread changes it.
        std::atomic<bool> _running{ false };
        std::atomic<bool> _forceGreetProcedureActive{ false };
        // A ForceGreet package that has had its conversation, and the scene it runs in (0 for none). Its
        // procedure reports done until the scene ends or the player runs another package. The hooks read it.
        std::atomic<RE::FormID> _greetedPackage{ 0 };

        // --- Main thread ---
        // Idle AI turned AI-driven on (PlayerCharacter::SetAIDriven) and hasn't turned it off
        bool _owned{ false };
        // Game time the player has been eligible for idle AI without acting
        float _idleSeconds{ 0.0f };
        LogOnChange<PlayerBusy> _blockedBy;

        bool _forceGreetTriggered{ false };
        // The ForceGreet package whose arrival opened the current self-dialogue
        RE::FormID _greetingPackage{ 0 };
        RE::FormID _greetedScene{ 0 };
        LogOnChange<RE::TESPackage*> _loggedPackage;
        GameClock::TimePoint _aiStartTime{};
        GameClock::TimePoint _lastPackageSyncTime{};

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

        // The ITS_PackageManager alias the player was last put in (-1 for none)
        std::int32_t _appliedAliasID{ -1 };
    };

}
