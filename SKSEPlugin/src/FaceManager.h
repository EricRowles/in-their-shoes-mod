#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include "RE/N/NiColor.h"

namespace InTheirShoes {

    struct FaceSnapshot {
        RE::TESRace* race{ nullptr };
        RE::TESNPC* faceNPC{ nullptr };
        RE::TESObjectARMO* skin{ nullptr };
        RE::TESObjectARMO* farSkin{ nullptr };
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

    // Stateless appearance capture/apply. The Dragonborn's saved appearance lives in
    // ImpersonationManager's Baseline.
    class FaceManager {
    public:
        static FaceManager& GetSingleton() {
            static FaceManager instance;
            return instance;
        }

        bool CaptureSnapshot(RE::Actor* a_actor, FaceSnapshot& a_outSnapshot);

        // a_asPuppet: the puppet gets the Dragonborn's look but no voice type or face template of its own
        bool ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot, bool a_update3D = true, bool a_asPuppet = false);

        // Copies the donor's current appearance onto the target
        bool CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target);

    private:
        FaceManager() = default;
        ~FaceManager() = default;
        FaceManager(const FaceManager&) = delete;
        FaceManager& operator=(const FaceManager&) = delete;

        // Head part arrays we allocated, so we never free one the engine owns
        std::unordered_set<void*> _allocatedHeadPartBuffers;
    };

}
