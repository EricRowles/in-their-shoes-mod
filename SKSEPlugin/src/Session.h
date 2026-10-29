#pragma once

#include "Singleton.h"
#include <atomic>

namespace InTheirShoes {

    // Who is who in the current impersonation. Readable from any thread: condition and voice hooks run on AI
    // and loading threads. ImpersonationManager writes the session fields; DialogueManager writes the
    // self-dialogue flag.
    class Session : public Singleton<Session> {
    public:
        bool IsActive() const { return _active.load(std::memory_order_acquire); }
        RE::Actor* GetDonor() const { return _donor.load(std::memory_order_acquire); }
        RE::Actor* GetPuppet() const { return _puppet.load(std::memory_order_acquire); }
        static RE::Actor* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }

        RE::BGSVoiceType* GetDonorVoiceType() const { return _donorVoiceType.load(std::memory_order_acquire); }
        RE::BGSVoiceType* GetOrigPlayerVoiceType() const { return _origPlayerVoiceType.load(std::memory_order_acquire); }
        bool IsNPCVoiceEnabled() const { return _npcVoiceEnabled.load(std::memory_order_acquire); }

        // Self-dialogue: the player talking to the puppet, which stands in for the donor. Set when the puppet
        // enters dialogue with the player; cleared when it leaves, when another actor does, or the menu closes.
        bool IsInSelfDialogue() const { return _inSelfDialogue.load(std::memory_order_acquire); }

        // Where a reference counts as being for spatial checks (conditions and Papyrus natives): the parked
        // donor is where the player is, and so is the puppet during self-dialogue
        RE::TESObjectREFR* ResolveSpatialRef(RE::TESObjectREFR* a_ref) const {
            if (!a_ref || !IsActive()) return a_ref;
            auto* donor = GetDonor();
            auto* puppet = GetPuppet();
            if ((donor && a_ref == donor) || (puppet && a_ref == puppet && IsInSelfDialogue())) {
                return GetPlayer();
            }
            return a_ref;
        }

        void SetActive(bool a_active) { _active.store(a_active, std::memory_order_release); }
        void SetDonor(RE::Actor* a_donor, RE::BGSVoiceType* a_voiceType) {
            _donor.store(a_donor, std::memory_order_release);
            _donorVoiceType.store(a_voiceType, std::memory_order_release);
        }
        void SetPuppet(RE::Actor* a_puppet) { _puppet.store(a_puppet, std::memory_order_release); }
        void SetOrigPlayerVoiceType(RE::BGSVoiceType* a_voiceType) { _origPlayerVoiceType.store(a_voiceType, std::memory_order_release); }
        void SetNPCVoiceEnabled(bool a_enabled) { _npcVoiceEnabled.store(a_enabled, std::memory_order_release); }
        void SetInSelfDialogue(bool a_inSelfDialogue) { _inSelfDialogue.store(a_inSelfDialogue, std::memory_order_release); }

    private:
        friend Singleton<Session>;
        Session() = default;
        ~Session() = default;

        std::atomic<bool> _active{ false };
        std::atomic<RE::Actor*> _donor{ nullptr };
        std::atomic<RE::Actor*> _puppet{ nullptr };
        std::atomic<RE::BGSVoiceType*> _donorVoiceType{ nullptr };
        std::atomic<RE::BGSVoiceType*> _origPlayerVoiceType{ nullptr };
        std::atomic<bool> _npcVoiceEnabled{ true };
        std::atomic<bool> _inSelfDialogue{ false };
    };

}
