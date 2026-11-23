#include "PCH.h"
#include "SessionStash.h"
#include "AliasSwapManager.h"
#include "Forms.h"
#include "GameUtil.h"

namespace InTheirShoes {

    using namespace Serialization;

    // --- Factions ---

    namespace {
        using FactionRank = FactionStash::FactionRank;

        // Whether the faction moves between the Dragonborn and the donor (see FactionStash)
        bool IsSwappable(const RE::TESFaction* a_faction) {
            return a_faction && a_faction != Forms::playerFaction && !AliasSwapManager::GetSingleton()->IsAliasFaction(a_faction);
        }

        std::vector<FactionRank> GetSwappableMemberships(RE::Actor* a_actor) {
            std::vector<FactionRank> result;
            for (const auto& [faction, rank] : GetFactionMemberships(a_actor)) {
                if (IsSwappable(faction)) result.emplace_back(faction, rank);
            }
            return result;
        }

        bool Contains(const std::vector<FactionRank>& a_list, const RE::TESFaction* a_faction) {
            return std::ranges::contains(a_list, a_faction, &FactionRank::first);
        }
    }

    void FactionStash::OnBegin(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
        auto dragonborn = GetSwappableMemberships(a_player);
        // The aliases have moved, so the donor's memberships no longer include their factions
        _given = GetSwappableMemberships(a_donor);
        for (const auto& [faction, rank] : dragonborn) {
            if (!Contains(_given, faction)) a_player->RemoveFromFaction(faction);
        }
        for (const auto& [faction, rank] : _given) {
            a_player->AddToFaction(faction, rank);
        }
        SKSE::log::info("Player took on {} donor factions; {} of the Dragonborn's set aside.", _given.size(), dragonborn.size());
        std::scoped_lock lock(_lock);
        _dragonborn = std::move(dragonborn);
    }

    void FactionStash::OnEnd(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
        const auto current = GetSwappableMemberships(a_player);
        if (a_donor) {
            const auto donorRanks = GetFactionMemberships(a_donor);
            for (const auto& [faction, rank] : current) {
                if (auto it = donorRanks.find(faction); it == donorRanks.end() || it->second != rank) {
                    a_donor->AddToFaction(faction, rank);
                }
            }
            for (const auto& [faction, rank] : _given) {
                if (!Contains(current, faction)) a_donor->RemoveFromFaction(faction);
            }
        }
        for (const auto& [faction, rank] : current) {
            if (!Contains(_dragonborn, faction)) a_player->RemoveFromFaction(faction);
        }
        for (const auto& [faction, rank] : _dragonborn) {
            a_player->AddToFaction(faction, rank);
        }
        // A script's RemoveFromAllFactions on the donor ran on the player (NativeRedirect)
        if (Forms::playerFaction && !a_player->IsInFaction(Forms::playerFaction)) {
            a_player->AddToFaction(Forms::playerFaction, 0);
        }
        SKSE::log::info("Returned {} factions to the donor and {} to the Dragonborn.", current.size(), _dragonborn.size());
    }

    std::optional<std::int8_t> FactionStash::GetDragonbornFactionRank(const RE::TESFaction* a_faction) const {
        // The player keeps PlayerFaction, so their own membership answers for it
        if (!a_faction || a_faction == Forms::playerFaction || !IsActive()) return std::nullopt;
        std::scoped_lock lock(_lock);
        auto it = std::ranges::find(_dragonborn, a_faction, &FactionRank::first);
        return it != _dragonborn.end() ? it->second : std::int8_t{ -1 };
    }

    void FactionStash::OnClear() {
        std::scoped_lock lock(_lock);
        _dragonborn.clear();
        _given.clear();
    }

    void FactionStash::Save(Serde* a_serde) const {
        WriteFactionRanks(a_serde, _dragonborn);
        WriteFactionRanks(a_serde, _given);
    }

    bool FactionStash::OnLoad(Serde* a_serde) {
        std::vector<FactionRank> dragonborn;
        std::vector<FactionRank> given;
        if (!ReadFactionRanks(a_serde, dragonborn) || !ReadFactionRanks(a_serde, given)) return false;
        std::scoped_lock lock(_lock);
        _dragonborn = std::move(dragonborn);
        _given = std::move(given);
        return true;
    }

