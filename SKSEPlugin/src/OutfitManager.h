#pragma once

#include "Singleton.h"
#include <mutex>
#include <unordered_set>

namespace InTheirShoes {

    // Actors that have been impersonated never get their outfit added again by the engine: the outfit's
    // items left with the player, and re-adding the outfit duplicated what they were given back.
    // No base record changes, so other actors sharing the donor's base keep their outfit. Papyrus SetOutfit on a
    // donor still changes the base outfit and removes the old outfit's items, but no longer adds the new one.
    // The list is read by the outfit hook, which may run on loading threads.
    class OutfitManager : public Singleton<OutfitManager> {
    public:
        // Actor::AddWornOutfit isn't virtual, so its direct callers are patched
        void InstallHook();

        void DisableOutfit(const RE::Actor* a_actor);
        bool IsOutfitDisabled(const RE::Actor* a_actor) const;
        void Clear();

        // The 'OUTF' co-save record
        std::unordered_set<RE::FormID> GetOutfitDisabledIDs() const;
        void SetOutfitDisabledIDs(std::unordered_set<RE::FormID> a_ids);

    private:
        friend Singleton<OutfitManager>;
        OutfitManager() = default;
        ~OutfitManager() = default;

        static void Hook_AddWornOutfit(RE::Actor* a_this, RE::BGSOutfit* a_outfit, bool a_forceUpdate);
        inline static REL::Relocation<decltype(Hook_AddWornOutfit)> _origAddWornOutfit;

        mutable std::mutex _lock;
        std::unordered_set<RE::FormID> _outfitDisabled;
    };

}
