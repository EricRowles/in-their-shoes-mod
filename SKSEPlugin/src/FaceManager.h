#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include "RE/N/NiColor.h"

namespace InTheirShoes {

    struct FaceSnapshot {
        RE::TESRace* race{ nullptr };
        RE::TESNPC* faceNPC{ nullptr };
        RE::BGSVoiceType* voiceType{ nullptr };
        RE::BGSColorForm* hairColor{ nullptr };
        RE::BGSTextureSet* faceDetails{ nullptr };
        RE::Color bodyTintColor{};
        float height{ 1.0f };
        float scale{ 1.0f };
        float weight{ 50.0f };
        RE::SEX sex{ RE::SEX::kNone };
        RE::BSFixedString fullName;

        // Face Morphs
        float morphs[RE::TESNPC::FaceData::Morphs::kTotal]{ 0.0f };
        std::int32_t parts[RE::TESNPC::FaceData::Parts::kTotal]{ 0 };

        // Head Parts
        std::vector<RE::BGSHeadPart*> headParts;

        // Tint Layers
        std::vector<RE::TESNPC::Layer> tintLayers;

        // 3D Rendered Hair Tint Color
        RE::NiColor hairTintColor{ 0.0f, 0.0f, 0.0f };
        bool hasHairTintColor{ false };

        // 3D Rendered Skin Tint Color
        RE::NiColor skinTintColor{ 0.0f, 0.0f, 0.0f };
        bool hasSkinTintColor{ false };
    };

    class FaceManager {
    public:
        static FaceManager& GetSingleton() {
            static FaceManager instance;
            return instance;
        }

        // Captures the Dragonborn's baseline appearance if not already saved
        bool SaveBaselineAppearance(RE::Actor* a_player);

        // Clones full visual appearance from donor to target actor (saving target's original appearance if not yet saved)
        bool CloneAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Copies visual appearance from donor to target actor without modifying saved history
        bool CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Restores saved original appearance onto target actor
        bool RevertAppearance(RE::Actor* a_target, bool a_update3D = true);

        // Restores active donor appearance onto target actor directly from the donor actor
        bool RestoreDonorAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Restores saved original Dragonborn appearance onto puppet actor
        bool RestoreOriginalPuppetAppearance(RE::Actor* a_puppet);

        // Snapshot utilities
        bool CaptureSnapshot(RE::Actor* a_actor, FaceSnapshot& a_outSnapshot);
        bool ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot, bool a_update3D = true);

        // State & Serialization
        bool HasSavedAppearance() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _hasSavedAppearance;
        }
        FaceSnapshot GetSavedAppearance() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _savedOriginalAppearance;
        }
        void SetSavedAppearance(const FaceSnapshot& a_snapshot) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _savedOriginalAppearance = a_snapshot;
            _hasSavedAppearance = true;
            RefreshOriginalVoice();
        }
        void ClearSavedAppearance() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _savedOriginalAppearance = FaceSnapshot{};
            _hasSavedAppearance = false;
            RefreshOriginalVoice();
        }

        // The Dragonborn's real name (empty if no appearance has been saved)
        std::string GetSavedAppearanceName() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _hasSavedAppearance ? std::string(_savedOriginalAppearance.fullName.c_str()) : std::string();
        }
        // The Dragonborn's real voice type; lock-free because condition hooks read it constantly
        RE::BGSVoiceType* GetOriginalVoiceType() const { return _origVoiceType.load(std::memory_order_acquire); }

    private:
        FaceManager() = default;
        ~FaceManager() = default;
        FaceManager(const FaceManager&) = delete;
        FaceManager& operator=(const FaceManager&) = delete;

        // Caller must hold _mutex
        void RefreshOriginalVoice() {
            _origVoiceType.store(_hasSavedAppearance ? _savedOriginalAppearance.voiceType : nullptr, std::memory_order_release);
        }

        mutable std::recursive_mutex _mutex;
        std::atomic<RE::BGSVoiceType*> _origVoiceType{ nullptr };
        FaceSnapshot _savedOriginalAppearance;
        bool _hasSavedAppearance{ false };
        std::unordered_set<void*> _allocatedHeadPartBuffers;
    };

}
