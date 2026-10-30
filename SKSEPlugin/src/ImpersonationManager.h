#pragma once

#include "Singleton.h"
#include <optional>
#include <unordered_set>
#include <cstdint>

#include "AliasSwapManager.h"
#include "FaceManager.h"
#include "InventoryManager.h"
#include "Session.h"
#include "StatManager.h"

namespace InTheirShoes {

    // The Dragonborn's own state, captured on the first impersonation and restored on revert
    struct Baseline {
        FaceSnapshot face;
        StatSnapshot stats;
        SpellSnapshot spells;
    };

    // Owns the impersonation session: swapping appearance, stats, spells, items and aliases between the
    // three actors, and keeping the donor parked. Who is who is published in Session for the hooks.
    // Not thread-safe.
    class ImpersonationManager :
        public Singleton<ImpersonationManager>,
        public RE::BSTEventSink<SKSE::NiNodeUpdateEvent> {
    public:
        void Initialize();
        void Tick();

        // BSTEventSink<SKSE::NiNodeUpdateEvent>: puts the donor's skin and hair back after the player's 3D is
        // rebuilt (#14)
        RE::BSEventNotifyControl ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event, RE::BSTEventSource<SKSE::NiNodeUpdateEvent>* a_eventSource) override;

        // Starts impersonating a_donor, or switches to it if already impersonating someone else.
        // Moves the donor to the storage marker; the caller positions the puppet beforehand.
        bool Impersonate(RE::Actor* a_donor, RE::Actor* a_puppet);

        // Ends impersonation: the donor gets the player's current items/spells/stats, and the
        // player gets their baseline back. The caller moves the actors afterwards.
        bool Revert();
        // Whether the player is in Werewolf or Vampire Lord form (or changing into or out of it).
        // Impersonate and Revert refuse while it is true.
        static bool IsPlayerTransformed();

        void SetPuppetFollowsPlayer(bool a_follows);

        // Faction changes made to the puppet in self-dialogue (it stands in for the donor) go to the donor
        void ForwardPuppetFactionsToDonor();

        // Serialization
        const std::optional<Baseline>& GetBaseline() const { return _baseline; }
        void SetBaseline(Baseline a_baseline);
        // The donor's appearance as captured when impersonation began (rendered tints included)
        const std::optional<FaceSnapshot>& GetDonorFace() const { return _donorFace; }
        void SetDonorFace(FaceSnapshot a_face) { _donorFace = std::move(a_face); }
        // Donor quest items the player carries ('SESS' record)
        const std::unordered_set<RE::FormID>& GetDonorQuestItems() const { return _donorQuestItems; }
        // Restores a saved session; a_donor is null if it no longer exists, and the session then isn't restored
        void RestoreSession(RE::Actor* a_donor, RE::Actor* a_puppet, std::vector<AliasSwapManager::SwappedAlias> a_aliases,
            std::unordered_set<RE::FormID> a_questItems);
        void OnRevert();
        void OnPostLoadGame();

    private:
        friend Singleton<ImpersonationManager>;
        ImpersonationManager() = default;
        ~ImpersonationManager() = default;

        static bool IsActive() { return Session::GetSingleton().IsActive(); }
        static RE::Actor* GetDonor() { return Session::GetSingleton().GetDonor(); }
        static RE::Actor* GetPuppet() { return Session::GetSingleton().GetPuppet(); }
        static RE::Actor* GetPlayer() { return Session::GetPlayer(); }

        void CaptureBaseline(RE::Actor* a_player);
        void BeginSession(RE::Actor* a_donor);
        void EndSession();
        void ReturnStateToDonor(RE::Actor* a_donor);
        // Forgets every part of the session, once Revert has handed its state back or a game revert discards it
        void ClearSessionState();

        void AttachDonor(RE::Actor* a_donor);
        void DetachDonor();
        void AttachPuppet(RE::Actor* a_puppet);
        void ConfigurePuppet(RE::Actor* a_puppet);
        void ReleasePuppet();
        void ApplyPuppetFollowState(RE::Actor* a_puppet);
        void RefreshPuppetAppearanceWhenLoaded();

        RE::NiPointer<RE::Actor> _donorKeepAlive;
        RE::NiPointer<RE::Actor> _puppetKeepAlive;
        bool _puppetFollows{ true };
        bool _leashQueued{ false };
        bool _puppetRefreshPending{ false };
        RE::ObjectRefHandle _storageMarker;
        std::optional<Baseline> _baseline;
        std::optional<FaceSnapshot> _donorFace;
        std::unordered_set<RE::FormID> _donorQuestItems;
    };

}
