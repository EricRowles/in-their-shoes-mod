#include "PCH.h"
#include "ImpersonationManager.h"
#include "ActorHooks.h"
#include "AliasSwapManager.h"
#include "FaceManager.h"
#include "Forms.h"
#include "Lifecycle.h"
#include "OutfitManager.h"
#include "Serialization.h"

namespace InTheirShoes {

    void ImpersonationManager::ForwardPuppetFactionsToDonor() {
        auto* puppet = GetPuppet();
        auto* donor = GetDonor();
        if (!puppet || !donor) return;

        auto* extraFaction = puppet->extraList.GetByType<RE::ExtraFactionChanges>();
        if (!extraFaction || extraFaction->factionChanges.empty()) return;

        for (const auto& fRank : extraFaction->factionChanges) {
            if (!fRank.faction) continue;
            donor->AddToFaction(fRank.faction, fRank.rank);
            SKSE::log::info("Forwarded faction {:08X} rank {} from puppet to donor {:08X}.",
                fRank.faction->GetFormID(), fRank.rank, donor->GetFormID());

            if (fRank.rank >= 0 &&
                (fRank.faction == Forms::currentFollowerFaction || fRank.faction == Forms::playerFollowerFaction)) {
                donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            }
        }
        extraFaction->factionChanges.clear();
    }

    void ImpersonationManager::AdoptPuppetAliases() {
        if (!IsActive()) return;
        AliasSwapManager::GetSingleton().AdoptPuppetAliases(GetPuppet(), GetPlayer());
    }

    void ImpersonationManager::Initialize() {
        if (Forms::storageMarker) {
            _storageMarker = Forms::storageMarker->GetHandle();
        }
    }

    void ImpersonationManager::AttachDonor(RE::Actor* a_donor) {
        _donorKeepAlive.reset(a_donor);
        auto* base = a_donor ? a_donor->GetActorBase() : nullptr;
        Session::GetSingleton().SetDonor(a_donor, base ? ActorHooks::GetNPCVoiceType(base) : nullptr);
    }

    void ImpersonationManager::DetachDonor() {
        Session::GetSingleton().SetDonor(nullptr, nullptr);
        _donorKeepAlive.reset();
    }

    void ImpersonationManager::AttachPuppet(RE::Actor* a_puppet) {
        _puppetKeepAlive.reset(a_puppet);
        Session::GetSingleton().SetPuppet(a_puppet);
    }

    void ImpersonationManager::SetBaseline(Baseline a_baseline) {
        _baseline = std::move(a_baseline);
        Session::GetSingleton().SetOrigPlayerVoiceType(_baseline->face.voiceType);
    }

    void ImpersonationManager::CaptureBaseline(RE::Actor* a_player) {
        Baseline baseline;
        FaceManager::GetSingleton().CaptureSnapshot(a_player, baseline.face);
        StatManager::CaptureSnapshot(a_player, baseline.stats);
        InventoryManager::StashPlayerSpells(a_player, baseline.spells);
        SetBaseline(std::move(baseline));
        SKSE::log::info("Captured Dragonborn baseline (name '{}').", _baseline->face.fullName.c_str());
    }

    bool ImpersonationManager::IsPlayerTransformed() {
        // The Werewolf and Vampire Lord quests run for the whole transformation, including the moments
        // around the race swap, and restore the race they saved when it began. (Confirmed for Vampire Lord: the
        // quest runs from the start of the transformation until ~2s after changing back.)
        return IsQuestRunning(Forms::playerWerewolfQuest) || IsQuestRunning(Forms::dlc1PlayerVampireQuest);
    }

