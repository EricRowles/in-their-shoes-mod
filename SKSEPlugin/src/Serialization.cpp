#include "PCH.h"
#include "Serialization.h"
#include "AliasSwapManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "ImpersonationManager.h"
#include "Lifecycle.h"
#include "SessionStash.h"
#include "Settings.h"

namespace InTheirShoes::Serialization {

    namespace {
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
            const bool faceIsPlayer = Forms::IsPlayerBase(a_face.faceNPC);
            WriteForm(a_serde, a_face.race);
            WriteForm(a_serde, faceIsPlayer ? nullptr : a_face.faceNPC);
            WriteForm(a_serde, a_face.hairColor);
            WriteForm(a_serde, a_face.voiceType);
            Write(a_serde, a_face.bodyTintColor);
            Write(a_serde, a_face.height);
            Write(a_serde, a_face.scale);
            Write(a_serde, a_face.weight);
            Write(a_serde, static_cast<std::uint8_t>(a_face.sex));
            Write(a_serde, a_face.morphs);
            Write(a_serde, a_face.parts);
            WriteList(a_serde, a_face.headParts, [&](const RE::BGSHeadPart* a_part) { WriteForm(a_serde, a_part); });
            Write(a_serde, a_face.hasHairTintColor);
            Write(a_serde, a_face.hairTintColor);
            WriteList(a_serde, a_face.tintLayers, [&](const RE::TESNPC::Layer& a_layer) { Write(a_serde, a_layer); });
            Write(a_serde, a_face.hasSkinTintColor);
            Write(a_serde, a_face.skinTintColor);
            WriteString(a_serde, a_face.fullName.c_str());
            WriteForm(a_serde, a_face.faceDetails);
            WriteForm(a_serde, a_face.skin);
            WriteForm(a_serde, a_face.farSkin);
        }

        bool ReadFaceSnapshot(Serde* a_serde, FaceSnapshot& a_out) {
            std::uint8_t sex = 0;
            const bool ok =
                ReadForm(a_serde, a_out.race) &&
                ReadForm(a_serde, a_out.faceNPC) &&
                ReadForm(a_serde, a_out.hairColor) &&
                ReadForm(a_serde, a_out.voiceType) &&
                Read(a_serde, a_out.bodyTintColor) &&
                Read(a_serde, a_out.height) &&
                Read(a_serde, a_out.scale) &&
                Read(a_serde, a_out.weight) &&
                Read(a_serde, sex) &&
                Read(a_serde, a_out.morphs) &&
                Read(a_serde, a_out.parts) &&
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
            a_out.sex = static_cast<RE::SEX>(sex);
            return ok;
        }

        // --- 'BASE': the Dragonborn's face, stats, spells and character progress ---

        void WriteBaseline(Serde* a_serde, const Baseline& a_baseline) {
            WriteFaceSnapshot(a_serde, a_baseline.face);
            WriteList(a_serde, a_baseline.stats.baseValues, [&](const auto& a_entry) {
                Write(a_serde, a_entry.first);
                Write(a_serde, a_entry.second);
            });
            const auto& spells = a_baseline.spells;
            WriteFormIDs(a_serde, spells.addedSpells);
            WriteForm(a_serde, spells.leftSpell);
            WriteForm(a_serde, spells.rightSpell);
            WriteForm(a_serde, spells.shout);
            WriteFormIDs(a_serde, spells.abilities);
            const auto& progress = a_baseline.progress;
            Write(a_serde, progress.level);
            Write(a_serde, progress.perkPoints);
            Write(a_serde, progress.skills);
            WriteFormIDs(a_serde, progress.perks);
        }

        bool ReadBaseline(Serde* a_serde, Baseline& a_out) {
            auto& spells = a_out.spells;
            auto& progress = a_out.progress;
            return ReadFaceSnapshot(a_serde, a_out.face) &&
                   ReadList(a_serde, [&] {
                       std::uint32_t av = 0;
                       float value = 0.0f;
                       if (!Read(a_serde, av) || !Read(a_serde, value)) return false;
                       a_out.stats.baseValues[av] = value;
                       return true;
                   }) &&
                   ReadFormIDs(a_serde, spells.addedSpells) &&
                   ReadForm(a_serde, spells.leftSpell) &&
                   ReadForm(a_serde, spells.rightSpell) &&
                   ReadForm(a_serde, spells.shout) &&
                   ReadFormIDs(a_serde, spells.abilities) &&
                   Read(a_serde, progress.level) &&
                   Read(a_serde, progress.perkPoints) &&
                   Read(a_serde, progress.skills) &&
                   ReadFormIDs(a_serde, progress.perks);
        }

        // --- 'SESS': donor, puppet, swapped aliases and donor quest items carried by the player ---

        void WriteSession(Serde* a_serde) {
            auto* donor = Session::GetSingleton()->GetDonor();
            WriteForm(a_serde, donor);
            WriteForm(a_serde, Session::GetSingleton()->GetPuppet());
            WriteList(a_serde, AliasSwapManager::GetSingleton()->GetSwappedAliases(), [&](const AliasSwapManager::SwappedAlias& a_alias) {
                Write(a_serde, a_alias.questFormID);
                Write(a_serde, a_alias.aliasID);
                WriteFactionRanks(a_serde, a_alias.factions);
            });
            const auto& questItems = ImpersonationManager::GetSingleton()->GetDonorQuestItems();
            WriteList(a_serde, questItems, [&](RE::FormID a_id) { Write(a_serde, a_id); });
        }

