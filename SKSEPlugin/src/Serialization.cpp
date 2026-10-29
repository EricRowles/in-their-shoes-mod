#include "PCH.h"
#include "Serialization.h"
#include "FaceManager.h"
#include "IdleAIManager.h"
#include "DialogueManager.h"
#include "ImpersonationManager.h"

namespace InTheirShoes::Serialization {

    void Register() {
        auto* serde = SKSE::GetSerializationInterface();
        if (!serde) {
            SKSE::log::error("Failed to obtain SKSE Serialization Interface!");
            return;
        }

        serde->SetUniqueID('ALTA');
        serde->SetSaveCallback(SaveCallback);
        serde->SetLoadCallback(LoadCallback);
        serde->SetRevertCallback(RevertCallback);
        SKSE::log::info("Registered SKSE Co-Save Serialization callbacks for ALTA.");
    }

    static void WriteFormID(SKSE::SerializationInterface* a_serde, const RE::TESForm* a_form) {
        RE::FormID id = a_form ? a_form->GetFormID() : 0;
        a_serde->WriteRecordData(&id, sizeof(id));
    }

    template <class T>
    static T* ReadForm(SKSE::SerializationInterface* a_serde) {
        RE::FormID id = 0;
        if (!a_serde->ReadRecordData(&id, sizeof(id)) || id == 0 || !a_serde->ResolveFormID(id, id)) {
            return nullptr;
        }
        return RE::TESForm::LookupByID<T>(id);
    }

    static void WriteFaceSnapshot(SKSE::SerializationInterface* a_serde, const FaceSnapshot& a_face) {
        RE::FormID raceID = a_face.race ? a_face.race->GetFormID() : 0;
        RE::FormID faceNPCID = (a_face.faceNPC && a_face.faceNPC->GetFormID() != 0x07) ? a_face.faceNPC->GetFormID() : 0;
        RE::FormID hairColorID = a_face.hairColor ? a_face.hairColor->GetFormID() : 0;
        RE::FormID voiceTypeID = a_face.voiceType ? a_face.voiceType->GetFormID() : 0;

        a_serde->WriteRecordData(&raceID, sizeof(raceID));
        a_serde->WriteRecordData(&faceNPCID, sizeof(faceNPCID));
        a_serde->WriteRecordData(&hairColorID, sizeof(hairColorID));
        a_serde->WriteRecordData(&voiceTypeID, sizeof(voiceTypeID));
        a_serde->WriteRecordData(&a_face.bodyTintColor, sizeof(a_face.bodyTintColor));
        a_serde->WriteRecordData(&a_face.height, sizeof(a_face.height));
        a_serde->WriteRecordData(&a_face.scale, sizeof(a_face.scale));
        a_serde->WriteRecordData(&a_face.weight, sizeof(a_face.weight));

        std::uint8_t sexVal = static_cast<std::uint8_t>(a_face.sex);
        a_serde->WriteRecordData(&sexVal, sizeof(sexVal));

        a_serde->WriteRecordData(a_face.morphs, sizeof(a_face.morphs));
        a_serde->WriteRecordData(a_face.parts, sizeof(a_face.parts));

        std::uint32_t hpCount = static_cast<std::uint32_t>(a_face.headParts.size());
        a_serde->WriteRecordData(&hpCount, sizeof(hpCount));
        for (auto* hp : a_face.headParts) {
            RE::FormID hpID = hp ? hp->GetFormID() : 0;
            a_serde->WriteRecordData(&hpID, sizeof(hpID));
        }

        a_serde->WriteRecordData(&a_face.hasHairTintColor, sizeof(a_face.hasHairTintColor));
        a_serde->WriteRecordData(&a_face.hairTintColor.red, sizeof(a_face.hairTintColor.red));
        a_serde->WriteRecordData(&a_face.hairTintColor.green, sizeof(a_face.hairTintColor.green));
        a_serde->WriteRecordData(&a_face.hairTintColor.blue, sizeof(a_face.hairTintColor.blue));

        // Tint layers
        std::uint32_t tintCount = static_cast<std::uint32_t>(a_face.tintLayers.size());
        a_serde->WriteRecordData(&tintCount, sizeof(tintCount));
        for (const auto& layer : a_face.tintLayers) {
            a_serde->WriteRecordData(&layer, sizeof(RE::TESNPC::Layer));
        }

        // Skin tint color
        a_serde->WriteRecordData(&a_face.hasSkinTintColor, sizeof(a_face.hasSkinTintColor));
        a_serde->WriteRecordData(&a_face.skinTintColor.red, sizeof(a_face.skinTintColor.red));
        a_serde->WriteRecordData(&a_face.skinTintColor.green, sizeof(a_face.skinTintColor.green));
        a_serde->WriteRecordData(&a_face.skinTintColor.blue, sizeof(a_face.skinTintColor.blue));

        // Full name
        std::string nameStr(a_face.fullName.c_str());
        std::uint32_t nameLen = static_cast<std::uint32_t>(nameStr.size());
        a_serde->WriteRecordData(&nameLen, sizeof(nameLen));
        if (nameLen > 0) {
            a_serde->WriteRecordData(nameStr.data(), nameLen);
        }

        // Face details texture set & base skins
        RE::FormID faceDetailsID = a_face.faceDetails ? a_face.faceDetails->GetFormID() : 0;
        a_serde->WriteRecordData(&faceDetailsID, sizeof(faceDetailsID));
        WriteFormID(a_serde, a_face.skin);
        WriteFormID(a_serde, a_face.farSkin);
    }

