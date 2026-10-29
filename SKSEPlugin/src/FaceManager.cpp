#include "PCH.h"
#include "FaceManager.h"
#include "Forms.h"
#include "ImpersonationManager.h"
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

    static bool ResolveSkinTintFromRace(
        RE::TESRace* a_race,
        RE::SEX a_sex,
        const std::vector<RE::TESNPC::Layer>& a_layers,
        RE::Color& a_outColor,
        RE::NiColor& a_outNiColor)
    {
        auto sexIdx = (a_sex == RE::SEX::kFemale) ? 1 : 0;
        if (!a_race || !a_race->faceRelatedData[sexIdx] || !a_race->faceRelatedData[sexIdx]->tintMasks) {
            return false;
        }
        const auto* tintMasks = a_race->faceRelatedData[sexIdx]->tintMasks;
        for (const auto* asset : *tintMasks) {
            if (asset && asset->texture.skinTone == RE::TESRace::FaceRelatedData::TintAsset::TintLayer::SkinTone::kSkinTone) {
                auto skinIndex = asset->texture.index;
                for (const auto& layer : a_layers) {
                    if (layer.tintIndex == skinIndex) {
                        a_outColor.red = layer.tintColor.red;
                        a_outColor.green = layer.tintColor.green;
                        a_outColor.blue = layer.tintColor.blue;
                        a_outNiColor.red = static_cast<float>(layer.tintColor.red) / 255.0f;
                        a_outNiColor.green = static_cast<float>(layer.tintColor.green) / 255.0f;
                        a_outNiColor.blue = static_cast<float>(layer.tintColor.blue) / 255.0f;
                        return true;
                    }
                }
                break;
            }
        }
        return false;
    }

    static void Apply3DShaderTints(
        RE::Actor* a_actor,
        bool a_hasHairTint,
        const RE::NiColor& a_hairTint,
        bool a_hasSkinTint,
        const RE::NiColor& a_skinTint)
    {
        if (!a_actor) return;

        if (a_hasHairTint) {
            if (auto* targetHair = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair)) {
                RE::BSVisit::TraverseScenegraphGeometries(targetHair, [&](RE::BSGeometry* geom) {
                    if (geom) {
                        if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                            if (auto* hairMat = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderProp->material)) {
                                hairMat->tintColor = a_hairTint;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }
        }

        if (a_hasSkinTint) {
            auto* targetFace = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
            if (targetFace) {
                RE::BSVisit::TraverseScenegraphGeometries(targetFace, [&](RE::BSGeometry* geom) {
                    if (geom) {
                        if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                            if (auto* faceMat = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderProp->material)) {
                                faceMat->tintColor = a_skinTint;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }

            if (auto* root3D = a_actor->Get3D()) {
                RE::BSVisit::TraverseScenegraphGeometries(root3D, [&](RE::BSGeometry* geom) {
                    if (geom && geom != targetFace) {
                        if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                            if (auto* faceMat = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderProp->material)) {
                                faceMat->tintColor = a_skinTint;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }
        }
    }

    bool FaceManager::CaptureSnapshot(RE::Actor* a_actor, FaceSnapshot& a_outSnapshot) {
        if (!a_actor) return false;

        auto* base = a_actor->GetActorBase();
        if (!base) return false;

        auto* npc = skyrim_cast<RE::TESNPC*>(base);
        if (!npc) return false;

        a_outSnapshot.hasSkinTintColor = false;
        a_outSnapshot.hasHairTintColor = false;

        a_outSnapshot.race = npc->GetRace();
        a_outSnapshot.skin = npc->skin;
        a_outSnapshot.farSkin = npc->farSkin;
        if (npc->GetFormID() == Forms::kPlayerBaseID) {
            a_outSnapshot.faceNPC = nullptr;
        } else {
            a_outSnapshot.faceNPC = (npc->faceNPC && npc->faceNPC != npc) ? npc->faceNPC : npc;
        }
        const char* dispName = a_actor->GetDisplayFullName();
        if (dispName && dispName[0] != '\0') {
            a_outSnapshot.fullName = dispName;
        } else {
            a_outSnapshot.fullName = npc->GetFullName();
        }
        a_outSnapshot.height = npc->height;

        auto* extraScale = a_actor->extraList.GetByType<RE::ExtraScale>();
        a_outSnapshot.scale = extraScale ? extraScale->scale : 1.0f;

        a_outSnapshot.weight = npc->weight;
        a_outSnapshot.sex = npc->GetSex();
        a_outSnapshot.bodyTintColor = npc->bodyTintColor;

        // Tint layers
        a_outSnapshot.tintLayers.clear();
        if (npc->tintLayers) {
            for (auto* layer : *npc->tintLayers) {
                if (layer) {
                    a_outSnapshot.tintLayers.push_back(*layer);
                }
            }
        }

        // Resolve skin tone from race tint assets if bodyTintColor is uninitialized (common on vanilla NPCs)
        if (a_outSnapshot.bodyTintColor.red == 0 &&
            a_outSnapshot.bodyTintColor.green == 0 &&
            a_outSnapshot.bodyTintColor.blue == 0) {
            a_outSnapshot.hasSkinTintColor = ResolveSkinTintFromRace(
                a_actor->GetRace(),
                a_outSnapshot.sex,
                a_outSnapshot.tintLayers,
                a_outSnapshot.bodyTintColor,
                a_outSnapshot.skinTintColor);
        }

        a_outSnapshot.voiceType = ImpersonationManager::GetNPCVoiceType(npc);

        if (npc->headRelatedData) {
            a_outSnapshot.hairColor = npc->headRelatedData->hairColor;
            a_outSnapshot.faceDetails = npc->headRelatedData->faceDetails;
        } else {
            a_outSnapshot.hairColor = nullptr;
            a_outSnapshot.faceDetails = nullptr;
        }

        // Capture 3D rendered hair tint directly from 3D geometry
        if (auto* hairObj = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair)) {
            RE::BSVisit::TraverseScenegraphGeometries(hairObj, [&](RE::BSGeometry* geom) {
                if (geom) {
                    if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                        if (auto* hairMat = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderProp->material)) {
                            a_outSnapshot.hairTintColor = hairMat->tintColor;
                            a_outSnapshot.hasHairTintColor = true;
                            return RE::BSVisit::BSVisitControl::kStop;
                        }
                    }
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        // Capture 3D rendered skin tint directly from face 3D geometry (overrides race-derived tint)
        if (auto* faceObj = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace)) {
            RE::BSVisit::TraverseScenegraphGeometries(faceObj, [&](RE::BSGeometry* geom) {
                if (geom) {
                    if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                        if (auto* faceMat = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderProp->material)) {
                            a_outSnapshot.skinTintColor = faceMat->tintColor;
                            a_outSnapshot.hasSkinTintColor = true;
                            return RE::BSVisit::BSVisitControl::kStop;
                        }
                    }
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        // Face morphs & parts
        if (npc->faceData) {
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
                a_outSnapshot.morphs[i] = npc->faceData->morphs[i];
            }
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
                a_outSnapshot.parts[i] = npc->faceData->parts[i];
            }
        }

        // Comprehensive Head parts capture (all types 0..6)
        a_outSnapshot.headParts.clear();
        for (int t = 0; t <= static_cast<int>(RE::BGSHeadPart::HeadPartType::kEyebrows); ++t) {
            auto type = static_cast<RE::BGSHeadPart::HeadPartType>(t);
            auto* part = npc->GetCurrentHeadPartByType(type);
            if (!part) part = npc->GetHeadPartByType(type);
            if (part) {
                a_outSnapshot.headParts.push_back(part);
            }
        }
        if (npc->headParts && npc->numHeadParts > 0) {
            for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                if (npc->headParts[i]) {
                    bool found = false;
                    for (auto* existing : a_outSnapshot.headParts) {
                        if (existing == npc->headParts[i]) { found = true; break; }
                    }
                    if (!found) a_outSnapshot.headParts.push_back(npc->headParts[i]);
                }
            }
        }

        return true;
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

        if (!a_asPuppet && a_snapshot.faceNPC && a_snapshot.faceNPC != targetNPC && a_snapshot.faceNPC->GetFormID() != 0x07) {
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
        targetNPC->bodyTintColor = a_snapshot.bodyTintColor;

        // 4. Hair & Face details
        targetNPC->SetHairColor(a_snapshot.hairColor);
        targetNPC->SetFaceTexture(a_snapshot.faceDetails);

        // 5. Face morphs & parts
        if (!targetNPC->faceData) {
            targetNPC->faceData = RE::calloc<RE::TESNPC::FaceData>(1);
            if (targetNPC->faceData) {
                std::memset(targetNPC->faceData, 0, sizeof(RE::TESNPC::FaceData));
            }
        }
        if (targetNPC->faceData) {
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
                targetNPC->faceData->morphs[i] = a_snapshot.morphs[i];
            }
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
                targetNPC->faceData->parts[i] = a_snapshot.parts[i];
            }
        }

        // 6. Headparts Replacement: only free buffers that were dynamically allocated by us
        if (targetNPC->headParts) {
            if (_allocatedHeadPartBuffers.erase(targetNPC->headParts) > 0) {
                RE::free(targetNPC->headParts);
            }
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
                    _allocatedHeadPartBuffers.insert(newParts);
                    targetNPC->headParts = newParts;
                    targetNPC->numHeadParts = count;
                }
            }
        }

        // 6b. Tint Layers Replication
        if (!a_snapshot.tintLayers.empty()) {
            if (!targetNPC->tintLayers) {
                auto* raw = RE::calloc<RE::BSTArray<RE::TESNPC::Layer*>>(1);
                if (raw) {
                    targetNPC->tintLayers = new (raw) RE::BSTArray<RE::TESNPC::Layer*>();
                }
            }
            if (targetNPC->tintLayers) {
                for (auto* oldLayer : *targetNPC->tintLayers) {
                    if (oldLayer) {
                        RE::free(oldLayer);
                    }
                }
                targetNPC->tintLayers->clear();

                for (const auto& layer : a_snapshot.tintLayers) {
                    auto* newLayer = RE::calloc<RE::TESNPC::Layer>(1);
                    if (newLayer) {
                        *newLayer = layer;
                        targetNPC->tintLayers->push_back(newLayer);
                    }
                }
            }
        } else if (targetNPC->tintLayers) {
            for (auto* oldLayer : *targetNPC->tintLayers) {
                if (oldLayer) {
                    RE::free(oldLayer);
                }
            }
            targetNPC->tintLayers->clear();
        }

        // Resolve skin tint color for body tint
        RE::NiColor skinTint{};
        bool hasSkinTint = false;
        if (a_snapshot.hasSkinTintColor) {
            skinTint = a_snapshot.skinTintColor;
            hasSkinTint = true;
        } else if (a_snapshot.bodyTintColor.red != 0 || a_snapshot.bodyTintColor.green != 0 || a_snapshot.bodyTintColor.blue != 0) {
            skinTint.red = static_cast<float>(a_snapshot.bodyTintColor.red) / 255.0f;
            skinTint.green = static_cast<float>(a_snapshot.bodyTintColor.green) / 255.0f;
            skinTint.blue = static_cast<float>(a_snapshot.bodyTintColor.blue) / 255.0f;
            hasSkinTint = true;
        } else {
            RE::Color resolvedBodyColor{};
            hasSkinTint = ResolveSkinTintFromRace(
                a_snapshot.race,
                a_snapshot.sex,
                a_snapshot.tintLayers,
                resolvedBodyColor,
                skinTint);
        }

        if (hasSkinTint) {
            targetNPC->bodyTintColor.red = static_cast<std::uint8_t>(std::clamp(skinTint.red * 255.0f, 0.0f, 255.0f));
            targetNPC->bodyTintColor.green = static_cast<std::uint8_t>(std::clamp(skinTint.green * 255.0f, 0.0f, 255.0f));
            targetNPC->bodyTintColor.blue = static_cast<std::uint8_t>(std::clamp(skinTint.blue * 255.0f, 0.0f, 255.0f));
        }

        if (a_update3D) {
            a_targetActor->DoReset3D(true);
            a_targetActor->Update3DModel();
            a_targetActor->UpdateHairColor();
            a_targetActor->UpdateSkinColor();

            Apply3DShaderTints(
                a_targetActor,
                a_snapshot.hasHairTintColor,
                a_snapshot.hairTintColor,
                hasSkinTint,
                skinTint);
        }

        SKSE::log::info("Applied appearance snapshot (Race: {:08X}, Sex: {}, Voice: {:08X}, Tints: {}, Parts: {}) to actor {:08X}",
            a_snapshot.race ? static_cast<std::uint32_t>(a_snapshot.race->GetFormID()) : 0,
            a_snapshot.sex == RE::SEX::kFemale ? "Female" : "Male",
            targetNPC->voiceType ? static_cast<std::uint32_t>(targetNPC->voiceType->GetFormID()) : 0,
            a_snapshot.tintLayers.size(),
            a_snapshot.headParts.size(),
            static_cast<std::uint32_t>(a_targetActor->GetFormID())
        );

        return true;
    }

    bool FaceManager::CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target) {
        if (!a_donor || !a_target) {
            SKSE::log::warn("CopyAppearance called with null actor pointers.");
            return false;
        }

        FaceSnapshot donorSnapshot;
        if (!CaptureSnapshot(a_donor, donorSnapshot)) {
            SKSE::log::error("Failed to capture appearance of donor actor {:08X}.",
                static_cast<std::uint32_t>(a_donor->GetFormID()));
            return false;
        }

        return ApplySnapshot(a_target, donorSnapshot);
    }

}
