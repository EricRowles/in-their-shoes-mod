#include "PCH.h"
#include "FaceManager.h"
#include "Forms.h"
#include "ActorHooks.h"
#include "Session.h"

#include <algorithm>
#include "RE/B/BSFaceGenNiNode.h"
#include "RE/B/BSVisit.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderMaterialHairTint.h"
#include "RE/B/BSLightingShaderMaterialFacegenTint.h"
#include "RE/E/ExtraScale.h"
#include "RE/E/ExtraTextDisplayData.h"

namespace InTheirShoes {

    namespace {
        RE::NiColor ToNiColor(const RE::Color& a_color) {
            return { a_color.red / 255.0f, a_color.green / 255.0f, a_color.blue / 255.0f };
        }

        std::uint8_t ToByte(float a_channel) {
            return static_cast<std::uint8_t>(std::clamp(a_channel * 255.0f, 0.0f, 255.0f));
        }

        // Sets a_out's red, green and blue (its alpha is left alone)
        void SetRGB(RE::Color& a_out, const RE::Color& a_from) {
            a_out.red = a_from.red;
            a_out.green = a_from.green;
            a_out.blue = a_from.blue;
        }

        void SetRGB(RE::Color& a_out, const RE::NiColor& a_from) {
            a_out.red = ToByte(a_from.red);
            a_out.green = ToByte(a_from.green);
            a_out.blue = ToByte(a_from.blue);
        }

        // A body tint of 0,0,0 means none was set
        bool IsBlack(const RE::Color& a_color) {
            return a_color.red == 0 && a_color.green == 0 && a_color.blue == 0;
        }

        // The color of the actor's skin tone tint layer: the layer whose index is the race's skin tone mask
        std::optional<RE::Color> FindRaceSkinTone(RE::TESRace* a_race, RE::SEX a_sex, const std::vector<RE::TESNPC::Layer>& a_layers) {
            auto sexIdx = (a_sex == RE::SEX::kFemale) ? 1 : 0;
            if (!a_race || !a_race->faceRelatedData[sexIdx] || !a_race->faceRelatedData[sexIdx]->tintMasks) {
                return std::nullopt;
            }
            for (const auto* asset : *a_race->faceRelatedData[sexIdx]->tintMasks) {
                if (asset && asset->texture.skinTone == RE::TESRace::FaceRelatedData::TintAsset::TintLayer::SkinTone::kSkinTone) {
                    for (const auto& layer : a_layers) {
                        if (layer.tintIndex == asset->texture.index) {
                            return layer.tintColor;
                        }
                    }
                    break;
                }
            }
            return std::nullopt;
        }

        // Calls a_fn(material) for each geometry under a_root whose lighting material is a Material
        template <class Material, class Fn>
        void ForEachMaterial(RE::NiAVObject* a_root, Fn&& a_fn) {
            if (!a_root) return;
            RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geom) {
                auto* shaderProp = a_geom ? a_geom->lightingShaderProp_cast() : nullptr;
                if (auto* material = shaderProp ? skyrim_cast<Material*>(shaderProp->material) : nullptr) {
                    a_fn(material);
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        template <class Material>
        Material* FindMaterial(RE::NiAVObject* a_root) {
            Material* found = nullptr;
            if (!a_root) return found;
            RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geom) {
                auto* shaderProp = a_geom ? a_geom->lightingShaderProp_cast() : nullptr;
                found = shaderProp ? skyrim_cast<Material*>(shaderProp->material) : nullptr;
                return found ? RE::BSVisit::BSVisitControl::kStop : RE::BSVisit::BSVisitControl::kContinue;
            });
            return found;
        }