    static void WriteBaseline(SKSE::SerializationInterface* a_serde, const Baseline& a_baseline) {
        if (!a_serde->OpenRecord(kBaselineRecord, kSerializationVersion)) {
            return;
        }

        WriteFaceSnapshot(a_serde, a_baseline.face);

        std::uint32_t statCount = static_cast<std::uint32_t>(a_baseline.stats.baseValues.size());
        a_serde->WriteRecordData(&statCount, sizeof(statCount));
        for (const auto& [av, val] : a_baseline.stats.baseValues) {
            a_serde->WriteRecordData(&av, sizeof(av));
            a_serde->WriteRecordData(&val, sizeof(val));
        }

        const auto& spells = a_baseline.spells;
        std::uint32_t spellCount = static_cast<std::uint32_t>(spells.addedSpells.size());
        a_serde->WriteRecordData(&spellCount, sizeof(spellCount));
        for (auto id : spells.addedSpells) {
            a_serde->WriteRecordData(&id, sizeof(id));
        }
        WriteFormID(a_serde, spells.leftSpell);
        WriteFormID(a_serde, spells.rightSpell);
        WriteFormID(a_serde, spells.shout);
    }

    void SaveCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        auto& mgr = ImpersonationManager::GetSingleton();
        if (const auto& baseline = mgr.GetBaseline()) {
            WriteBaseline(a_serde, *baseline);
        }
        mgr.SaveSession(a_serde);
        if (const auto& donorFace = mgr.GetDonorFace(); donorFace && mgr.IsActive() &&
            a_serde->OpenRecord(kDonorFaceRecord, kSerializationVersion)) {
            WriteFaceSnapshot(a_serde, *donorFace);
        }
        mgr.SaveOutfitDisabled(a_serde);
        mgr.SaveDonorAliasFactions(a_serde);