    bool ImpersonationManager::Impersonate(RE::Actor* a_donor, RE::Actor* a_puppet) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_donor || !a_puppet || a_donor == player || a_donor == a_puppet) {
            SKSE::log::warn("Impersonate called with invalid actors.");
            return false;
        }
        // The baseline (or the session being handed back) would capture the beast race
        if (IsPlayerTransformed()) {
            SKSE::log::warn("Impersonate refused: the player is transformed.");
            return false;
        }

        auto& faces = FaceManager::GetSingleton();

        if (IsActive()) {
            // Donor-to-donor switch: hand the current state back to the old donor, who takes the player's place
            // (the quest script then moves the player to the new donor's spot)
            auto* oldDonor = GetDonor();
            if (oldDonor == a_donor) return false;
            EndSession();
            if (oldDonor) {
                ReturnStateToDonor(oldDonor);
                oldDonor->MoveTo(player);
                oldDonor->StopCombat();
                oldDonor->EvaluatePackage(false, false);
            }
        } else {
            // First impersonation: the puppet takes on the Dragonborn's look, stats and gear
            CaptureBaseline(player);
            AttachPuppet(a_puppet);
            ConfigurePuppet(a_puppet);
            StatManager::CopyStats(player, a_puppet);
            InventoryManager::TransferAllItems(player, a_puppet, true, _donorQuestItems);
        }

        // Kept for the session: the donor's rendered skin and hair tints can only be read while its 3D is
        // loaded, and on a later load it is parked out of sight
        FaceSnapshot donorFace;
        if (faces.CaptureSnapshot(a_donor, donorFace)) {
            faces.ApplySnapshot(player, donorFace);
            _donorFace = std::move(donorFace);
        } else {
            SKSE::log::error("Failed to capture the appearance of donor {:08X}.", a_donor->GetFormID());
            _donorFace.reset();
        }
        StatManager::CopyStats(a_donor, player);
        InventoryManager::CopyDonorSpells(a_donor, player);
        InventoryManager::TransferAllItems(a_donor, player, false, _donorQuestItems);

        if (auto marker = _storageMarker.get()) {
            a_donor->MoveTo(marker.get());
        }

        BeginSession(a_donor);
        return true;
    }

    bool ImpersonationManager::Revert() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !IsActive()) return false;
        // Restoring the baseline race mid-transformation would be undone (to the donor's race) when it ends
        if (IsPlayerTransformed()) {
            SKSE::log::warn("Revert refused: the player is transformed.");
            return false;
        }

        auto* donor = GetDonor();
        auto* puppet = GetPuppet();
        EndSession();
        if (donor) {
            ReturnStateToDonor(donor);
        }

        if (_baseline) {
            FaceManager::GetSingleton().ApplySnapshot(player, _baseline->face);
            StatManager::ApplySnapshot(player, _baseline->stats);
        }
        if (puppet) {
            InventoryManager::TransferAllItems(puppet, player, false, _donorQuestItems);
        }
        if (_baseline) {
            InventoryManager::RestorePlayerSpells(player, _baseline->spells);
        }

        ReleasePuppet();
        _baseline.reset();
        _donorFace.reset();
        Session::GetSingleton().SetOrigPlayerVoiceType(nullptr);
        _donorQuestItems.clear();
        SKSE::log::info("Reverted to the Dragonborn.");
        return true;
    }

    void ImpersonationManager::ReturnStateToDonor(RE::Actor* a_donor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        StatManager::CopyStats(player, a_donor);
        InventoryManager::TransferAllItems(player, a_donor, true, _donorQuestItems);
        InventoryManager::ReturnSpellsToDonor(player, a_donor);
        _donorQuestItems.clear();
    }

    void ImpersonationManager::BeginSession(RE::Actor* a_donor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        AttachDonor(a_donor);
        Session::GetSingleton().SetActive(true);

        SKSE::log::info("Beginning impersonation: Donor {:08X} ({})", a_donor->GetFormID(), a_donor->GetName());

        OutfitManager::GetSingleton().DisableOutfit(a_donor);
        AliasSwapManager::GetSingleton().SwapDonorAliasesToPlayer(a_donor, player);

        if (auto* puppet = GetPuppet()) {
            ApplyPuppetFollowState(puppet);
            puppet->EvaluatePackage(false, false);
        }

        Lifecycle::OnSessionBegin();
    }

    void ImpersonationManager::EndSession() {
        auto* donor = GetDonor();
        SKSE::log::info("Ending impersonation of donor {:08X}", donor ? donor->GetFormID() : 0u);

        ForwardPuppetFactionsToDonor();
        AliasSwapManager::GetSingleton().RestoreSwappedAliases(donor);

        Lifecycle::OnSessionEnd();

        Session::GetSingleton().SetActive(false);
        DetachDonor();
    }

    void ImpersonationManager::ApplyPuppetFollowState(RE::Actor* a_puppet) {
        // The follow package itself lives on the PlayerPuppet alias, gated by the ITS_PuppetFollows global.
        // Teammate status lets the puppet travel through load doors with the player.
        if (_puppetFollows) {
            a_puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
        } else {
            a_puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
        }
    }

    void ImpersonationManager::ConfigurePuppet(RE::Actor* a_puppet) {
        if (_baseline) {
            FaceManager::GetSingleton().ApplySnapshot(a_puppet, _baseline->face, true, true);
            // The puppet has usually just been moved in, and its 3D may still be loading from the old
            // base data; a 3D reset before it attaches is lost. Reapply once the 3D is in place.
            _puppetRefreshPending = true;
        }
        ApplyPuppetFollowState(a_puppet);
        a_puppet->AllowPCDialogue(true);
        a_puppet->AllowBleedoutDialogue(true);
        a_puppet->StopCombat();
        a_puppet->EvaluatePackage(true, true);
        SKSE::log::info("Configured puppet {:08X} (followsPlayer={}).", a_puppet->GetFormID(), _puppetFollows);
    }

    void ImpersonationManager::ReleasePuppet() {
        if (auto* puppet = GetPuppet()) {
            puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
            puppet->EvaluatePackage(false, false);
        }
        AttachPuppet(nullptr);
    }

    void ImpersonationManager::SetPuppetFollowsPlayer(bool a_follows) {
        _puppetFollows = a_follows;
        auto* puppet = GetPuppet();
        if (!puppet || !IsActive()) return;

        ApplyPuppetFollowState(puppet);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (a_follows && player &&
            (puppet->GetParentCell() != player->GetParentCell() || puppet->GetPosition().GetDistance(player->GetPosition()) > 1000.0f)) {
            puppet->MoveTo(player);
            SKSE::log::info("SetPuppetFollowsPlayer: Teleported puppet to player.");
        }
        puppet->EvaluatePackage(true, true);
        SKSE::log::info("Updated puppet follows player: {}", a_follows);
    }

    void ImpersonationManager::RefreshPuppetAppearanceWhenLoaded() {
        auto* puppet = GetPuppet();
        if (!puppet || !_baseline) {
            _puppetRefreshPending = false;
            return;
        }
        if (!puppet->Is3DLoaded()) return;

        _puppetRefreshPending = false;
        FaceManager::GetSingleton().ApplySnapshot(puppet, _baseline->face, true, true);
        SKSE::log::info("Reapplied Dragonborn appearance to puppet {:08X} after its 3D loaded.", puppet->GetFormID());
    }

    void ImpersonationManager::Tick() {
        if (_puppetRefreshPending) {
            RefreshPuppetAppearanceWhenLoaded();
        }

        if (!IsActive() || _leashQueued) return;

        auto* donor = GetDonor();
        auto marker = _storageMarker.get();
        if (!donor || !marker) return;

        bool needsLeash = donor->GetParentCell() != marker->GetParentCell() ||
                          donor->GetPosition().GetDistance(marker->GetPosition()) > 500.0f;
        if (!needsLeash) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        auto* cell = player->GetParentCell();
        bool worldReady = cell && cell->IsAttached() && player->Get3D() &&
                          !(ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
        if (!worldReady) return;

        SKSE::log::info("Tick: Donor {:08X} moved away from holding marker; leashing back.", donor->GetFormID());
        _leashQueued = true;
        SKSE::GetTaskInterface()->AddTask([donorHandle = donor->GetHandle(), markerHandle = _storageMarker]() {
            ImpersonationManager::GetSingleton()._leashQueued = false;
            auto d = donorHandle.get();
            auto m = markerHandle.get();
            if (d && m) {
                d->MoveTo(m.get());
            }
        });
    }

    void ImpersonationManager::SaveSession(SKSE::SerializationInterface* a_serde) const {
        auto* donor = GetDonor();
        auto* puppet = GetPuppet();
        if (!IsActive() || !donor) return;
        if (!a_serde->OpenRecord(Serialization::kSessionRecord, Serialization::kSessionVersion)) return;

        const RE::FormID donorID = donor->GetFormID();
        const RE::FormID puppetID = puppet ? puppet->GetFormID() : 0;
        a_serde->WriteRecordData(&donorID, sizeof(donorID));
        a_serde->WriteRecordData(&puppetID, sizeof(puppetID));

        auto& aliases = AliasSwapManager::GetSingleton();
        aliases.WriteSwappedAliases(a_serde);

        const auto itemCount = static_cast<std::uint32_t>(_donorQuestItems.size());
        a_serde->WriteRecordData(&itemCount, sizeof(itemCount));
        for (auto id : _donorQuestItems) {
            a_serde->WriteRecordData(&id, sizeof(id));
        }
        SKSE::log::info("Saved impersonation session ({} swapped aliases, {} donor quest items).",
            aliases.GetSwappedAliasCount(), itemCount);
    }

    void ImpersonationManager::LoadSession(SKSE::SerializationInterface* a_serde) {
        RE::FormID donorID = 0;
        RE::FormID puppetID = 0;
        std::vector<AliasSwapManager::SwappedAlias> aliases;
        if (!a_serde->ReadRecordData(&donorID, sizeof(donorID)) ||
            !a_serde->ReadRecordData(&puppetID, sizeof(puppetID)) ||
            !AliasSwapManager::ReadSwappedAliases(a_serde, aliases)) {
            SKSE::log::warn("Impersonation session record is truncated; ignoring it.");
            return;
        }

        std::uint32_t itemCount = 0;
        if (a_serde->ReadRecordData(&itemCount, sizeof(itemCount))) {
            for (std::uint32_t i = 0; i < itemCount; ++i) {
                RE::FormID id = 0;
                if (a_serde->ReadRecordData(&id, sizeof(id)) && a_serde->ResolveFormID(id, id)) {
                    _donorQuestItems.insert(id);
                }
            }
        }

        if (puppetID != 0 && a_serde->ResolveFormID(puppetID, puppetID)) {
            AttachPuppet(RE::TESForm::LookupByID<RE::Actor>(puppetID));
        }

        RE::Actor* donor = nullptr;
        if (a_serde->ResolveFormID(donorID, donorID)) {
            donor = RE::TESForm::LookupByID<RE::Actor>(donorID);
        }
        if (!donor) {
            SKSE::log::warn("Impersonation session donor {:08X} no longer exists; session not restored.", donorID);
            return;
        }

        AttachDonor(donor);
        const auto aliasCount = aliases.size();
        AliasSwapManager::GetSingleton().SetSwappedAliases(std::move(aliases));
        Session::GetSingleton().SetActive(true);
        SKSE::log::info("Restored impersonation session: Donor {:08X} ({} swapped aliases)", donorID, aliasCount);
    }

    void ImpersonationManager::OnRevert() {
        // Put the in-memory player record back before another save loads over it
        if (_baseline) {
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetActorBase()) {
                FaceManager::GetSingleton().ApplySnapshot(player, _baseline->face, false);
            }
        }

        Session::GetSingleton().SetActive(false);
        DetachDonor();
        AttachPuppet(nullptr);
        _donorQuestItems.clear();
        _baseline.reset();
        _donorFace.reset();
        Session::GetSingleton().SetOrigPlayerVoiceType(nullptr);
        _leashQueued = false;
        _puppetRefreshPending = false;
    }

    void ImpersonationManager::OnPostLoadGame() {
        if (!IsActive()) return;

        if (auto* donor = GetDonor()) {
            // The player's record may have reloaded with stale appearance data; reapply the donor's look as
            // captured when impersonation began. Re-capturing now would miss the donor's rendered skin and hair
            // tints, since the parked donor has no 3D. (If the snapshot couldn't be captured, re-capture anyway.)
            if (_donorFace) {
                SKSE::GetTaskInterface()->AddTask([face = *_donorFace]() {
                    FaceManager::GetSingleton().ApplySnapshot(RE::PlayerCharacter::GetSingleton(), face);
                });
            } else {
                SKSE::GetTaskInterface()->AddTask([donorHandle = donor->GetHandle()]() {
                    if (auto d = donorHandle.get()) {
                        FaceManager::GetSingleton().CopyAppearance(d.get(), RE::PlayerCharacter::GetSingleton());
                    }
                });
            }
        }

        // The puppet's base record is not saved, so its appearance must be reapplied every load
        if (auto* puppet = GetPuppet()) {
            ConfigurePuppet(puppet);
        }
    }

}