    // --- Linked references ---

    namespace {
        // Packages, conditions and scripts that ask an actor for its linked reference (a UseIdleMarker package going
        // to "my linked ref with keyword X") find the donor's on the player
        class LinkedRefStash : public SessionStash {
        public:
            LinkedRefStash() : SessionStash('LINK', 1, "linked references") {}

            void Save(Serde* a_serde) const override {
                WriteList(a_serde, _dragonborn, [&](const Link& a_link) {
                    WriteForm(a_serde, a_link.keyword);
                    WriteForm(a_serde, a_link.ref);
                });
            }

        protected:
            void OnBegin(RE::Actor* a_donor, RE::PlayerCharacter* a_player) override {
                // A switch to another donor ends the last session first, which put the Dragonborn's links back
                _dragonborn = GetLinks(a_player);
                // Read from the donor once: its placed reference's links (the ones packages use) don't change on their
                // own. A script that changes them mid-session changes the player's instead when NativeRedirect is on.
                const auto given = GetLinks(a_donor);
                ReplaceLinks(a_player, given);
                SKSE::log::info("Player took on {} donor linked refs ({} of the Dragonborn's set aside).", given.size(), _dragonborn.size());
            }

            void OnEnd(RE::Actor* a_donor, RE::PlayerCharacter* a_player) override {
                // The player has had only the donor's links since the session began, so all of its links now are the
                // donor's: a script that relinked the donor (through NativeRedirect) sticks
                const auto current = GetLinks(a_player);
                if (a_donor) ReplaceLinks(a_donor, current);
                ReplaceLinks(a_player, _dragonborn);
                SKSE::log::info("Returned {} linked refs to the donor and {} to the Dragonborn.", current.size(), _dragonborn.size());
            }

            void OnClear() override { _dragonborn.clear(); }

            bool OnLoad(Serde* a_serde) override {
                std::vector<Link> links;
                const bool ok = ReadList(a_serde, [&] {
                    Link link;
                    RE::FormID keywordID = 0;
                    // A keyword saved as none is the default link; one whose plugin is gone drops the link
                    if (!Read(a_serde, keywordID)) return false;
                    const bool keywordGone = keywordID != 0 && !a_serde->ResolveFormID(keywordID, keywordID);
                    link.keyword = keywordID != 0 && !keywordGone ? RE::TESForm::LookupByID<RE::BGSKeyword>(keywordID) : nullptr;
                    if (!ReadForm(a_serde, link.ref)) return false;
                    if (link.ref && !keywordGone && (keywordID == 0 || link.keyword)) links.push_back(link);
                    return true;
                });
                if (ok) _dragonborn = std::move(links);
                return ok;
            }

        private:
            struct Link {
                RE::BGSKeyword* keyword{ nullptr };  // null for the reference's default link
                RE::TESObjectREFR* ref{ nullptr };
            };

            // A reference's links, one per keyword. Targets are resolved through GetLinkedRef, which also finds a
            // target the extra data holds only by form ID; one that doesn't resolve is left out.
            static std::vector<Link> GetLinks(RE::TESObjectREFR* a_refr) {
                std::vector<RE::BGSKeyword*> keywords;
                if (auto* extra = a_refr->extraList.GetByType<RE::ExtraLinkedRef>()) {
                    for (const auto& entry : extra->linkedRefs) {
                        if (!std::ranges::contains(keywords, entry.keyword)) keywords.push_back(entry.keyword);
                    }
                }
                std::vector<Link> links;
                for (auto* keyword : keywords) {
                    if (auto* ref = a_refr->GetLinkedRef(keyword)) links.push_back({ keyword, ref });
                }
                return links;
            }

            static const Link* Find(const std::vector<Link>& a_links, const RE::BGSKeyword* a_keyword) {
                auto it = std::ranges::find(a_links, a_keyword, &Link::keyword);
                return it != a_links.end() ? &*it : nullptr;
            }

