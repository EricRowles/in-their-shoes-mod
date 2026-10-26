#pragma once

#include <cstdint>

namespace InTheirShoes::Serialization {

    // v4: the Dragonborn's face/stats/spells are one BASE record; the session is one SESS record
    inline constexpr std::uint32_t kSerializationVersion = 4;

    inline constexpr std::uint32_t kBaselineRecord = 'BASE';
    inline constexpr std::uint32_t kSessionRecord = 'SESS';
    // Actors whose outfit the engine no longer adds (every donor ever impersonated); absent in older saves
    inline constexpr std::uint32_t kOutfitDisabledRecord = 'OUTF';
    // The donor's appearance as captured when impersonation began; absent in older saves
    inline constexpr std::uint32_t kDonorFaceRecord = 'DFAC';

    void Register();
    void SaveCallback(SKSE::SerializationInterface* a_serde);
    void LoadCallback(SKSE::SerializationInterface* a_serde);
    void RevertCallback(SKSE::SerializationInterface* a_serde);

}
