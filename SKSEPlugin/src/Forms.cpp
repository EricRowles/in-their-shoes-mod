#include "PCH.h"
#include "Forms.h"

namespace InTheirShoes::Forms {

    namespace {
        constexpr auto kModPlugin = "InTheirShoes.esp"sv;
        constexpr auto kSkyrimPlugin = "Skyrim.esm"sv;
        constexpr auto kDawnguardPlugin = "Dawnguard.esm"sv;

        // IDs are plugin-local (no load order prefix)
        template <class T>
        void Lookup(RE::TESDataHandler* a_dataHandler, T*& a_out, RE::FormID a_localID, std::string_view a_plugin, std::string_view a_name) {
            a_out = a_dataHandler->LookupForm<T>(a_localID, a_plugin);
            if (a_out) {
                SKSE::log::info("Resolved {} ({:08X}).", a_name, a_out->GetFormID());
            } else {
                SKSE::log::error("Failed to resolve {} ({:06X} in {}).", a_name, a_localID, a_plugin);
            }
        }
    }

    void Resolve() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("No data handler; forms not resolved.");
            return;
        }

        Lookup(dataHandler, itsQuest, 0x000D92, kModPlugin, "ITS_Quest"sv);
        Lookup(dataHandler, storageMarker, 0x000807, kModPlugin, "ITS_StorageMarker"sv);
        Lookup(dataHandler, impersonateSpell, 0x000D69, kModPlugin, "ITS_ImpersonateSpell"sv);
        Lookup(dataHandler, revertSpell, 0x000802, kModPlugin, "ITS_RevertSpell"sv);
        Lookup(dataHandler, emptyOutfit, 0x00087C, kModPlugin, "ITS_EmptyOutfit"sv);

        Lookup(dataHandler, currentFollowerFaction, 0x05C84E, kSkyrimPlugin, "CurrentFollowerFaction"sv);
        Lookup(dataHandler, playerFollowerFaction, 0x0BD738, kSkyrimPlugin, "PlayerFollowerFaction"sv);
        Lookup(dataHandler, defaultSandboxPackage, 0x0BFB6B, kSkyrimPlugin, "DefaultSandboxCurrentLocation1024"sv);
        Lookup(dataHandler, playerWerewolfQuest, 0x02BA16, kSkyrimPlugin, "PlayerWerewolfQuest"sv);

        Lookup(dataHandler, dlc1PlayerVampireQuest, 0x0071D0, kDawnguardPlugin, "DLC1PlayerVampireQuest"sv);
    }

}
