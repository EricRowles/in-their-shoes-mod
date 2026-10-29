#include "PCH.h"
#include "OutfitManager.h"

namespace InTheirShoes {

    void OutfitManager::InstallHook() {
        // Actor::AddWornOutfit isn't virtual. Its callers (inventory init when an actor loads, and Papyrus
        // SetOutfit) are found by scanning the code for direct calls to it, so no per-version offsets are needed.
        const auto target = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(19266, 19692) }.address();
        const auto text = REL::Module::get().segment(REL::Segment::textx);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.address());

        std::vector<std::uintptr_t> sites;
        for (std::size_t i = 0; i + 5 <= text.size(); ++i) {
            if (bytes[i] != 0xE8) continue;
            std::int32_t rel = 0;
            std::memcpy(&rel, bytes + i + 1, sizeof(rel));
            if (text.address() + i + 5 + rel == target) {
                sites.push_back(text.address() + i);
            }
        }

        auto& trampoline = SKSE::GetTrampoline();
        for (auto site : sites) {
            _origAddWornOutfit = trampoline.write_call<5>(site, &Hook_AddWornOutfit);
        }
        if (sites.empty()) {
            SKSE::log::warn("Found no calls to Actor::AddWornOutfit; donors' outfits will not be disabled");
        } else {
            SKSE::log::info("Hooked {} call sites of Actor::AddWornOutfit", sites.size());
        }
    }

    void OutfitManager::Hook_AddWornOutfit(RE::Actor* a_this, RE::BGSOutfit* a_outfit, bool a_forceUpdate) {
        if (a_this && GetSingleton().IsOutfitDisabled(a_this)) {
            return;
        }
        _origAddWornOutfit(a_this, a_outfit, a_forceUpdate);
    }

    void OutfitManager::DisableOutfit(const RE::Actor* a_actor) {
        std::scoped_lock lock(_lock);
        _outfitDisabled.insert(a_actor->GetFormID());
    }

    bool OutfitManager::IsOutfitDisabled(const RE::Actor* a_actor) const {
        std::scoped_lock lock(_lock);
        return _outfitDisabled.contains(a_actor->GetFormID());
    }

    void OutfitManager::Clear() {
        std::scoped_lock lock(_lock);
        _outfitDisabled.clear();
    }

    std::unordered_set<RE::FormID> OutfitManager::GetOutfitDisabledIDs() const {
        std::scoped_lock lock(_lock);
        return _outfitDisabled;
    }

    void OutfitManager::SetOutfitDisabledIDs(std::unordered_set<RE::FormID> a_ids) {
        std::scoped_lock lock(_lock);
        _outfitDisabled = std::move(a_ids);
    }

}
