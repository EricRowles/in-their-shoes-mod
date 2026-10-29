#include "PCH.h"
#include "ActorHooks.h"
#include "Lifecycle.h"
#include "Session.h"

namespace InTheirShoes::ActorHooks {

    namespace {
        REL::Relocation<RE::BGSVoiceType*(const RE::TESNPC*)> _origGetObjectVoiceType;

        using Update_t = void(RE::Actor*, float);
        REL::Relocation<Update_t> _origPlayerUpdate;

        using GetEditorLocation1_t = RE::BGSLocation*(const RE::TESObjectREFR*);
        REL::Relocation<GetEditorLocation1_t> _origPlayerGetEditorLocation1;

        using GetEditorLocation2_t = bool(RE::TESObjectREFR*, RE::NiPoint3&, RE::NiPoint3&, RE::TESForm*&, RE::TESObjectCELL*);
        REL::Relocation<GetEditorLocation2_t> _origPlayerGetEditorLocation2;

        RE::BGSVoiceType* Hook_GetObjectVoiceType(const RE::TESNPC* a_this) {
            if (a_this) {
                auto& session = Session::GetSingleton();
                auto* donorVoice = session.GetDonorVoiceType();
                if (session.IsActive() && donorVoice) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (player && a_this == player->GetActorBase()) {
                        if (session.IsNPCVoiceEnabled()) {
                            return donorVoice;
                        }
                        return session.GetOrigPlayerVoiceType();
                    }

                    auto* puppet = session.GetPuppet();
                    if (puppet && a_this == puppet->GetActorBase()) {
                        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                        if (topicMgr && topicMgr->forceGoodbye) {
                            return nullptr;
                        }
                        return donorVoice;
                    }
                    if (a_this->voiceType) {
                        return a_this->voiceType;
                    }
                }
            }
            return _origGetObjectVoiceType(a_this);
        }

        void Hook_Player_Update(RE::Actor* a_this, float a_delta) {
            _origPlayerUpdate(a_this, a_delta);

            if (a_this && IsMainThread()) {
                Lifecycle::OnFrame();
            }
        }

        RE::BGSLocation* Hook_Player_GetEditorLocation1(const RE::TESObjectREFR* a_this) {
            if (a_this && Session::GetSingleton().IsActive()) {
                if (auto* loc = a_this->GetCurrentLocation()) {
                    return loc;
                }
            }
            return _origPlayerGetEditorLocation1(a_this);
        }

        bool Hook_Player_GetEditorLocation2(
            RE::TESObjectREFR* a_this,
            RE::NiPoint3& a_outPos,
            RE::NiPoint3& a_outRot,
            RE::TESForm*& a_outWorldOrCell,
            RE::TESObjectCELL* a_fallback)
        {
            if (a_this && Session::GetSingleton().IsActive()) {
                a_outPos = a_this->GetPosition();
                a_outRot = a_this->GetAngle();
                auto* cell = a_this->GetParentCell();
                auto* world = a_this->GetWorldspace();
                a_outWorldOrCell = world ? static_cast<RE::TESForm*>(world) : static_cast<RE::TESForm*>(cell);
                return true;
            }
            return _origPlayerGetEditorLocation2(a_this, a_outPos, a_outRot, a_outWorldOrCell, a_fallback);
        }
    }

    void Install() {
        // Hook TESNPC::GetObjectVoiceType on primary vtable (index 0x49)
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESNPC[0] };
        _origGetObjectVoiceType = vtbl.write_vfunc(0x49, &Hook_GetObjectVoiceType);
        SKSE::log::info("Hooked TESNPC::GetObjectVoiceType at VTABLE_TESNPC[0] idx 0x49 (orig: {:016X})",
            _origGetObjectVoiceType.address());

        // Hook PlayerCharacter vtable (0x3C GetEditorLocation1, 0x3D GetEditorLocation2, 0xAD Update)
        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerGetEditorLocation1 = playerVtbl.write_vfunc(0x3C, &Hook_Player_GetEditorLocation1);
        _origPlayerGetEditorLocation2 = playerVtbl.write_vfunc(0x3D, &Hook_Player_GetEditorLocation2);
        _origPlayerUpdate = playerVtbl.write_vfunc(0xAD, &Hook_Player_Update);
        SKSE::log::info("Hooked PlayerCharacter editor location & update vfuncs (0x3C, 0x3D, 0xAD)");
    }

    RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc) {
        if (!a_npc) return nullptr;
        if (a_npc->voiceType) return a_npc->voiceType;
        if (_origGetObjectVoiceType.address() != 0) {
            return _origGetObjectVoiceType(a_npc);
        }
        if (a_npc->race) {
            std::size_t sexIdx = (a_npc->GetSex() == RE::SEX::kFemale) ? 1 : 0;
            return a_npc->race->defaultVoiceTypes[sexIdx];
        }
        return nullptr;
    }

}
