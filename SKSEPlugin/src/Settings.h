#pragma once

#include "Singleton.h"
#include <atomic>
#include <cstdint>

namespace InTheirShoes {

    // The MCM's settings. ITS_MCMScript sets them through ITS_NG; they are saved in the co-save, so they are
    // right as soon as a save loads. Defaults match the MCM's. Readable from any thread (the voice and
    // native redirect hooks read them).
    class Settings : public Singleton<Settings> {
    public:
        // Idle movement
        bool IsIdleAIEnabled() const { return _idleAIEnabled.load(std::memory_order_relaxed); }
        float GetIdleTimeout() const { return _idleTimeoutSeconds.load(std::memory_order_relaxed); }
        // 0 runs the donor's own packages; n puts the player in ITS_PackageManager's alias n - 1
        std::int32_t GetIdlePackage() const { return _idlePackage.load(std::memory_order_relaxed); }
        // An index into Forms::travelDestinations
        std::int32_t GetTravelDestination() const { return _travelDestination.load(std::memory_order_relaxed); }

        // NPC voice
        bool IsNPCVoiceEnabled() const { return _npcVoiceEnabled.load(std::memory_order_relaxed); }
        float GetIdleChatterInterval() const { return _idleChatterSeconds.load(std::memory_order_relaxed); }

        bool IsNativeRedirectEnabled() const { return _nativeRedirectEnabled.load(std::memory_order_relaxed); }
        bool DoesPuppetFollowPlayer() const { return _puppetFollows.load(std::memory_order_relaxed); }

        void SetIdleAIEnabled(bool a_enabled) { _idleAIEnabled.store(a_enabled, std::memory_order_relaxed); }
        void SetIdleTimeout(float a_seconds) { _idleTimeoutSeconds.store(a_seconds, std::memory_order_relaxed); }
        void SetIdlePackage(std::int32_t a_index) { _idlePackage.store(a_index, std::memory_order_relaxed); }
        void SetTravelDestination(std::int32_t a_index) { _travelDestination.store(a_index, std::memory_order_relaxed); }
        void SetNPCVoiceEnabled(bool a_enabled) { _npcVoiceEnabled.store(a_enabled, std::memory_order_relaxed); }
        // Below the MCM slider's minimum (from an old save) falls back to the default
        void SetIdleChatterInterval(float a_seconds) {
            _idleChatterSeconds.store(a_seconds < kMinIdleChatterSeconds ? kDefaultIdleChatterSeconds : a_seconds, std::memory_order_relaxed);
        }
        void SetNativeRedirectEnabled(bool a_enabled) { _nativeRedirectEnabled.store(a_enabled, std::memory_order_relaxed); }
        void SetPuppetFollowsPlayer(bool a_follows) { _puppetFollows.store(a_follows, std::memory_order_relaxed); }

        // Back to the defaults, before a save loads or a new game starts
        void Reset() {
            SetIdleAIEnabled(true);
            SetIdleTimeout(10.0f);
            SetIdlePackage(0);
            SetTravelDestination(0);
            SetNPCVoiceEnabled(true);
            SetIdleChatterInterval(kDefaultIdleChatterSeconds);
            SetNativeRedirectEnabled(true);
            SetPuppetFollowsPlayer(true);
        }

    private:
        friend Singleton<Settings>;
        Settings() = default;
        ~Settings() = default;

        static constexpr float kMinIdleChatterSeconds = 10.0f;
        static constexpr float kDefaultIdleChatterSeconds = 60.0f;

        std::atomic<bool> _idleAIEnabled{ true };
        std::atomic<float> _idleTimeoutSeconds{ 10.0f };
        std::atomic<std::int32_t> _idlePackage{ 0 };
        std::atomic<std::int32_t> _travelDestination{ 0 };
        std::atomic<bool> _npcVoiceEnabled{ true };
        std::atomic<float> _idleChatterSeconds{ kDefaultIdleChatterSeconds };
        std::atomic<bool> _nativeRedirectEnabled{ true };
        std::atomic<bool> _puppetFollows{ true };
    };

}
