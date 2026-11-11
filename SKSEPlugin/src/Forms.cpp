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
        Lookup(dataHandler, playerStorageChest, 0x00087D, kModPlugin, "ITS_PlayerStorageChest"sv);
        Lookup(dataHandler, impersonateSpell, 0x000D69, kModPlugin, "ITS_ImpersonateSpell"sv);
        Lookup(dataHandler, revertSpell, 0x000802, kModPlugin, "ITS_RevertSpell"sv);
        Lookup(dataHandler, emptyOutfit, 0x00087C, kModPlugin, "ITS_EmptyOutfit"sv);
        Lookup(dataHandler, idlePackageQuest, 0x00087E, kModPlugin, "ITS_PackageManager"sv);

        Lookup(dataHandler, playerFaction, 0x000DB1, kSkyrimPlugin, "PlayerFaction"sv);
        Lookup(dataHandler, playerWerewolfQuest, 0x02BA16, kSkyrimPlugin, "PlayerWerewolfQuest"sv);

        Lookup(dataHandler, dlc1PlayerVampireQuest, 0x0071D0, kDawnguardPlugin, "DLC1PlayerVampireQuest"sv);

        // Each city's Location and the LocationCenterMarker its record lists (a persistent reference in the city)
        struct DestinationIDs {
            RE::FormID location;
            RE::FormID marker;
            std::string_view name;
        };
        constexpr std::array<DestinationIDs, 9> kDestinations{ {
            { 0x018A56, 0x029AAF, "WhiterunLocation"sv },
            { 0x018A5A, 0x04DEB7, "SolitudeLocation"sv },
            { 0x018A57, 0x02A975, "WindhelmLocation"sv },
            { 0x018A58, 0x01CDD6, "RiftenLocation"sv },
            { 0x018A59, 0x01B38D, "MarkarthLocation"sv },
            { 0x018A49, 0x029DBB, "FalkreathLocation"sv },
            { 0x018A53, 0x01C1BA, "MorthalLocation"sv },
            { 0x018A50, 0x0284C5, "DawnstarLocation"sv },
            { 0x018A51, 0x01C1B1, "WinterholdLocation"sv },
        } };
        static_assert(kDestinations.size() == std::tuple_size_v<decltype(travelDestinations)>);
        for (std::size_t i = 0; i < kDestinations.size(); ++i) {
            const auto& ids = kDestinations[i];
            Lookup(dataHandler, travelDestinations[i].location, ids.location, kSkyrimPlugin, ids.name);
            Lookup(dataHandler, travelDestinations[i].marker, ids.marker, kSkyrimPlugin, "its LocationCenterMarker"sv);
        }
    }

}