        bool ReadSession(Serde* a_serde) {
            RE::FormID donorID = 0;
            RE::Actor* puppet = nullptr;
            std::vector<AliasSwapManager::SwappedAlias> aliases;
            std::vector<RE::FormID> questItems;
            const bool ok =
                ReadFormID(a_serde, donorID) &&
                ReadForm(a_serde, puppet) &&
                ReadList(a_serde, [&] {
                    AliasSwapManager::SwappedAlias alias;
                    if (!ReadFormID(a_serde, alias.questFormID) || !Read(a_serde, alias.aliasID) ||
                        !ReadFactionRanks(a_serde, alias.factions)) {
                        return false;
                    }
                    if (alias.questFormID != 0) aliases.push_back(alias);
                    return true;
                }) &&
                ReadFormIDs(a_serde, questItems);
            if (!ok) return false;

            auto* donor = donorID != 0 ? RE::TESForm::LookupByID<RE::Actor>(donorID) : nullptr;
            ImpersonationManager::GetSingleton()->RestoreSession(donor, puppet, std::move(aliases),
                std::unordered_set<RE::FormID>(questItems.begin(), questItems.end()));
            return true;
        }

        // --- 'SETT': the MCM's settings ---

        void WriteSettings(Serde* a_serde) {
            const auto& settings = *Settings::GetSingleton();
            for (std::size_t i = 0; i < Settings::kCount; ++i) {
                Write(a_serde, settings.Get(static_cast<Settings::Id>(i)));
            }
        }

        bool ReadSettings(Serde* a_serde) {
            std::array<float, Settings::kCount> values{};
            for (auto& value : values) {
                if (!Read(a_serde, value)) return false;
            }
            auto& settings = *Settings::GetSingleton();
            for (std::size_t i = 0; i < Settings::kCount; ++i) {
                settings.Set(static_cast<Settings::Id>(i), values[i]);
            }
            return true;
        }

        // --- Callbacks ---

        void SaveCallback(Serde* a_serde) {
            if (a_serde->OpenRecord(kSettingsRecord, kSettingsVersion)) {
                WriteSettings(a_serde);
            }
            auto& mgr = *ImpersonationManager::GetSingleton();
            if (const auto& baseline = mgr.GetBaseline(); baseline && a_serde->OpenRecord(kBaselineRecord, kBaselineVersion)) {
                WriteBaseline(a_serde, *baseline);
            }
            const auto& session = *Session::GetSingleton();
            if (!session.IsActive() || !session.GetDonor()) return;
            if (a_serde->OpenRecord(kSessionRecord, kSessionVersion)) {
                WriteSession(a_serde);
            }
            if (const auto& donorFace = mgr.GetDonorFace(); donorFace && a_serde->OpenRecord(kDonorFaceRecord, kDonorFaceVersion)) {
                WriteFaceSnapshot(a_serde, *donorFace);
            }
            for (const auto* stash : SessionStash::All()) {
                if (stash->IsActive() && a_serde->OpenRecord(stash->record, stash->version)) {
                    stash->Save(a_serde);
                }
            }
            SKSE::log::info("Saved the impersonation session.");
        }

        // Reads a record saved at a_version with a_read, if this build reads that version
        template <class ReadRecord>
        void LoadRecord(std::uint32_t a_version, std::uint32_t a_current, std::string_view a_name, ReadRecord&& a_read) {
            if (a_version != a_current) {
                SKSE::log::warn("Skipped the {} record: saved at version {}, this build reads version {}.", a_name, a_version, a_current);
            } else if (!a_read()) {
                SKSE::log::warn("The {} record is truncated; ignored it.", a_name);
            } else {
                SKSE::log::info("Loaded the {} record.", a_name);
            }
        }

        void LoadCallback(Serde* a_serde) {
            Lifecycle::OnGameRevert();

            std::uint32_t type = 0;
            std::uint32_t version = 0;
            std::uint32_t length = 0;
            while (a_serde->GetNextRecordInfo(type, version, length)) {
                switch (type) {
                    case kSettingsRecord:
                        LoadRecord(version, kSettingsVersion, "settings"sv, [&] { return ReadSettings(a_serde); });
                        break;
                    case kBaselineRecord:
                        LoadRecord(version, kBaselineVersion, "Dragonborn baseline"sv, [&] {
                            Baseline baseline;
                            if (!ReadBaseline(a_serde, baseline)) return false;
                            ImpersonationManager::GetSingleton()->SetBaseline(std::move(baseline));
                            return true;
                        });
                        break;
                    case kSessionRecord:
                        LoadRecord(version, kSessionVersion, "session"sv, [&] { return ReadSession(a_serde); });
                        break;
                    case kDonorFaceRecord:
                        LoadRecord(version, kDonorFaceVersion, "donor appearance"sv, [&] {
                            FaceSnapshot donorFace;
                            if (!ReadFaceSnapshot(a_serde, donorFace)) return false;
                            ImpersonationManager::GetSingleton()->SetDonorFace(std::move(donorFace));
                            return true;
                        });
                        break;
                    default: {
                        const auto stashes = SessionStash::All();
                        auto it = std::ranges::find(stashes, type, &SessionStash::record);
                        if (it != stashes.end()) {
                            auto* stash = *it;
                            LoadRecord(version, stash->version, stash->name, [&] { return stash->Load(a_serde); });
                        } else {
                            SKSE::log::warn("Unknown record type {:08X} in co-save", type);
                        }
                        break;
                    }
                }
            }
        }

        void RevertCallback(Serde*) {
            Lifecycle::OnGameRevert();
        }
    }

    void Register() {
        auto* serde = SKSE::GetSerializationInterface();
        if (!serde) {
            SKSE::log::error("Failed to obtain SKSE Serialization Interface!");
            return;
        }
        serde->SetUniqueID(kUniqueID);
        serde->SetSaveCallback(SaveCallback);
        serde->SetLoadCallback(LoadCallback);
        serde->SetRevertCallback(RevertCallback);
    }

}
