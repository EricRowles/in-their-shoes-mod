#include "PCH.h"
#include "LinkedRefManager.h"

namespace InTheirShoes {

    namespace {
        using Link = LinkedRefManager::Link;

        // A reference's links, one per keyword. Targets are resolved through GetLinkedRef, which also finds a
        // target the extra data holds only by form ID; one that doesn't resolve is left out.
        std::vector<Link> GetLinks(RE::TESObjectREFR* a_refr) {
            // Main thread, where the engine changes links too
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

        const Link* Find(const std::vector<Link>& a_links, const RE::BGSKeyword* a_keyword) {
            auto it = std::ranges::find(a_links, a_keyword, &Link::keyword);
            return it != a_links.end() ? &*it : nullptr;
        }

        // The engine's SetLinkedRef (what Papyrus calls): keeps the target's linked-children list in step, and a
        // null target removes the link. The player's links are saved with the player like any reference's.
        void SetLink(RE::TESObjectREFR* a_refr, RE::TESObjectREFR* a_target, RE::BGSKeyword* a_keyword) {
            a_refr->extraList.SetLinkedRef(a_target, a_keyword);
        }

        // Only links that differ are written, so a reference whose links already match isn't marked changed
        void ReplaceLinks(RE::TESObjectREFR* a_refr, const std::vector<Link>& a_links) {
            const auto current = GetLinks(a_refr);
            for (const auto& link : current) {
                if (!Find(a_links, link.keyword)) SetLink(a_refr, nullptr, link.keyword);
            }
            for (const auto& link : a_links) {
                const auto* existing = Find(current, link.keyword);
                if (!existing || existing->ref != link.ref) SetLink(a_refr, link.ref, link.keyword);
            }
        }

        std::uint32_t KeywordID(const RE::BGSKeyword* a_keyword) {
            return a_keyword ? a_keyword->GetFormID() : 0;
        }
    }

    void LinkedRefManager::OnSessionBegin(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
        if (!a_donor || !a_player) return;
        // A switch to another donor ends the last session first, which put the Dragonborn's links back
        _dragonborn = GetLinks(a_player);
        // Read from the donor once: its placed reference's links (the ones packages use) don't change on their
        // own. A script that changes them mid-session changes the player's instead when NativeRedirect is on.
        const auto given = GetLinks(a_donor);
        ReplaceLinks(a_player, given);
        _active = true;
        for (const auto& link : given) {
            SKSE::log::info("  player linked to {:08X} (keyword {:08X})", link.ref->GetFormID(), KeywordID(link.keyword));
        }
        SKSE::log::info("Player took on {} donor linked refs ({} of the Dragonborn's set aside).", given.size(), _dragonborn.size());
    }

    void LinkedRefManager::OnSessionEnd(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
        if (!_active || !a_player) {
            Clear();
            return;
        }
        // The player has had only the donor's links since the session began, so all of its links now are the
        // donor's: a script that relinked the donor (through NativeRedirect) sticks
        const auto current = GetLinks(a_player);
        if (a_donor) {
            ReplaceLinks(a_donor, current);
        }
        ReplaceLinks(a_player, _dragonborn);
        SKSE::log::info("Returned {} linked refs to the donor and {} to the Dragonborn.", current.size(), _dragonborn.size());
        Clear();
    }

    void LinkedRefManager::OnPostLoadGame(RE::Actor* a_donor, RE::PlayerCharacter* a_player) {
        if (_active) return;
        SKSE::log::info("Session has no saved linked refs; the player takes on the donor's now.");
        OnSessionBegin(a_donor, a_player);
    }

    void LinkedRefManager::Clear() {
        _active = false;
        _dragonborn.clear();
    }

    void LinkedRefManager::Restore(std::vector<Link> a_dragonborn) {
        _dragonborn = std::move(a_dragonborn);
        _active = true;
    }

}
