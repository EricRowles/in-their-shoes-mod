#include "PCH.h"
#include "Serialization.h"
#include "AliasSwapManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "ImpersonationManager.h"
#include "Lifecycle.h"

#include <type_traits>

namespace InTheirShoes::Serialization {

    namespace {
        using Serde = SKSE::SerializationInterface;

        // --- Primitives ---
        // Every record is written and read with these, so a record's layout is the sequence of calls in its
        // Write/Read pair below. Readers return false only when the record ends early; callers then drop the
        // whole record rather than apply part of it.

        // Saved as raw bytes: plain data, never a pointer (forms are saved by ID). Engine types such as
        // RE::Color define copy operators, so "trivially copyable" is too strict.
        template <class T>
        constexpr bool kIsRawData = std::is_standard_layout_v<T> && std::is_trivially_destructible_v<T> && !std::is_pointer_v<T>;

        template <class T>
        void Write(Serde* a_serde, const T& a_value) {
            static_assert(kIsRawData<T>);
            a_serde->WriteRecordData(&a_value, sizeof(T));
        }

        template <class T>
        bool Read(Serde* a_serde, T& a_out) {
            static_assert(kIsRawData<T>);
            return a_serde->ReadRecordData(&a_out, sizeof(T)) == sizeof(T);
        }

        void WriteFormID(Serde* a_serde, const RE::TESForm* a_form) {
            Write(a_serde, a_form ? a_form->GetFormID() : RE::FormID{ 0 });
        }

        // A saved form ID mapped to the current load order; 0 if it was saved as none or its plugin is gone
        bool ReadFormID(Serde* a_serde, RE::FormID& a_out) {
            if (!Read(a_serde, a_out)) return false;
            if (a_out != 0 && !a_serde->ResolveFormID(a_out, a_out)) {
                a_out = 0;
            }
            return true;
        }

        // A form that no longer exists (or was saved as none) reads as nullptr
        template <class T>
        bool ReadForm(Serde* a_serde, T*& a_out) {
            RE::FormID id = 0;
            if (!ReadFormID(a_serde, id)) return false;
            a_out = id != 0 ? RE::TESForm::LookupByID<T>(id) : nullptr;
            return true;
        }

        // A count, then a_writeItem for each element
        template <class Range, class WriteItem>
        void WriteList(Serde* a_serde, const Range& a_range, WriteItem&& a_writeItem) {
            Write(a_serde, static_cast<std::uint32_t>(std::size(a_range)));
            for (const auto& item : a_range) {
                a_writeItem(item);
            }
        }

