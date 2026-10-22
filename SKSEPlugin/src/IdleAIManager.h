#pragma once

#include <chrono>
#include <vector>
#include <cstdint>

namespace InTheirShoes {

    // A swapped-alias package whose target was redirected from the player to the puppet
    struct RetargetedPackageTarget {
        RE::TESPackage* package{ nullptr };
        std::uint8_t targType{ 0 };
        RE::ObjectRefHandle origHandle;
        RE::TESForm* origObject{ nullptr };
    };

    // Drives the player with the donor's AI packages after a period without input.
    // Runs entirely on the main thread (player update, input events, Papyrus natives).
    class IdleAIManager : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static IdleAIManager& GetSingleton() {
            static IdleAIManager instance;
            return instance;
        }

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
        IdleAIManager() = default;
        ~IdleAIManager() override = default;
        IdleAIManager(const IdleAIManager&) = delete;
        IdleAIManager& operator=(const IdleAIManager&) = delete;

        void OnUserInput();
        void CheckIdleAI();
        void SyncDonorPackagesToPlayer();
        void RetargetAliasPackagesToPuppet();
        void CheckForceGreetReachedPuppet();

        bool _initialized{ false };
        bool _enabled{ false };
        bool _aiActive{ false };
        bool _forceGreetTriggered{ false };
        float _idleTimeoutSeconds{ 10.0f };
        std::chrono::steady_clock::time_point _lastPackageSyncTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
        bool _savedUserRunning{ true };

        std::vector<RetargetedPackageTarget> _retargetedPackages;
    };

}
