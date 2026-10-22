#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <cstdint>

namespace InTheirShoes {

    class ImpersonationManager {
    public:
        static ImpersonationManager& GetSingleton() {
            static ImpersonationManager instance;
            return instance;
        }

        void Initialize();
        std::int32_t Begin(RE::Actor* a_donor, RE::Actor* a_player);
        std::int32_t End();
        void Tick();

        bool IsActive() const { return _active.load(std::memory_order_acquire); }

        RE::Actor* GetDonor() const { return _donorActor.load(std::memory_order_acquire); }
        RE::Actor* GetPuppet() const { return _puppetActor.load(std::memory_order_acquire); }
        RE::Actor* GetPlayer() const { return RE::PlayerCharacter::GetSingleton(); }

        std::string GetOrigPlayerName() const;
        RE::BGSVoiceType* GetOrigPlayerVoiceType() const;
        RE::BGSVoiceType* GetDonorVoiceType() const { return _donorVoiceType.load(std::memory_order_acquire); }
        bool IsPuppetBase(const RE::TESNPC* a_npc) const {
            if (!a_npc) return false;
            return a_npc == _puppetBase.load(std::memory_order_acquire);
        }
        bool IsPuppetBase(const RE::TESForm* a_form) const {
            if (!a_form) return false;
            return a_form == static_cast<const RE::TESForm*>(_puppetBase.load(std::memory_order_acquire));
        }
        void SetPuppet(RE::Actor* a_puppet);

        void SetNPCVoiceEnabled(bool a_enabled) { _npcVoiceEnabled.store(a_enabled, std::memory_order_release); }
        bool IsNPCVoiceEnabled() const { return _npcVoiceEnabled.load(std::memory_order_acquire); }
        void SetPuppetFollowsPlayer(bool a_follows);

        void Save(SKSE::SerializationInterface* a_serde);
        void Load(SKSE::SerializationInterface* a_serde);
        void OnRevert();
        void OnPostLoadGame();

        static RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc);
        static bool IsModInternalQuest(RE::TESQuest* a_quest);

        void ForwardPuppetFactionsToDonor();

        RE::ObjectRefHandle GetStorageMarker() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _storageMarker;
        }

        struct SwappedAlias {
            RE::FormID questFormID{ 0 };
            std::uint32_t aliasID{ 0 };
        };

        bool IsSwappedAlias(RE::FormID a_questFormID, std::uint32_t a_aliasID) const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            for (const auto& entry : _swappedAliases) {
                if (entry.questFormID == a_questFormID && entry.aliasID == a_aliasID) return true;
            }
            return false;
        }

    private:
        ImpersonationManager() = default;
        ~ImpersonationManager() = default;
        ImpersonationManager(const ImpersonationManager&) = delete;
        ImpersonationManager& operator=(const ImpersonationManager&) = delete;

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

        mutable std::recursive_mutex _mutex;
        std::atomic<bool> _active{ false };

        std::atomic<RE::Actor*> _donorActor{ nullptr };
        RE::NiPointer<RE::Actor> _donorKeepAlive;
        std::atomic<RE::BGSVoiceType*> _donorVoiceType{ nullptr };
        std::atomic<RE::Actor*> _puppetActor{ nullptr };
        RE::NiPointer<RE::Actor> _puppetKeepAlive;
        std::atomic<RE::TESNPC*> _puppetBase{ nullptr };
        std::atomic<bool> _npcVoiceEnabled{ true };
        std::atomic<bool> _puppetFollows{ true };
        RE::ObjectRefHandle _storageMarker;

        std::vector<SwappedAlias> _swappedAliases;

        void AttachDonor(RE::Actor* a_donor);
        void DetachDonor();
        void AttachPuppet(RE::Actor* a_puppet);
        void ConfigurePuppet(RE::Actor* a_puppet);
        void ApplyPuppetFollowState(RE::Actor* a_puppet);
        bool _puppetConfigured{ false };
        void SwapDonorAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player);
        void RestoreSwappedAliases(RE::Actor* a_donor);

        bool _hooksInstalled{ false };
    };

}
