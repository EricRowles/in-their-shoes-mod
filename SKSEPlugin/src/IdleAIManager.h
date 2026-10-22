#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <cstdint>

namespace RE {
    class Actor;
    class TESPackage;
    class TESQuest;
}

namespace InTheirShoes {

    struct RetargetedPackageTarget {
        RE::TESPackage* package{ nullptr };
        RE::FormID packageFormID{ 0 };
        std::uint8_t targType{ 0 };
        RE::ObjectRefHandle origHandle;
        RE::TESForm* origObject{ nullptr };
    };

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

        void OnUserInput();
        void CheckIdleAI();
        void StartIdleAI();
        void StopIdleAI();

        void OnImpersonationChanged(bool a_active);

        void SetIdleAIEnabled(bool a_enabled) { _enabled.store(a_enabled, std::memory_order_release); }
        bool IsIdleAIEnabled() const { return _enabled.load(std::memory_order_acquire); }

        void SetIdleTimeout(float a_seconds) { _idleTimeoutSeconds.store(a_seconds, std::memory_order_release); }
        float GetIdleTimeout() const { return _idleTimeoutSeconds.load(std::memory_order_acquire); }

        bool IsAIActive() const { return _aiActive.load(std::memory_order_acquire); }

        // Package management
        void SyncDonorUpdatesToPlayer();
        void ClearPlayerPackages();
        std::vector<RE::TESPackage*> GetAliasPackages(RE::Actor* a_actor) const;

        void Clear() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            StopIdleAI();
            ClearPlayerPackages();
        }

    private:
        IdleAIManager() = default;
        ~IdleAIManager() override = default;
        IdleAIManager(const IdleAIManager&) = delete;
        IdleAIManager& operator=(const IdleAIManager&) = delete;

        mutable std::recursive_mutex _mutex;
        bool _initialized{ false };
        std::atomic<bool> _enabled{ false };
        std::atomic<bool> _aiActive{ false };
        std::atomic<float> _idleTimeoutSeconds{ 10.0f };
        std::chrono::steady_clock::time_point _lastPackageSyncTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
        bool _savedUserRunning{ true };

        std::vector<RetargetedPackageTarget> _retargetedPackages;
    };

}