        SKSE::log::info("Completed SKSE Co-Save serialization write.");
    }

    static bool ReadFaceSnapshot(SKSE::SerializationInterface* a_serde, FaceSnapshot& a_outFace) {
        RE::FormID raceID = 0, faceNPCID = 0, hairColorID = 0, voiceTypeID = 0;

        if (!a_serde->ReadRecordData(&raceID, sizeof(raceID))) return false;
        a_serde->ReadRecordData(&faceNPCID, sizeof(faceNPCID));
        a_serde->ReadRecordData(&hairColorID, sizeof(hairColorID));
        a_serde->ReadRecordData(&voiceTypeID, sizeof(voiceTypeID));
        a_serde->ReadRecordData(&a_outFace.bodyTintColor, sizeof(a_outFace.bodyTintColor));
        a_serde->ReadRecordData(&a_outFace.height, sizeof(a_outFace.height));
        a_serde->ReadRecordData(&a_outFace.scale, sizeof(a_outFace.scale));
        a_serde->ReadRecordData(&a_outFace.weight, sizeof(a_outFace.weight));

        std::uint8_t sexVal = 0;
        a_serde->ReadRecordData(&sexVal, sizeof(sexVal));
        a_outFace.sex = static_cast<RE::SEX>(sexVal);

        a_serde->ReadRecordData(a_outFace.morphs, sizeof(a_outFace.morphs));
        a_serde->ReadRecordData(a_outFace.parts, sizeof(a_outFace.parts));

        std::uint32_t hpCount = 0;
        if (a_serde->ReadRecordData(&hpCount, sizeof(hpCount))) {
            for (std::uint32_t i = 0; i < hpCount; ++i) {
                RE::FormID hpID = 0;
                a_serde->ReadRecordData(&hpID, sizeof(hpID));
                if (a_serde->ResolveFormID(hpID, hpID)) {
                    auto* hp = RE::TESForm::LookupByID<RE::BGSHeadPart>(hpID);
                    if (hp) a_outFace.headParts.push_back(hp);
                }
            }
        }

        a_serde->ReadRecordData(&a_outFace.hasHairTintColor, sizeof(a_outFace.hasHairTintColor));
        a_serde->ReadRecordData(&a_outFace.hairTintColor.red, sizeof(a_outFace.hairTintColor.red));
        a_serde->ReadRecordData(&a_outFace.hairTintColor.green, sizeof(a_outFace.hairTintColor.green));
        a_serde->ReadRecordData(&a_outFace.hairTintColor.blue, sizeof(a_outFace.hairTintColor.blue));

        if (a_serde->ResolveFormID(raceID, raceID)) {
            a_outFace.race = RE::TESForm::LookupByID<RE::TESRace>(raceID);
        }
        if (a_serde->ResolveFormID(faceNPCID, faceNPCID)) {
            if (faceNPCID != 0x07) {
                a_outFace.faceNPC = RE::TESForm::LookupByID<RE::TESNPC>(faceNPCID);
            } else {
                a_outFace.faceNPC = nullptr;
            }
        }
        if (a_serde->ResolveFormID(hairColorID, hairColorID)) {
            a_outFace.hairColor = RE::TESForm::LookupByID<RE::BGSColorForm>(hairColorID);
        }
        if (a_serde->ResolveFormID(voiceTypeID, voiceTypeID)) {
            a_outFace.voiceType = RE::TESForm::LookupByID<RE::BGSVoiceType>(voiceTypeID);
        }

        // Tint layers
        std::uint32_t tintCount = 0;
        if (a_serde->ReadRecordData(&tintCount, sizeof(tintCount))) {
            for (std::uint32_t i = 0; i < tintCount; ++i) {
                RE::TESNPC::Layer layer{};
                if (a_serde->ReadRecordData(&layer, sizeof(RE::TESNPC::Layer))) {
                    a_outFace.tintLayers.push_back(layer);
                }
            }
        }

        // Skin tint color
        if (a_serde->ReadRecordData(&a_outFace.hasSkinTintColor, sizeof(a_outFace.hasSkinTintColor))) {
            a_serde->ReadRecordData(&a_outFace.skinTintColor.red, sizeof(a_outFace.skinTintColor.red));
            a_serde->ReadRecordData(&a_outFace.skinTintColor.green, sizeof(a_outFace.skinTintColor.green));
            a_serde->ReadRecordData(&a_outFace.skinTintColor.blue, sizeof(a_outFace.skinTintColor.blue));
        }

        std::uint32_t nameLen = 0;
        if (a_serde->ReadRecordData(&nameLen, sizeof(nameLen)) && nameLen > 0) {
            std::string nameStr(nameLen, '\0');
            if (a_serde->ReadRecordData(nameStr.data(), nameLen)) {
                a_outFace.fullName = nameStr.c_str();
            }
        }

        RE::FormID faceDetailsID = 0;
        if (a_serde->ReadRecordData(&faceDetailsID, sizeof(faceDetailsID))) {
            if (faceDetailsID != 0 && a_serde->ResolveFormID(faceDetailsID, faceDetailsID)) {
                a_outFace.faceDetails = RE::TESForm::LookupByID<RE::BGSTextureSet>(faceDetailsID);
            }
        }
        a_outFace.skin = ReadForm<RE::TESObjectARMO>(a_serde);
        a_outFace.farSkin = ReadForm<RE::TESObjectARMO>(a_serde);

        return true;
    }

    static bool ReadBaseline(SKSE::SerializationInterface* a_serde, Baseline& a_out) {
        if (!ReadFaceSnapshot(a_serde, a_out.face)) return false;

        std::uint32_t statCount = 0;
        if (!a_serde->ReadRecordData(&statCount, sizeof(statCount))) return false;
        for (std::uint32_t i = 0; i < statCount; ++i) {
            std::uint32_t av = 0;
            float val = 0.0f;
            if (!a_serde->ReadRecordData(&av, sizeof(av)) || !a_serde->ReadRecordData(&val, sizeof(val))) return false;
            a_out.stats.baseValues[av] = val;
        }

        std::uint32_t spellCount = 0;
        if (!a_serde->ReadRecordData(&spellCount, sizeof(spellCount))) return false;
        for (std::uint32_t i = 0; i < spellCount; ++i) {
            RE::FormID id = 0;
            if (!a_serde->ReadRecordData(&id, sizeof(id))) return false;
            if (a_serde->ResolveFormID(id, id)) {
                a_out.spells.addedSpells.push_back(id);
            }
        }
        a_out.spells.leftSpell = ReadForm<RE::SpellItem>(a_serde);
        a_out.spells.rightSpell = ReadForm<RE::SpellItem>(a_serde);
        a_out.spells.shout = ReadForm<RE::TESShout>(a_serde);
        return true;
    }

    void LoadCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        RevertCallback(a_serde);

        std::uint32_t type;
        std::uint32_t version;
        std::uint32_t length;

        while (a_serde->GetNextRecordInfo(type, version, length)) {
            if (version != kSerializationVersion) {
                SKSE::log::warn("Skipping record {:08X} with unsupported version {}", type, version);
                continue;
            }

            switch (type) {
                case kBaselineRecord: {
                    Baseline baseline;
                    if (ReadBaseline(a_serde, baseline)) {
                        ImpersonationManager::GetSingleton().SetBaseline(std::move(baseline));
                        SKSE::log::info("Loaded Dragonborn baseline from co-save.");
                    } else {
                        SKSE::log::warn("Baseline record is truncated; ignoring it.");
                    }
                    break;
                }

                case kSessionRecord:
                    ImpersonationManager::GetSingleton().LoadSession(a_serde);
                    break;

                case kOutfitDisabledRecord:
                    ImpersonationManager::GetSingleton().LoadOutfitDisabled(a_serde);
                    break;

                case kDonorAliasFactionsRecord:
                    ImpersonationManager::GetSingleton().LoadDonorAliasFactions(a_serde);
                    break;

                case kDonorFaceRecord: {
                    FaceSnapshot donorFace;
                    if (ReadFaceSnapshot(a_serde, donorFace)) {
                        ImpersonationManager::GetSingleton().SetDonorFace(std::move(donorFace));
                        SKSE::log::info("Loaded the donor's appearance from co-save.");
                    } else {
                        SKSE::log::warn("Donor appearance record is truncated; ignoring it.");
                    }
                    break;
                }

                default:
                    SKSE::log::warn("Unknown record type {:08X} in co-save", type);
                    break;
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        ImpersonationManager::GetSingleton().OnRevert();
        IdleAIManager::GetSingleton().Clear();
        DialogueManager::GetSingleton().Clear();
        SKSE::log::info("Reverted all InTheirShoes saved manager states.");
    }

}
