#pragma once

#include <mutex>
#include <unordered_set>

namespace InTheirShoes {

    // Actors that have been impersonated never get their outfit added again by the engine: the outfit's
    // items left with the player, and re-adding the outfit duplicated what they were given back.
    // No base record changes, so other actors sharing the donor's base keep their outfit. Papyrus SetOutfit on a
    // donor still changes the base outfit and removes the old outfit's items, but no longer adds the new one.
    // The list is read by the outfit hook, which may run on loading threads.
    class OutfitManager {
    public:
        static OutfitManager& GetSingleton() {
            static OutfitManager instance;
            return instance;
        }

        // Actor::AddWornOutfit isn't virtual, so its direct callers are patched
        void InstallHook();

        void DisableOutfit(const RE::Actor* a_actor);
        bool IsOutfitDisabled(const RE::Actor* a_actor) const;
        void Clear();

        // The 'OUTF' co-save record
        void Save(SKSE::SerializationInterface* a_serde) const;
        void Load(SKSE::SerializationInterface* a_serde);

    private:
        OutfitManager() = default;
        ~OutfitManager() = default;
        OutfitManager(const OutfitManager&) = delete;
        OutfitManager& operator=(const OutfitManager&) = delete;

        static void Hook_AddWornOutfit(RE::Actor* a_this, RE::BGSOutfit* a_outfit, bool a_forceUpdate);
        inline static REL::Relocation<decltype(Hook_AddWornOutfit)> _origAddWornOutfit;

        mutable std::mutex _lock;
        std::unordered_set<RE::FormID> _outfitDisabled;
    };

}
