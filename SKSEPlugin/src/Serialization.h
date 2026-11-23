#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

namespace InTheirShoes::Serialization {

    using Serde = SKSE::SerializationInterface;

    // The co-save's ID. It was 'ALTA' before 0.9, whose records this version doesn't read: a session must be
    // reverted before updating from an older version.
    inline constexpr std::uint32_t kUniqueID = 'ITSH';

    // Each record has its own version. When a layout changes, bump that record's version; only the current one is
    // read, so an older record is skipped (and logged).

    // The Dragonborn's face, stats, spells and character progress
    inline constexpr std::uint32_t kBaselineRecord = 'BASE';
    inline constexpr std::uint32_t kBaselineVersion = 1;
    // Donor, puppet, swapped aliases (with their factions) and donor quest items carried by the player
    inline constexpr std::uint32_t kSessionRecord = 'SESS';
    inline constexpr std::uint32_t kSessionVersion = 1;
    // The donor's appearance as captured when impersonation began
    inline constexpr std::uint32_t kDonorFaceRecord = 'DFAC';
    inline constexpr std::uint32_t kDonorFaceVersion = 1;
    // The MCM's settings (Settings)
    inline constexpr std::uint32_t kSettingsRecord = 'SETT';
    inline constexpr std::uint32_t kSettingsVersion = 1;
    // SessionStash records are declared with their stashes

    void Register();

    // --- Primitives ---
    // Every record is written and read with these, so a record's layout is the sequence of calls in its write and
    // read functions. Readers return false only when the record ends early; callers then drop the whole record
    // rather than apply part of it.

    // Saved as raw bytes: plain data, never a pointer (forms are saved by ID). Engine types such as RE::Color
    // define copy operators, so "trivially copyable" is too strict.
    template <class T>
    constexpr bool kIsRawData = std::is_standard_layout_v<T> && std::is_trivially_destructible_v<T> && !std::is_pointer_v<T>;

    template <class T>
    void Write(Serde* a_serde, const T& a_value) {
        static_assert(kIsRawData<T>);
        a_serde->WriteRecordData(&a_value, sizeof(T));
    }

    template <class T>
    bool Read(Serde* a_serde, T& a_out) {
        static_assert(kIsRawData<T>);
        return a_serde->ReadRecordData(&a_out, sizeof(T)) == sizeof(T);
    }

    inline void WriteForm(Serde* a_serde, const RE::TESForm* a_form) {
        Write(a_serde, a_form ? a_form->GetFormID() : RE::FormID{ 0 });
    }

    // A saved form ID mapped to the current load order; 0 if it was saved as none or its plugin is gone
    inline bool ReadFormID(Serde* a_serde, RE::FormID& a_out) {
        if (!Read(a_serde, a_out)) return false;
        if (a_out != 0 && !a_serde->ResolveFormID(a_out, a_out)) {
            a_out = 0;
        }
        return true;
    }

    // A form that no longer exists (or was saved as none) reads as nullptr
    template <class T>
    bool ReadForm(Serde* a_serde, T*& a_out) {
        RE::FormID id = 0;
        if (!ReadFormID(a_serde, id)) return false;
        a_out = id != 0 ? RE::TESForm::LookupByID<T>(id) : nullptr;
        return true;
    }

    // A count, then a_writeItem for each element
    template <class Range, class WriteItem>
    void WriteList(Serde* a_serde, const Range& a_range, WriteItem&& a_writeItem) {
        Write(a_serde, static_cast<std::uint32_t>(std::size(a_range)));
        for (const auto& item : a_range) {
            a_writeItem(item);
        }
    }

    // Reads a count, then calls a_readItem (which returns false if the record ends) that many times
    template <class ReadItem>
    bool ReadList(Serde* a_serde, ReadItem&& a_readItem) {
        std::uint32_t count = 0;
        if (!Read(a_serde, count)) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!a_readItem()) return false;
        }
        return true;
    }

    // A list of form IDs; IDs whose plugin is gone are left out
    inline void WriteFormIDs(Serde* a_serde, const std::vector<RE::FormID>& a_ids) {
        WriteList(a_serde, a_ids, [&](RE::FormID a_id) { Write(a_serde, a_id); });
    }

    inline bool ReadFormIDs(Serde* a_serde, std::vector<RE::FormID>& a_out) {
        return ReadList(a_serde, [&] {
            RE::FormID id = 0;
            if (!ReadFormID(a_serde, id)) return false;
            if (id != 0) a_out.push_back(id);
            return true;
        });
    }

    // Factions with ranks; factions whose plugin is gone are left out
    using FactionRank = std::pair<RE::TESFaction*, std::int8_t>;

    inline void WriteFactionRanks(Serde* a_serde, const std::vector<FactionRank>& a_factions) {
        WriteList(a_serde, a_factions, [&](const FactionRank& a_entry) {
            WriteForm(a_serde, a_entry.first);
            Write(a_serde, a_entry.second);
        });
    }

    inline bool ReadFactionRanks(Serde* a_serde, std::vector<FactionRank>& a_out) {
        return ReadList(a_serde, [&] {
            RE::TESFaction* faction = nullptr;
            std::int8_t rank = 0;
            if (!ReadForm(a_serde, faction) || !Read(a_serde, rank)) return false;
            if (faction) a_out.emplace_back(faction, rank);
            return true;
        });
    }

}