        // Reads a count, then calls a_readItem (which returns false if the record ends) that many times
        template <class ReadItem>
        bool ReadList(Serde* a_serde, ReadItem&& a_readItem) {
            std::uint32_t count = 0;
            if (!Read(a_serde, count)) return false;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!a_readItem()) return false;
            }
            return true;
        }

        // Guards against allocating from a corrupt length
        constexpr std::uint32_t kMaxStringLength = 1024;

        void WriteString(Serde* a_serde, std::string_view a_str) {
            Write(a_serde, static_cast<std::uint32_t>(a_str.size()));
            if (!a_str.empty()) {
                a_serde->WriteRecordData(a_str.data(), static_cast<std::uint32_t>(a_str.size()));
            }
        }

        bool ReadString(Serde* a_serde, RE::BSFixedString& a_out) {
            std::uint32_t length = 0;
            if (!Read(a_serde, length) || length > kMaxStringLength) return false;
            if (length > 0) {
                std::string str(length, '\0');
                if (a_serde->ReadRecordData(str.data(), length) != length) return false;
                a_out = str.c_str();
            }
            return true;
        }

        // --- Appearance (part of 'BASE', and the whole of 'DFAC') ---

        // Tint colors are saved as their three floats
        static_assert(sizeof(RE::NiColor) == 3 * sizeof(float));

        void WriteFaceSnapshot(Serde* a_serde, const FaceSnapshot& a_face) {
            // The player's own base record is saved as none: it is the target the snapshot is applied to
            const bool faceIsPlayer = a_face.faceNPC && a_face.faceNPC->GetFormID() == Forms::kPlayerBaseID;
            WriteFormID(a_serde, a_face.race);
            WriteFormID(a_serde, faceIsPlayer ? nullptr : a_face.faceNPC);
            WriteFormID(a_serde, a_face.hairColor);
            WriteFormID(a_serde, a_face.voiceType);
            Write(a_serde, a_face.bodyTintColor);
            Write(a_serde, a_face.height);
            Write(a_serde, a_face.scale);
            Write(a_serde, a_face.weight);
            Write(a_serde, static_cast<std::uint8_t>(a_face.sex));
            Write(a_serde, a_face.morphs);
            Write(a_serde, a_face.parts);
            WriteList(a_serde, a_face.headParts, [&](const RE::BGSHeadPart* a_part) { WriteFormID(a_serde, a_part); });
            Write(a_serde, a_face.hasHairTintColor);
            Write(a_serde, a_face.hairTintColor);
            WriteList(a_serde, a_face.tintLayers, [&](const RE::TESNPC::Layer& a_layer) { Write(a_serde, a_layer); });
            Write(a_serde, a_face.hasSkinTintColor);
            Write(a_serde, a_face.skinTintColor);
            WriteString(a_serde, a_face.fullName.c_str());
            WriteFormID(a_serde, a_face.faceDetails);
            WriteFormID(a_serde, a_face.skin);
            WriteFormID(a_serde, a_face.farSkin);
        }

        bool ReadFaceSnapshot(Serde* a_serde, FaceSnapshot& a_out) {
            std::uint8_t sex = 0;
            if (!ReadForm(a_serde, a_out.race) ||
                !ReadForm(a_serde, a_out.faceNPC) ||
                !ReadForm(a_serde, a_out.hairColor) ||
                !ReadForm(a_serde, a_out.voiceType) ||
                !Read(a_serde, a_out.bodyTintColor) ||
                !Read(a_serde, a_out.height) ||
                !Read(a_serde, a_out.scale) ||
                !Read(a_serde, a_out.weight) ||
                !Read(a_serde, sex) ||
                !Read(a_serde, a_out.morphs) ||
                !Read(a_serde, a_out.parts)) {
                return false;
            }
            a_out.sex = static_cast<RE::SEX>(sex);

            const bool ok =
                ReadList(a_serde, [&] {
                    RE::BGSHeadPart* part = nullptr;
                    if (!ReadForm(a_serde, part)) return false;
                    if (part) a_out.headParts.push_back(part);
                    return true;
                }) &&
                Read(a_serde, a_out.hasHairTintColor) &&
                Read(a_serde, a_out.hairTintColor) &&
                ReadList(a_serde, [&] {
                    RE::TESNPC::Layer layer{};
                    if (!Read(a_serde, layer)) return false;
                    a_out.tintLayers.push_back(layer);
                    return true;
                }) &&
                Read(a_serde, a_out.hasSkinTintColor) &&
                Read(a_serde, a_out.skinTintColor) &&
                ReadString(a_serde, a_out.fullName) &&
                ReadForm(a_serde, a_out.faceDetails) &&
                ReadForm(a_serde, a_out.skin) &&
                ReadForm(a_serde, a_out.farSkin);
            return ok;
        }

        // --- 'BASE': the Dragonborn's face, stats and spells ---

        void WriteBaseline(Serde* a_serde, const Baseline& a_baseline) {
            if (!a_serde->OpenRecord(kBaselineRecord, kBaselineVersion)) return;
            WriteFaceSnapshot(a_serde, a_baseline.face);
            WriteList(a_serde, a_baseline.stats.baseValues, [&](const auto& a_entry) {
                Write(a_serde, a_entry.first);
                Write(a_serde, a_entry.second);
            });
            const auto& spells = a_baseline.spells;
            WriteList(a_serde, spells.addedSpells, [&](RE::FormID a_id) { Write(a_serde, a_id); });
            WriteFormID(a_serde, spells.leftSpell);
            WriteFormID(a_serde, spells.rightSpell);
            WriteFormID(a_serde, spells.shout);
        }

        bool ReadBaseline(Serde* a_serde, Baseline& a_out) {
            return ReadFaceSnapshot(a_serde, a_out.face) &&
                   ReadList(a_serde, [&] {
                       std::uint32_t av = 0;
                       float value = 0.0f;
                       if (!Read(a_serde, av) || !Read(a_serde, value)) return false;
                       a_out.stats.baseValues[av] = value;
                       return true;
                   }) &&
                   ReadList(a_serde, [&] {
                       RE::FormID id = 0;
                       if (!ReadFormID(a_serde, id)) return false;
                       if (id != 0) a_out.spells.addedSpells.push_back(id);
                       return true;
                   }) &&
                   ReadForm(a_serde, a_out.spells.leftSpell) &&
                   ReadForm(a_serde, a_out.spells.rightSpell) &&
                   ReadForm(a_serde, a_out.spells.shout);
        }

        // --- 'SESS': donor, puppet, swapped aliases and donor quest items carried by the player ---

        void WriteSession(Serde* a_serde) {
            auto& session = Session::GetSingleton();
            auto* donor = session.GetDonor();
            if (!session.IsActive() || !donor) return;
            if (!a_serde->OpenRecord(kSessionRecord, kSessionVersion)) return;

            WriteFormID(a_serde, donor);
            WriteFormID(a_serde, session.GetPuppet());
            const auto& aliases = AliasSwapManager::GetSingleton().GetSwappedAliases();
            WriteList(a_serde, aliases, [&](const AliasSwapManager::SwappedAlias& a_alias) {
                Write(a_serde, a_alias.questFormID);
                Write(a_serde, a_alias.aliasID);
            });
            const auto& questItems = ImpersonationManager::GetSingleton().GetDonorQuestItems();
            WriteList(a_serde, questItems, [&](RE::FormID a_id) { Write(a_serde, a_id); });
            SKSE::log::info("Saved impersonation session ({} swapped aliases, {} donor quest items).",
                aliases.size(), questItems.size());
        }

        void ReadSession(Serde* a_serde) {
            RE::FormID donorID = 0;
            RE::Actor* puppet = nullptr;
            std::vector<AliasSwapManager::SwappedAlias> aliases;
            std::unordered_set<RE::FormID> questItems;
            const bool ok =
                ReadFormID(a_serde, donorID) &&
                ReadForm(a_serde, puppet) &&
                ReadList(a_serde, [&] {
                    AliasSwapManager::SwappedAlias alias;
                    if (!ReadFormID(a_serde, alias.questFormID) || !Read(a_serde, alias.aliasID)) return false;
                    if (alias.questFormID != 0) aliases.push_back(alias);
                    return true;
                }) &&
                ReadList(a_serde, [&] {
                    RE::FormID id = 0;
                    if (!ReadFormID(a_serde, id)) return false;
                    if (id != 0) questItems.insert(id);
                    return true;
                });
            if (!ok) {
                SKSE::log::warn("Impersonation session record is truncated; ignoring it.");
                return;
            }

            auto* donor = donorID != 0 ? RE::TESForm::LookupByID<RE::Actor>(donorID) : nullptr;
            ImpersonationManager::GetSingleton().RestoreSession(donor, puppet, std::move(aliases), std::move(questItems));
        }

        // --- 'AFAC': factions the donor's swapped aliases gave it ---

        void WriteDonorAliasFactions(Serde* a_serde) {
            const auto factions = AliasSwapManager::GetSingleton().GetDonorAliasFactions();
            if (!Session::GetSingleton().IsActive() || factions.empty()) return;
            if (!a_serde->OpenRecord(kDonorAliasFactionsRecord, kDonorAliasFactionsVersion)) return;
            WriteList(a_serde, factions, [&](const AliasSwapManager::FactionRank& a_entry) {
                WriteFormID(a_serde, a_entry.first);
                Write(a_serde, a_entry.second);
            });
        }

        void ReadDonorAliasFactions(Serde* a_serde) {
            std::vector<AliasSwapManager::FactionRank> factions;
            const bool ok = ReadList(a_serde, [&] {
                RE::TESFaction* faction = nullptr;
                std::int8_t rank = 0;
                if (!ReadForm(a_serde, faction) || !Read(a_serde, rank)) return false;
                if (faction) factions.emplace_back(faction, rank);
                return true;
            });
            if (!ok) {
                SKSE::log::warn("Donor alias faction record is truncated; ignoring it.");
                return;
            }
            SKSE::log::info("Loaded {} donor alias factions.", factions.size());
            AliasSwapManager::GetSingleton().SetDonorAliasFactions(std::move(factions));
        }

        // Whether this build can read a record saved at a_version (it reads a_current and every older
        // version back to kMinSupportedVersion)
        bool IsReadableVersion(std::uint32_t a_type, std::uint32_t a_version, std::uint32_t a_current) {
            if (a_version >= kMinSupportedVersion && a_version <= a_current) return true;
            SKSE::log::warn("Skipping record {:08X}: saved at version {}, this build reads versions {} to {}.",
                a_type, a_version, kMinSupportedVersion, a_current);
            return false;
        }
    }

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

    void SaveCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        auto& mgr = ImpersonationManager::GetSingleton();
        if (const auto& baseline = mgr.GetBaseline()) {
            WriteBaseline(a_serde, *baseline);
        }
        WriteSession(a_serde);
        if (const auto& donorFace = mgr.GetDonorFace(); donorFace && Session::GetSingleton().IsActive() &&
            a_serde->OpenRecord(kDonorFaceRecord, kDonorFaceVersion)) {
            WriteFaceSnapshot(a_serde, *donorFace);
        }
        WriteDonorAliasFactions(a_serde);

        SKSE::log::info("Completed SKSE Co-Save serialization write.");
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
                    ReadSession(a_serde);
                    break;

                case kDonorAliasFactionsRecord:
                    if (!IsReadableVersion(type, version, kDonorAliasFactionsVersion)) break;
                    ReadDonorAliasFactions(a_serde);
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
