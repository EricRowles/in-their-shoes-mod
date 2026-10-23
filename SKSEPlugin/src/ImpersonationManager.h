#pragma once

#include <atomic>
#include <optional>
#include <unordered_set>
#include <vector>
#include <cstdint>

#include "FaceManager.h"
#include "InventoryManager.h"
#include "StatManager.h"

namespace InTheirShoes {

    // The Dragonborn's own state, captured on the first impersonation and restored on revert
    struct Baseline {
        FaceSnapshot face;
        StatSnapshot stats;
        SpellSnapshot spells;
    };

    // Owns the impersonation session. Everything here runs on the main thread except the
    // condition/voice hooks, which may run on AI worker threads and only read the atomic fields.
    class ImpersonationManager {
    public:
        static ImpersonationManager& GetSingleton() {
            static ImpersonationManager instance;
            return instance;
        }

        void Initialize();
        void Tick();

        // Starts impersonating a_donor, or switches to it if already impersonating someone else.
        // Moves the donor to the storage marker; the caller positions the puppet beforehand.
        bool Impersonate(RE::Actor* a_donor, RE::Actor* a_puppet);

        // Ends impersonation: the donor gets the player's current items/spells/stats, and the
        // player gets their baseline back. The caller moves the actors afterwards.
        bool Revert();

        bool IsActive() const { return _active.load(std::memory_order_acquire); }
        RE::Actor* GetDonor() const { return _donorActor.load(std::memory_order_acquire); }
        RE::Actor* GetPuppet() const { return _puppetActor.load(std::memory_order_acquire); }
        RE::Actor* GetPlayer() const { return RE::PlayerCharacter::GetSingleton(); }

        RE::BGSVoiceType* GetOrigPlayerVoiceType() const { return _origVoiceType.load(std::memory_order_acquire); }
        RE::BGSVoiceType* GetDonorVoiceType() const { return _donorVoiceType.load(std::memory_order_acquire); }

        void SetNPCVoiceEnabled(bool a_enabled) { _npcVoiceEnabled.store(a_enabled, std::memory_order_release); }
        bool IsNPCVoiceEnabled() const { return _npcVoiceEnabled.load(std::memory_order_acquire); }
        void SetPuppetFollowsPlayer(bool a_follows);

        void ForwardPuppetFactionsToDonor();

        // Moves any outside quest alias the puppet was forced into (e.g. by a recruitment fragment that
        // calls ForceRefTo(speaker) during self-dialogue) to the player, tracked as a swapped alias so
        // the donor gets it on revert
        void AdoptPuppetAliases();

        bool IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const {
            for (const auto& entry : _swappedAliases) {
                if (entry.questFormID == a_questFormID && entry.aliasID == a_aliasID) return true;
            }
            return false;
        }

        // Serialization
        const std::optional<Baseline>& GetBaseline() const { return _baseline; }
        void SetBaseline(Baseline a_baseline);
        void SaveSession(SKSE::SerializationInterface* a_serde) const;
        void LoadSession(SKSE::SerializationInterface* a_serde);
        void OnRevert();
        void OnPostLoadGame();

        static RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc);
        static bool IsModInternalQuest(RE::TESQuest* a_quest);

    private:
        ImpersonationManager() = default;
        ~ImpersonationManager() = default;
        ImpersonationManager(const ImpersonationManager&) = delete;
        ImpersonationManager& operator=(const ImpersonationManager&) = delete;

        struct SwappedAlias {
            RE::FormID questFormID{ 0 };
            std::uint32_t aliasID{ 0 };
        };

        void InstallHooks();
        static RE::BGSVoiceType* Hook_GetObjectVoiceType(const RE::TESNPC* a_this);
        inline static REL::Relocation<decltype(Hook_GetObjectVoiceType)> _origGetObjectVoiceType;

        using Update_t = void(RE::Actor*, float);
        static void Hook_Player_Update(RE::Actor* a_this, float a_delta);
        inline static REL::Relocation<Update_t> _origPlayerUpdate;

        static RE::BGSLocation* Hook_Player_GetEditorLocation1(const RE::TESObjectREFR* a_this);
        using GetEditorLocation1_t = RE::BGSLocation*(const RE::TESObjectREFR*);
        inline static REL::Relocation<GetEditorLocation1_t> _origPlayerGetEditorLocation1;

        static bool Hook_Player_GetEditorLocation2(
            RE::TESObjectREFR* a_this,
            RE::NiPoint3& a_outPos,
            RE::NiPoint3& a_outRot,
            RE::TESForm*& a_outWorldOrCell,
            RE::TESObjectCELL* a_fallback);
        using GetEditorLocation2_t = bool(RE::TESObjectREFR*, RE::NiPoint3&, RE::NiPoint3&, RE::TESForm*&, RE::TESObjectCELL*);
        inline static REL::Relocation<GetEditorLocation2_t> _origPlayerGetEditorLocation2;

        void CaptureBaseline(RE::Actor* a_player);
        void BeginSession(RE::Actor* a_donor);
        void EndSession();
        void ReturnStateToDonor(RE::Actor* a_donor);

        void AttachDonor(RE::Actor* a_donor);
        void DetachDonor();
        void AttachPuppet(RE::Actor* a_puppet);
        void ConfigurePuppet(RE::Actor* a_puppet);
        void ReleasePuppet();
        void ApplyPuppetFollowState(RE::Actor* a_puppet);
        void RefreshPuppetAppearanceWhenLoaded();

        void SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player);
        void RestoreSwappedAliases(RE::Actor* a_donor);

        // Read by condition/voice hooks
        std::atomic<bool> _active{ false };
        std::atomic<RE::Actor*> _donorActor{ nullptr };
        std::atomic<RE::BGSVoiceType*> _donorVoiceType{ nullptr };
        std::atomic<RE::Actor*> _puppetActor{ nullptr };
        std::atomic<RE::BGSVoiceType*> _origVoiceType{ nullptr };
        std::atomic<bool> _npcVoiceEnabled{ true };

        // Main thread only
        RE::NiPointer<RE::Actor> _donorKeepAlive;
        RE::NiPointer<RE::Actor> _puppetKeepAlive;
        bool _puppetFollows{ true };
        bool _leashQueued{ false };
        bool _puppetRefreshPending{ false };
        bool _hooksInstalled{ false };
        RE::ObjectRefHandle _storageMarker;
        std::optional<Baseline> _baseline;
        std::vector<SwappedAlias> _swappedAliases;
        std::unordered_set<RE::FormID> _donorQuestItems;
    };

}
