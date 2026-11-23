#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace InTheirShoes {

    // The MCM's settings. The plugin owns them: they're saved in the co-save, so they're right as soon as a save
    // loads, and ITS_MCMScript reads and sets them by name through ITS_NG. Readable from any thread (the voice and
    // native redirect hooks read them).
    class Settings : public REX::Singleton<Settings> {
    public:
        enum class Id : std::uint32_t {
            kIdleAIEnabled,
            kIdleTimeout,           // Seconds of game time
            kIdlePackage,           // 0 runs the donor's own packages; n puts the player in ITS_PackageManager's alias n - 1
            kNPCVoiceEnabled,
            kIdleChatterInterval,   // Seconds of game time
            kNativeRedirectEnabled,
            kPuppetFollows,
            kTotal
        };
        static constexpr auto kCount = static_cast<std::size_t>(Id::kTotal);

        struct Info {
            std::string_view name;  // As ITS_MCMScript names it
            float defaultValue;
            float min;
            float max;
            bool whole;  // A toggle or an index, stored as a whole number
        };
        // In Id's order, which is also the co-save's
        static constexpr std::array<Info, kCount> kInfo{ {
            { "IdleAIEnabled", 1.0f, 0.0f, 1.0f, true },
            { "IdleAITimeout", 10.0f, 1.0f, 120.0f, false },
            { "IdleAIPackage", 0.0f, 0.0f, 2.0f, true },  // ITS_MCMScript.GetIdlePackageNames
            { "NPCVoiceEnabled", 1.0f, 0.0f, 1.0f, true },
            { "IdleChatterTime", 60.0f, 10.0f, 600.0f, false },
            { "NativeRedirectEnabled", 1.0f, 0.0f, 1.0f, true },
            { "PuppetFollowsPlayer", 1.0f, 0.0f, 1.0f, true },
        } };

        // Case-insensitive, as Papyrus strings are
        static std::optional<Id> Find(std::string_view a_name) {
            for (std::size_t i = 0; i < kCount; ++i) {
                if (_strnicmp(kInfo[i].name.data(), a_name.data(), a_name.size()) == 0 && kInfo[i].name.size() == a_name.size()) {
                    return static_cast<Id>(i);
                }
            }
            return std::nullopt;
        }

        float Get(Id a_id) const { return _values[Index(a_id)].load(std::memory_order_relaxed); }
        // Clamped to the setting's range
        void Set(Id a_id, float a_value) {
            const auto& info = kInfo[Index(a_id)];
            a_value = std::clamp(info.whole ? std::round(a_value) : a_value, info.min, info.max);
            _values[Index(a_id)].store(a_value, std::memory_order_relaxed);
        }

        bool IsIdleAIEnabled() const { return Get(Id::kIdleAIEnabled) != 0.0f; }
        float GetIdleTimeout() const { return Get(Id::kIdleTimeout); }
        std::int32_t GetIdlePackage() const { return static_cast<std::int32_t>(Get(Id::kIdlePackage)); }
        bool IsNPCVoiceEnabled() const { return Get(Id::kNPCVoiceEnabled) != 0.0f; }
        float GetIdleChatterInterval() const { return Get(Id::kIdleChatterInterval); }
        bool IsNativeRedirectEnabled() const { return Get(Id::kNativeRedirectEnabled) != 0.0f; }
        bool DoesPuppetFollowPlayer() const { return Get(Id::kPuppetFollows) != 0.0f; }

        // Back to the defaults, before a save loads or a new game starts
        void Reset() {
            for (std::size_t i = 0; i < kCount; ++i) {
                _values[i].store(kInfo[i].defaultValue, std::memory_order_relaxed);
            }
        }

    private:
        friend REX::Singleton<Settings>;
        Settings() { Reset(); }
        ~Settings() = default;

        static constexpr std::size_t Index(Id a_id) { return static_cast<std::size_t>(a_id); }

        std::array<std::atomic<float>, kCount> _values;
    };

}