        // Sets the rendered hair and skin tints, which the record's colors don't reach once the 3D is built
        void Apply3DShaderTints(RE::Actor* a_actor, const std::optional<RE::NiColor>& a_hairTint, const std::optional<RE::NiColor>& a_skinTint) {
            if (!a_actor) return;
            if (a_hairTint) {
                ForEachMaterial<RE::BSLightingShaderMaterialHairTint>(a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair),
                    [&](auto* a_material) { a_material->tintColor = *a_hairTint; });
            }
            if (a_skinTint) {
                // The face, then every skin geometry of the body
                const auto setSkin = [&](auto* a_material) { a_material->tintColor = *a_skinTint; };
                ForEachMaterial<RE::BSLightingShaderMaterialFacegenTint>(a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace), setSkin);
                ForEachMaterial<RE::BSLightingShaderMaterialFacegenTint>(a_actor->Get3D(), setSkin);
            }
        }

        // The player's face and body are tinted from PlayerCharacter's own skin tone tint mask as well as the
        // base record's bodyTintColor, so the two have to agree. Sets the mask in both of the player's tint lists
        // (base and overlay), as SKSE's Game.SetNthTintMaskColor does. CommonLib's GetTintList() returns only
        // one of them.
        void SetPlayerSkinTone(RE::PlayerCharacter* a_player, const RE::Color& a_color) {
            if (!a_player || REL::Module::IsVR()) return;
            auto& data = a_player->GetPlayerRuntimeData();
            for (auto* list : { &data.tintMasks, data.overlayTintMasks }) {
                if (!list) continue;
                for (auto* mask : *list) {
                    if (mask && mask->type == RE::TintMask::Type::kSkinTone) {
                        SetRGB(mask->color, a_color);
                    }
                }
            }
        }

        // The skin tint to apply: the rendered one if captured, else the body tint, else the race's skin tone layer
        std::optional<RE::NiColor> ResolveSkinTint(const FaceSnapshot& a_snapshot) {
            if (a_snapshot.hasSkinTintColor) {
                return a_snapshot.skinTintColor;
            }
            if (!IsBlack(a_snapshot.bodyTintColor)) {
                return ToNiColor(a_snapshot.bodyTintColor);
            }
            if (auto tone = FindRaceSkinTone(a_snapshot.race, a_snapshot.sex, a_snapshot.tintLayers)) {
                return ToNiColor(*tone);
            }
            return std::nullopt;
        }

        // Sets the record's skin and hair colors (and the player's skin tone mask, which must match them)
        void ApplyTintColors(RE::Actor* a_actor, RE::TESNPC* a_npc, const FaceSnapshot& a_snapshot, const std::optional<RE::NiColor>& a_skinTint) {
            a_npc->bodyTintColor = a_snapshot.bodyTintColor;
            if (a_skinTint) {
                SetRGB(a_npc->bodyTintColor, *a_skinTint);
            }
            a_npc->SetHairColor(a_snapshot.hairColor);
            if (a_actor->IsPlayerRef()) {
                SetPlayerSkinTone(RE::PlayerCharacter::GetSingleton(), a_npc->bodyTintColor);
            }
        }

        void FreeTintLayers(RE::TESNPC* a_npc) {
            if (!a_npc->tintLayers) return;
            for (auto* layer : *a_npc->tintLayers) {
                if (layer) {
                    RE::free(layer);
                }
            }
            a_npc->tintLayers->clear();
        }
    }

    void FaceManager::InstallHook() {
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_RaceSexMenu[0] };
        _origRaceSexMenuProcessMessage = vtbl.write_vfunc(0x4, &Hook_RaceSexMenu_ProcessMessage);
        SKSE::log::info("Hooked RaceSexMenu::ProcessMessage (0x4)");
    }

    RE::UI_MESSAGE_RESULTS FaceManager::Hook_RaceSexMenu_ProcessMessage(RE::RaceSexMenu* a_this, RE::UIMessage& a_message) {
        // RaceSexMenu's show handler builds its tint sliders from the current race's tint assets and reads the
        // player's own tint mask at each asset's index (ID 52412, called from ProcessMessage 52345 via 52409).
        // PlayerCharacter's tint masks were built for the Dragonborn's race and aren't swapped with the record,
        // so with a donor of another race the index runs past them and the menu crashes on a null mask.
        // Returning kIgnore to kShow is the UI's own refusal path (UI message processing, ID 82082): the menu
        // is dropped before it joins the menu stack, so no open/close event is sent and RaceMenu's script
        // doesn't save the donor's tints as the Dragonborn's.
        if (a_message.type == RE::UI_MESSAGE_TYPE::kShow && Session::GetSingleton().IsActive() && !a_this->AlwaysOpen()) {
            SKSE::log::info("Refused to open RaceMenu while impersonating");
            RE::SendHUDMessage::ShowHUDMessage("You can't change your appearance while impersonating.");
            return RE::UI_MESSAGE_RESULTS::kIgnore;
        }
        return _origRaceSexMenuProcessMessage(a_this, a_message);
    }

    std::optional<FaceSnapshot> FaceManager::CaptureSnapshot(RE::Actor* a_actor) {
        if (!a_actor) return std::nullopt;

        auto* base = a_actor->GetActorBase();
        if (!base) return std::nullopt;

        auto* npc = skyrim_cast<RE::TESNPC*>(base);
        if (!npc) return std::nullopt;

        FaceSnapshot snapshot;

        snapshot.race = npc->GetRace();
        snapshot.skin = npc->skin;
        snapshot.farSkin = npc->farSkin;
        if (npc->GetFormID() == Forms::kPlayerBaseID) {
            snapshot.faceNPC = nullptr;
        } else {
            snapshot.faceNPC = (npc->faceNPC && npc->faceNPC != npc) ? npc->faceNPC : npc;
        }
        const char* dispName = a_actor->GetDisplayFullName();
        if (dispName && dispName[0] != '\0') {
            snapshot.fullName = dispName;
        } else {
            snapshot.fullName = npc->GetFullName();
        }
        snapshot.height = npc->height;

        auto* extraScale = a_actor->extraList.GetByType<RE::ExtraScale>();
        snapshot.scale = extraScale ? extraScale->scale : 1.0f;

        snapshot.weight = npc->weight;
        snapshot.sex = npc->GetSex();
        snapshot.bodyTintColor = npc->bodyTintColor;

        // Tint layers
        if (npc->tintLayers) {
            for (auto* layer : *npc->tintLayers) {
                if (layer) {
                    snapshot.tintLayers.push_back(*layer);
                }
            }
        }

        // Resolve skin tone from race tint assets if bodyTintColor is uninitialized (common on vanilla NPCs)
        if (IsBlack(snapshot.bodyTintColor)) {
            if (auto tone = FindRaceSkinTone(a_actor->GetRace(), snapshot.sex, snapshot.tintLayers)) {
                SetRGB(snapshot.bodyTintColor, *tone);
                snapshot.skinTintColor = ToNiColor(*tone);
                snapshot.hasSkinTintColor = true;
            }
        }

        snapshot.voiceType = ActorHooks::GetNPCVoiceType(npc);

        if (npc->headRelatedData) {
            snapshot.hairColor = npc->headRelatedData->hairColor;
            snapshot.faceDetails = npc->headRelatedData->faceDetails;
        } else {
            snapshot.hairColor = nullptr;
            snapshot.faceDetails = nullptr;
        }

        // The rendered hair and skin tints, read from the 3D (the skin tint overrides the race-derived one)
        if (auto* hairMat = FindMaterial<RE::BSLightingShaderMaterialHairTint>(a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair))) {
            snapshot.hairTintColor = hairMat->tintColor;
            snapshot.hasHairTintColor = true;
        }
        if (auto* faceMat = FindMaterial<RE::BSLightingShaderMaterialFacegenTint>(a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace))) {
            snapshot.skinTintColor = faceMat->tintColor;
            snapshot.hasSkinTintColor = true;
        }

        // Face morphs & parts
        if (npc->faceData) {
            std::ranges::copy(npc->faceData->morphs, snapshot.morphs.begin());
            std::ranges::copy(npc->faceData->parts, snapshot.parts.begin());
        }

        // Comprehensive Head parts capture (all types 0..6)
        for (int t = 0; t <= static_cast<int>(RE::BGSHeadPart::HeadPartType::kEyebrows); ++t) {
            auto type = static_cast<RE::BGSHeadPart::HeadPartType>(t);
            auto* part = npc->GetCurrentHeadPartByType(type);
            if (!part) part = npc->GetHeadPartByType(type);
            if (part) {
                snapshot.headParts.push_back(part);
            }
        }
        if (npc->headParts && npc->numHeadParts > 0) {
            for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                if (npc->headParts[i]) {
                    bool found = false;
                    for (auto* existing : snapshot.headParts) {
                        if (existing == npc->headParts[i]) { found = true; break; }
                    }
                    if (!found) snapshot.headParts.push_back(npc->headParts[i]);
                }
            }
        }

        return snapshot;
    }

    bool FaceManager::ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot, bool a_update3D, bool a_asPuppet) {
        if (!a_targetActor) return false;

        auto* base = a_targetActor->GetActorBase();
        if (!base) return false;

        auto* targetNPC = skyrim_cast<RE::TESNPC*>(base);
        if (!targetNPC) return false;

        // 1. Race, Skin & Face NPC
        if (a_snapshot.race) {
            targetNPC->race = a_snapshot.race;
            targetNPC->originalRace = a_snapshot.race;
            a_targetActor->GetActorRuntimeData().race = a_snapshot.race;
        }
        targetNPC->skin = a_snapshot.skin;
        targetNPC->farSkin = a_snapshot.farSkin;

        if (!a_asPuppet && a_snapshot.faceNPC && a_snapshot.faceNPC != targetNPC && a_snapshot.faceNPC->GetFormID() != Forms::kPlayerBaseID) {
            targetNPC->faceNPC = a_snapshot.faceNPC;
        } else {
            targetNPC->faceNPC = nullptr;
        }

        if (!a_snapshot.fullName.empty()) {
            targetNPC->fullName = a_snapshot.fullName;
            auto* textData = a_targetActor->extraList.GetByType<RE::ExtraTextDisplayData>();
            if (!textData) {
                textData = new RE::ExtraTextDisplayData(a_snapshot.fullName.c_str());
                a_targetActor->extraList.Add(textData);
            } else {
                textData->SetName(a_snapshot.fullName.c_str());
            }
        }

        // 2. Sex & Voice Type
        if (a_snapshot.sex == RE::SEX::kFemale) {
            targetNPC->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
        } else {
            targetNPC->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kFemale);
        }

        if (a_asPuppet) {
            targetNPC->voiceType = nullptr;
        } else {
            targetNPC->voiceType = a_snapshot.voiceType;
        }

        // 3. Body scales & tint
        targetNPC->height = a_snapshot.height;
        if (a_update3D) {
            a_targetActor->SetScale(a_snapshot.scale);
        }
        targetNPC->weight = a_snapshot.weight;

        // 4. Face details (the skin and hair colors are set after the tint layers)
        targetNPC->SetFaceTexture(a_snapshot.faceDetails);

        // 5. Face morphs & parts
        if (!targetNPC->faceData) {
            targetNPC->faceData = RE::calloc<RE::TESNPC::FaceData>(1);
            if (targetNPC->faceData) {
                std::memset(targetNPC->faceData, 0, sizeof(RE::TESNPC::FaceData));
            }
        }
        if (targetNPC->faceData) {
            std::ranges::copy(a_snapshot.morphs, targetNPC->faceData->morphs);
            std::ranges::copy(a_snapshot.parts, targetNPC->faceData->parts);
        }

        // 6. Headparts Replacement
        if (targetNPC->headParts) {
            RE::free(targetNPC->headParts);
            targetNPC->headParts = nullptr;
            targetNPC->numHeadParts = 0;
        }
        if (!a_snapshot.headParts.empty()) {
            auto count = static_cast<std::int8_t>(a_snapshot.headParts.size());
            if (count > 0) {
                auto* newParts = RE::calloc<RE::BGSHeadPart*>(static_cast<std::size_t>(count));
                if (newParts) {
                    for (std::int8_t i = 0; i < count; ++i) {
                        newParts[i] = a_snapshot.headParts[i];
                    }
                    targetNPC->headParts = newParts;
                    targetNPC->numHeadParts = count;
                }
            }
        }

        // 6b. Tint Layers Replication
        if (!a_snapshot.tintLayers.empty() && !targetNPC->tintLayers) {
            if (auto* raw = RE::calloc<RE::BSTArray<RE::TESNPC::Layer*>>(1)) {
                targetNPC->tintLayers = new (raw) RE::BSTArray<RE::TESNPC::Layer*>();
            }
        }
        FreeTintLayers(targetNPC);
        if (targetNPC->tintLayers) {
            for (const auto& layer : a_snapshot.tintLayers) {
                if (auto* newLayer = RE::calloc<RE::TESNPC::Layer>(1)) {
                    *newLayer = layer;
                    targetNPC->tintLayers->push_back(newLayer);
                }
            }
        }

        // 7. Skin & hair colors
        const auto skinTint = ResolveSkinTint(a_snapshot);
        ApplyTintColors(a_targetActor, targetNPC, a_snapshot, skinTint);

        if (a_update3D) {
            a_targetActor->DoReset3D(true);
            a_targetActor->Update3DModel();
            a_targetActor->UpdateHairColor();
            a_targetActor->UpdateSkinColor();

            Apply3DShaderTints(a_targetActor, a_snapshot.hasHairTintColor ? std::optional{ a_snapshot.hairTintColor } : std::nullopt, skinTint);
        }

        SKSE::log::info("Applied appearance snapshot (Race: {:08X}, Sex: {}, Voice: {:08X}, Tints: {}, Parts: {}) to actor {:08X}",
            a_snapshot.race ? a_snapshot.race->GetFormID() : 0u,
            a_snapshot.sex == RE::SEX::kFemale ? "Female" : "Male",
            targetNPC->voiceType ? targetNPC->voiceType->GetFormID() : 0u,
            a_snapshot.tintLayers.size(),
            a_snapshot.headParts.size(),
            a_targetActor->GetFormID()
        );

        return true;
    }

    bool FaceManager::CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target) {
        if (!a_donor || !a_target) {
            SKSE::log::warn("CopyAppearance called with null actor pointers.");
            return false;
        }

        auto donorSnapshot = CaptureSnapshot(a_donor);
        if (!donorSnapshot) {
            SKSE::log::error("Failed to capture appearance of donor actor {:08X}.",
                a_donor->GetFormID());
            return false;
        }

        return ApplySnapshot(a_target, *donorSnapshot);
    }

    void FaceManager::ReapplyTints(RE::Actor* a_actor, const FaceSnapshot& a_snapshot) {
        auto* npc = a_actor ? skyrim_cast<RE::TESNPC*>(a_actor->GetActorBase()) : nullptr;
        if (!npc) return;

        const auto skinTint = ResolveSkinTint(a_snapshot);
        ApplyTintColors(a_actor, npc, a_snapshot, skinTint);
        a_actor->UpdateHairColor();
        a_actor->UpdateSkinColor();
        Apply3DShaderTints(a_actor, a_snapshot.hasHairTintColor ? std::optional{ a_snapshot.hairTintColor } : std::nullopt, skinTint);
    }

}
