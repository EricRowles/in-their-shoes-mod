#pragma once

#include <cstdint>

namespace InTheirShoes::Serialization {

    // Each record has its own version, so changing one record's layout doesn't discard the others in
    // existing saves. When a layout changes, bump that record's version and keep reading the old layout.
    // Every record starts at v1 (the first release format; development saves aren't read).
    inline constexpr std::uint32_t kMinSupportedVersion = 1;

    // The Dragonborn's face, stats and spells
    inline constexpr std::uint32_t kBaselineRecord = 'BASE';
    inline constexpr std::uint32_t kBaselineVersion = 1;
    // Donor, puppet, swapped aliases (with their factions) and donor quest items carried by the player.
    // v2 added the aliases' factions; v1 isn't read.
    inline constexpr std::uint32_t kSessionRecord = 'SESS';
    inline constexpr std::uint32_t kSessionVersion = 2;
    inline constexpr std::uint32_t kSessionMinVersion = 2;
    // The donor's appearance as captured when impersonation began
    inline constexpr std::uint32_t kDonorFaceRecord = 'DFAC';
    inline constexpr std::uint32_t kDonorFaceVersion = 1;

    void Register();
    void SaveCallback(SKSE::SerializationInterface* a_serde);
    void LoadCallback(SKSE::SerializationInterface* a_serde);
    void RevertCallback(SKSE::SerializationInterface* a_serde);

}
