#include "PCH.h"
#include "Serialization.h"
#include "AliasSwapManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "ImpersonationManager.h"
#include "Lifecycle.h"
#include "OutfitManager.h"

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

    // Readers return false only when the record ends early. A form that no longer exists (or was saved as
    // none) reads as nullptr.
    template <class T>
    static bool Read(SKSE::SerializationInterface* a_serde, T& a_out) {
        return a_serde->ReadRecordData(&a_out, sizeof(T)) == sizeof(T);
    }

    template <class T>
    static bool ReadForm(SKSE::SerializationInterface* a_serde, T*& a_out) {
        RE::FormID id = 0;
        if (!Read(a_serde, id)) return false;
        a_out = (id != 0 && a_serde->ResolveFormID(id, id)) ? RE::TESForm::LookupByID<T>(id) : nullptr;
        return true;
    }

    // Guards against allocating from a corrupt length
    constexpr std::uint32_t kMaxNameLength = 1024;

    static void WriteFaceSnapshot(SKSE::SerializationInterface* a_serde, const FaceSnapshot& a_face) {
        RE::FormID raceID = a_face.race ? a_face.race->GetFormID() : 0;
        RE::FormID faceNPCID = (a_face.faceNPC && a_face.faceNPC->GetFormID() != Forms::kPlayerBaseID) ? a_face.faceNPC->GetFormID() : 0;
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
        if (!a_serde->OpenRecord(kBaselineRecord, kBaselineVersion)) {
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
        if (const auto& donorFace = mgr.GetDonorFace(); donorFace && Session::GetSingleton().IsActive() &&
            a_serde->OpenRecord(kDonorFaceRecord, kDonorFaceVersion)) {
            WriteFaceSnapshot(a_serde, *donorFace);
        }
        OutfitManager::GetSingleton().Save(a_serde);
        AliasSwapManager::GetSingleton().SaveDonorAliasFactions(a_serde);

        SKSE::log::info("Completed SKSE Co-Save serialization write.");
    }

    // Fails if the record ends early, so a damaged snapshot is never applied in part
    static bool ReadFaceSnapshot(SKSE::SerializationInterface* a_serde, FaceSnapshot& a_outFace) {
        std::uint8_t sexVal = 0;
        if (!ReadForm(a_serde, a_outFace.race) ||
            !ReadForm(a_serde, a_outFace.faceNPC) ||
            !ReadForm(a_serde, a_outFace.hairColor) ||
            !ReadForm(a_serde, a_outFace.voiceType) ||
            !Read(a_serde, a_outFace.bodyTintColor) ||
            !Read(a_serde, a_outFace.height) ||
            !Read(a_serde, a_outFace.scale) ||
            !Read(a_serde, a_outFace.weight) ||
            !Read(a_serde, sexVal) ||
            !Read(a_serde, a_outFace.morphs) ||
            !Read(a_serde, a_outFace.parts)) {
            return false;
        }
        a_outFace.sex = static_cast<RE::SEX>(sexVal);
        // The player's own base record is saved as none (see WriteFaceSnapshot)
        if (a_outFace.faceNPC && a_outFace.faceNPC->GetFormID() == Forms::kPlayerBaseID) {
            a_outFace.faceNPC = nullptr;
        }

        std::uint32_t hpCount = 0;
        if (!Read(a_serde, hpCount)) return false;
        for (std::uint32_t i = 0; i < hpCount; ++i) {
            RE::BGSHeadPart* hp = nullptr;
            if (!ReadForm(a_serde, hp)) return false;
            if (hp) a_outFace.headParts.push_back(hp);
        }

        if (!Read(a_serde, a_outFace.hasHairTintColor) ||
            !Read(a_serde, a_outFace.hairTintColor.red) ||
            !Read(a_serde, a_outFace.hairTintColor.green) ||
            !Read(a_serde, a_outFace.hairTintColor.blue)) {
            return false;
        }

        // Tint layers
        std::uint32_t tintCount = 0;
        if (!Read(a_serde, tintCount)) return false;
        for (std::uint32_t i = 0; i < tintCount; ++i) {
            RE::TESNPC::Layer layer{};
            if (!Read(a_serde, layer)) return false;
            a_outFace.tintLayers.push_back(layer);
        }

        // Skin tint color
        if (!Read(a_serde, a_outFace.hasSkinTintColor) ||
            !Read(a_serde, a_outFace.skinTintColor.red) ||
            !Read(a_serde, a_outFace.skinTintColor.green) ||
            !Read(a_serde, a_outFace.skinTintColor.blue)) {
            return false;
        }

        std::uint32_t nameLen = 0;
        if (!Read(a_serde, nameLen) || nameLen > kMaxNameLength) return false;
        if (nameLen > 0) {
            std::string nameStr(nameLen, '\0');
            if (a_serde->ReadRecordData(nameStr.data(), nameLen) != nameLen) return false;
            a_outFace.fullName = nameStr.c_str();
        }

        return ReadForm(a_serde, a_outFace.faceDetails) &&
               ReadForm(a_serde, a_outFace.skin) &&
               ReadForm(a_serde, a_outFace.farSkin);
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
        return ReadForm(a_serde, a_out.spells.leftSpell) &&
               ReadForm(a_serde, a_out.spells.rightSpell) &&
               ReadForm(a_serde, a_out.spells.shout);
    }

    // Whether this build can read a record saved at a_version (it reads a_current and every older
    // version back to kMinSupportedVersion)
    static bool IsReadableVersion(std::uint32_t a_type, std::uint32_t a_version, std::uint32_t a_current) {
        if (a_version >= kMinSupportedVersion && a_version <= a_current) return true;
        SKSE::log::warn("Skipping record {:08X}: saved at version {}, this build reads versions {} to {}.",
            a_type, a_version, kMinSupportedVersion, a_current);
        return false;
    }

    void LoadCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        RevertCallback(a_serde);

        std::uint32_t type;
        std::uint32_t version;
        std::uint32_t length;

        while (a_serde->GetNextRecordInfo(type, version, length)) {
            switch (type) {
                case kBaselineRecord: {
                    if (!IsReadableVersion(type, version, kBaselineVersion)) break;
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
                    if (!IsReadableVersion(type, version, kSessionVersion)) break;
                    ImpersonationManager::GetSingleton().LoadSession(a_serde);
                    break;

                case kOutfitDisabledRecord:
                    if (!IsReadableVersion(type, version, kOutfitDisabledVersion)) break;
                    OutfitManager::GetSingleton().Load(a_serde);
                    break;

                case kDonorAliasFactionsRecord:
                    if (!IsReadableVersion(type, version, kDonorAliasFactionsVersion)) break;
                    AliasSwapManager::GetSingleton().LoadDonorAliasFactions(a_serde);
                    break;

                case kDonorFaceRecord: {
                    if (!IsReadableVersion(type, version, kDonorFaceVersion)) break;
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
        Lifecycle::OnGameRevert();
    }

}
