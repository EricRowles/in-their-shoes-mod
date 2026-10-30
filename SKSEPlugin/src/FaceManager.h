#pragma once

#include "Singleton.h"
#include <array>
#include <optional>
#include <string>
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
        std::array<float, RE::TESNPC::FaceData::Morphs::kTotal> morphs{};
        std::array<std::int32_t, RE::TESNPC::FaceData::Parts::kTotal> parts{};

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

    // Appearance capture/apply. The Dragonborn's saved appearance lives in ImpersonationManager's Baseline.
    class FaceManager : public Singleton<FaceManager> {
    public:
        // Keeps RaceMenu (the RaceSex Menu) from opening while impersonating
        void InstallHook();

        // Empty if the actor has no NPC base record
        std::optional<FaceSnapshot> CaptureSnapshot(RE::Actor* a_actor);

        // a_asPuppet: the puppet gets the Dragonborn's look but no voice type or face template of its own
        bool ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot, bool a_update3D = true, bool a_asPuppet = false);

        // Copies the donor's current appearance onto the target
        bool CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Puts back only the snapshot's skin and hair colors, on the actor's current 3D
        void ReapplyTints(RE::Actor* a_actor, const FaceSnapshot& a_snapshot);

    private:
        friend Singleton<FaceManager>;
        FaceManager() = default;
        ~FaceManager() = default;

        static RE::UI_MESSAGE_RESULTS Hook_RaceSexMenu_ProcessMessage(RE::RaceSexMenu* a_this, RE::UIMessage& a_message);
        inline static REL::Relocation<decltype(Hook_RaceSexMenu_ProcessMessage)> _origRaceSexMenuProcessMessage;
    };

}
