#include "PCH.h"
#include "ActorHooks.h"
#include "Forms.h"
#include "GameUtil.h"
#include "Lifecycle.h"
#include "Session.h"
#include "Settings.h"

namespace InTheirShoes::ActorHooks {

    namespace {
        REL::Relocation<RE::BGSVoiceType*(const RE::TESNPC*)> _origGetObjectVoiceType;

        using Update_t = void(RE::Actor*, float);
        REL::Relocation<Update_t> _origPlayerUpdate;

        using GetEditorLocation1_t = RE::BGSLocation*(const RE::TESObjectREFR*);
        REL::Relocation<GetEditorLocation1_t> _origPlayerGetEditorLocation1;

        using GetEditorLocation2_t = bool(RE::TESObjectREFR*, RE::NiPoint3&, RE::NiPoint3&, RE::TESForm*&, RE::TESObjectCELL*);
        REL::Relocation<GetEditorLocation2_t> _origPlayerGetEditorLocation2;

        REL::Relocation<RE::UI_MESSAGE_RESULTS(RE::RaceSexMenu*, RE::UIMessage&)> _origRaceSexMenuProcessMessage;

        RE::UI_MESSAGE_RESULTS Hook_RaceSexMenu_ProcessMessage(RE::RaceSexMenu* a_this, RE::UIMessage& a_message) {
            // RaceSexMenu reads the player's tint masks at the race's tint indices (ID 52412); with a donor of another race
            // they run past the Dragonborn's masks and crash. kIgnore on kShow drops the menu before it opens (ID 82082),
            // so RaceMenu doesn't save the donor's tints either.
            if (a_message.type == RE::UI_MESSAGE_TYPE::kShow && Session::GetSingleton()->IsActive() && !a_this->AlwaysOpen()) {
                SKSE::log::info("Refused to open RaceMenu while impersonating");
                RE::SendHUDMessage::ShowHUDMessage("You can't change your appearance while impersonating.");
                return RE::UI_MESSAGE_RESULTS::kIgnore;
            }
            return _origRaceSexMenuProcessMessage(a_this, a_message);
        }

        RE::BGSVoiceType* Hook_GetObjectVoiceType(const RE::TESNPC* a_this) {
            if (a_this) {
                auto& session = *Session::GetSingleton();
                auto* donorVoice = session.GetDonorVoiceType();
                if (session.IsActive() && donorVoice) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (player && a_this == player->GetActorBase()) {
                        if (Settings::GetSingleton()->IsNPCVoiceEnabled()) {
                            return donorVoice;
                        }
                        return session.GetOrigPlayerVoiceType();
                    }

                    auto* puppet = session.GetPuppet();
                    // Also for lines that end the conversation (forceGoodbye): without a voice type they had no
                    // voice file for the player to voice (#3)
                    if (puppet && a_this == puppet->GetActorBase()) {
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
            if (a_this && Session::GetSingleton()->IsActive()) {
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
            if (a_this && Session::GetSingleton()->IsActive()) {
                // The donor's editor location, so its sandbox packages (DefaultMasterPackage's "near editor
                // location") take the player home. Not the holding cell, where anchoring them froze the game
                // (2c72b0c): there, and when the donor has none, the player's own position.
                if (auto* donor = Session::GetSingleton()->GetDonor()) {
                    RE::TESForm* worldOrCell = nullptr;
                    const auto* holdingCell = Forms::storageMarker ? Forms::storageMarker->GetParentCell() : nullptr;
                    if (donor->GetEditorLocation2(a_outPos, a_outRot, worldOrCell, a_fallback) && worldOrCell &&
                        worldOrCell != holdingCell) {
                        a_outWorldOrCell = worldOrCell;
                        return true;
                    }
                }
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

        REL::Relocation<std::uintptr_t> raceSexVtbl{ RE::VTABLE_RaceSexMenu[0] };
        _origRaceSexMenuProcessMessage = raceSexVtbl.write_vfunc(0x4, &Hook_RaceSexMenu_ProcessMessage);
        SKSE::log::info("Hooked RaceSexMenu::ProcessMessage (0x4)");
    }

    RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc) {
        if (!a_npc) return nullptr;
        if (a_npc->voiceType) return a_npc->voiceType;
        // Install runs at data load, before any caller
        return _origGetObjectVoiceType(a_npc);
    }

}