            // Only links that differ are written, so a reference whose links already match isn't marked changed. The
            // engine's SetLinkedRef (what Papyrus calls) keeps the target's linked-children list in step, and a null
            // target removes the link.
            static void ReplaceLinks(RE::TESObjectREFR* a_refr, const std::vector<Link>& a_links) {
                const auto current = GetLinks(a_refr);
                for (const auto& link : current) {
                    if (!Find(a_links, link.keyword)) a_refr->extraList.SetLinkedRef(nullptr, link.keyword);
                }
                for (const auto& link : a_links) {
                    const auto* existing = Find(current, link.keyword);
                    if (!existing || existing->ref != link.ref) a_refr->extraList.SetLinkedRef(link.ref, link.keyword);
                }
            }

            // The Dragonborn's links when the session began
            std::vector<Link> _dragonborn;
        };

        // --- Bounty ---

        // Crime gold lives on the player, so without this the donor would be wanted for the Dragonborn's crimes and
        // the Dragonborn would carry the donor's. Each session starts with no bounty; when it ends, what the donor ran
        // up is dropped and the Dragonborn's comes back.
        class BountyStash : public SessionStash {
        public:
            BountyStash() : SessionStash('CRIM', 1, "bounty") {}

            void Save(Serde* a_serde) const override {
                WriteList(a_serde, _dragonborn, [&](const FactionBounty& a_entry) {
                    WriteForm(a_serde, a_entry.faction);
                    Write(a_serde, a_entry.gold);
                });
            }

        protected:
            void OnBegin(RE::Actor*, RE::PlayerCharacter* a_player) override {
                // A switch to another donor ends the last session first, which put the Dragonborn's bounty back
                _dragonborn.clear();
                for (const auto& entry : GetCrimeGoldMap(a_player)) {
                    if (entry.first) _dragonborn.push_back({ const_cast<RE::TESFaction*>(entry.first), entry.second });
                }
                ClearAll(a_player);
                SKSE::log::info("Set aside the Dragonborn's bounty in {} crime factions.", _dragonborn.size());
            }

            void OnEnd(RE::Actor*, RE::PlayerCharacter* a_player) override {
                ClearAll(a_player);
                auto& map = GetCrimeGoldMap(a_player);
                for (const auto& [faction, gold] : _dragonborn) {
                    if (auto it = map.find(faction); it != map.end()) {
                        it->second = gold;
                    } else {
                        map.emplace(faction, gold);
                    }
                }
                SKSE::log::info("Restored the Dragonborn's bounty in {} crime factions.", _dragonborn.size());
            }

            void OnClear() override { _dragonborn.clear(); }

            bool OnLoad(Serde* a_serde) override {
                std::vector<FactionBounty> bounty;
                const bool ok = ReadList(a_serde, [&] {
                    FactionBounty entry;
                    if (!ReadForm(a_serde, entry.faction) || !Read(a_serde, entry.gold)) return false;
                    if (entry.faction) bounty.push_back(entry);
                    return true;
                });
                if (ok) _dragonborn = std::move(bounty);
                return ok;
            }

        private:
            struct FactionBounty {
                RE::TESFaction* faction{ nullptr };
                RE::CrimeGoldStruct gold{};
            };

            // Current bounty (violent and non-violent) and infamy, per crime faction. The values are written directly,
            // like a save loading them, rather than through Actor::SetCrimeGoldValue, which only sets the current bounty.
            static auto& GetCrimeGoldMap(RE::PlayerCharacter* a_player) { return a_player->GetCrimeValue().crimeGoldMap; }

            static void ClearAll(RE::PlayerCharacter* a_player) {
                for (auto& entry : GetCrimeGoldMap(a_player)) {
                    entry.second = {};
                }
            }

            std::vector<FactionBounty> _dragonborn;
        };

        FactionStash s_factions;
        LinkedRefStash s_linkedRefs;
        BountyStash s_bounty;
        // Linked refs before idle AI re-evaluates the player's package, whose target may be one
        constexpr std::array<SessionStash*, 3> kAll{ &s_factions, &s_linkedRefs, &s_bounty };
    }

    std::span<SessionStash* const> SessionStash::All() {
        return kAll;
    }

    FactionStash& SessionStashes::Factions() {
        return s_factions;
    }

}
