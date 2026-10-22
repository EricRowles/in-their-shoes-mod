#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "DialogueManager.h"

#include <algorithm>
#include <cmath>

#include "RE/P/PlayerControls.h"
#include "RE/T/TESPackage.h"
#include "RE/T/TESQuest.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/B/BGSRefAlias.h"

namespace InTheirShoes {

    // DefaultSandboxCurrentLocation1024, the fallback when nothing else applies
    constexpr RE::FormID kDefaultSandboxPackage = 0x000BFB6B;

    static bool PackageTargetsPlayer(const RE::TESPackage* a_pkg, RE::Actor* a_player) {
        if (!a_pkg || !a_pkg->packTarg) return false;
        auto type = static_cast<std::uint8_t>(a_pkg->packTarg->targType);
        if (type == 0) {
            return a_pkg->packTarg->target.handle == a_player->GetHandle();
        }
        if (type == 1) {
            return a_pkg->packTarg->target.object == a_player || a_pkg->packTarg->target.object == a_player->GetBaseObject();
        }
        return false;
    }

    void IdleAIManager::Initialize() {
        if (_initialized) return;
        if (auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton()) {
            inputDispatcher->AddEventSink<RE::InputEvent*>(this);
            SKSE::log::info("IdleAIManager registered input event sink successfully.");
        } else {
            SKSE::log::warn("IdleAIManager failed to get BSInputDeviceManager singleton.");
        }
        _initialized = true;
    }

    void IdleAIManager::Update() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->IsInCombat()) {
            if (_aiActive) {
                StopIdleAI();
            }
        } else {
            CheckIdleAI();
        }

        if (!_aiActive || !ImpersonationManager::GetSingleton().IsActive()) return;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - _lastPackageSyncTime).count() >= 1.5f) {
            _lastPackageSyncTime = now;
            SyncDonorPackagesToPlayer();
        }
        CheckForceGreetReachedPuppet();
    }

    void IdleAIManager::CheckForceGreetReachedPuppet() {
        // A donor package that would approach the Dragonborn has been retargeted to the puppet.
        // When the player arrives, open self-dialogue once for this approach.
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* curPkg = player ? player->GetCurrentPackage() : nullptr;
        bool targetsPuppet = puppet && curPkg && curPkg->packTarg &&
                             (curPkg->packTarg->target.handle == puppet->GetHandle() || curPkg->packTarg->target.object == puppet);
        if (!targetsPuppet) {
            _forceGreetTriggered = false;
            return;
        }
        if (_forceGreetTriggered) return;

        float dist = player->GetDistance(puppet, false, false);
        if (dist <= 250.0f) {
            SKSE::log::info("Player package reached puppet target (dist: {:.1f}) - opening self-dialogue.", dist);
            _forceGreetTriggered = true;
            DialogueManager::GetSingleton().OpenSelfDialogue(puppet);
        }
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            bool isUserAction = false;
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* button = static_cast<const RE::ButtonEvent*>(event);
                isUserAction = button->IsPressed() || button->IsDown() || button->IsHeld();
            } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                const auto* stick = static_cast<const RE::ThumbstickEvent*>(event);
                isUserAction = std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f;
            }
            if (isUserAction) {
                OnUserInput();
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnUserInput() {
        _lastInputTime = std::chrono::steady_clock::now();
        if (_aiActive) {
            StopIdleAI();
        }
    }

    void IdleAIManager::CheckIdleAI() {
        if (!_enabled || !ImpersonationManager::GetSingleton().IsActive()) {
            if (_aiActive) {
                StopIdleAI();
            }
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat() || IsBlockingMenuOpen()) {
            _lastInputTime = std::chrono::steady_clock::now();
            if (_aiActive) {
                StopIdleAI();
            }
            return;
        }

        float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - _lastInputTime).count();
        if (elapsed >= _idleTimeoutSeconds && !_aiActive) {
            StartIdleAI();
        }
    }

    void IdleAIManager::StartIdleAI() {
        if (!ImpersonationManager::GetSingleton().IsActive() || !_enabled) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat() || IsBlockingMenuOpen()) return;

        if (auto* pc = RE::PlayerControls::GetSingleton()) {
            _savedUserRunning = pc->data.running;
        }

        _aiActive = true;
        _forceGreetTriggered = false;
        _lastPackageSyncTime = std::chrono::steady_clock::now();

        SyncDonorPackagesToPlayer();

        player->AllowBleedoutDialogue(true);
        player->SetAIDriven(true);
        player->EvaluatePackage(true, true);

        if (auto* currentPkg = player->GetCurrentPackage()) {
            SKSE::log::info("Player idle timeout reached - engaged autonomous AI package: {:08X} ({})",
                currentPkg->GetFormID(), currentPkg->GetFormEditorID() ? currentPkg->GetFormEditorID() : "No EDID");
        } else {
            SKSE::log::info("Player idle timeout reached - engaged autonomous AI (package evaluates next AI tick).");
        }
    }

    void IdleAIManager::StopIdleAI() {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            player->SetAIDriven(false);
            ClearPlayerPackages();
            player->EvaluatePackage(false, false);
            if (_aiActive) {
                SKSE::log::info("Player input detected - disengaged autonomous AI.");
            }
        }
        if (_aiActive) {
            if (auto* pc = RE::PlayerControls::GetSingleton()) {
                pc->data.autoMove = false;
                pc->data.moveInputVec = { 0.0f, 0.0f };
                pc->data.running = _savedUserRunning;
            }
        }
        _aiActive = false;
        _lastInputTime = std::chrono::steady_clock::now();
    }

    void IdleAIManager::OnImpersonationChanged(bool a_active) {
        if (!a_active) {
            StopIdleAI();
        } else if (auto* pc = RE::PlayerControls::GetSingleton()) {
            _savedUserRunning = pc->data.running;
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager: impersonation active = {}", a_active);
    }

    void IdleAIManager::RetargetAliasPackagesToPuppet() {
        // Packages on the donor's swapped aliases now apply to the player natively. Any that target the
        // player (force-greets, "approach the Dragonborn") would make the player target itself, so point
        // them at the puppet. These forms belong to the alias's quest; base packages are never touched.
        auto& impMgr = ImpersonationManager::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* puppet = impMgr.GetPuppet();
        auto* aliasArray = player->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (!puppet || !aliasArray) return;

        RE::BSReadLockGuard locker(aliasArray->lock);
        for (auto* inst : aliasArray->aliases) {
            if (!inst || !inst->quest || !inst->alias || !inst->instancedPackages) continue;
            if (!impMgr.IsSwappedAlias(inst->quest->GetFormID(), inst->alias->aliasID)) continue;

            for (auto* pkg : *inst->instancedPackages) {
                if (!pkg || pkg->GetFormType() != RE::FormType::Package || !PackageTargetsPlayer(pkg, player)) continue;

                RetargetedPackageTarget rec;
                rec.package = pkg;
                rec.targType = static_cast<std::uint8_t>(pkg->packTarg->targType);
                if (rec.targType == 0) {
                    rec.origHandle = pkg->packTarg->target.handle;
                    pkg->packTarg->target.handle = puppet->GetHandle();
                } else {
                    rec.origObject = pkg->packTarg->target.object;
                    pkg->packTarg->target.object = puppet;
                }
                _retargetedPackages.push_back(rec);
                SKSE::log::info("Retargeted alias package {:08X} from player to puppet {:08X}", pkg->GetFormID(), puppet->GetFormID());
            }
        }
    }

    void IdleAIManager::SyncDonorPackagesToPlayer() {
        auto& impMgr = ImpersonationManager::GetSingleton();
        auto* donor = impMgr.GetDonor();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!impMgr.IsActive() || !donor || !player || !_aiActive) return;

        auto* donorBase = donor->GetActorBase();
        auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
        if (!donorBase || !playerBase) return;

        RetargetAliasPackagesToPuppet();

        // The donor's own (base record) packages go on the player's base record. Alias packages are
        // not copied: the swapped aliases already apply them to the player.
        std::vector<RE::TESPackage*> donorPkgs;
        for (auto* pkg : donorBase->aiPackages.packages) {
            if (!pkg || PackageTargetsPlayer(pkg, player)) continue;
            if (std::find(donorPkgs.begin(), donorPkgs.end(), pkg) == donorPkgs.end()) {
                donorPkgs.push_back(pkg);
            }
        }
        if (auto* defaultSandbox = RE::TESForm::LookupByID<RE::TESPackage>(kDefaultSandboxPackage)) {
            if (std::find(donorPkgs.begin(), donorPkgs.end(), defaultSandbox) == donorPkgs.end()) {
                donorPkgs.push_back(defaultSandbox);
            }
        }

        std::vector<RE::TESPackage*> playerPkgs;
        for (auto* pkg : playerBase->aiPackages.packages) {
            if (pkg) {
                playerPkgs.push_back(pkg);
            }
        }

        if (donorPkgs != playerPkgs) {
            playerBase->aiPackages.packages.clear();
            for (auto it = donorPkgs.rbegin(); it != donorPkgs.rend(); ++it) {
                playerBase->aiPackages.packages.push_front(*it);
            }
            player->EvaluatePackage(false, false);
            SKSE::log::info("Synchronized {} donor AI packages to player.", donorPkgs.size());
        }
    }

    void IdleAIManager::ClearPlayerPackages() {
        for (const auto& entry : _retargetedPackages) {
            auto* pkg = entry.package;
            if (!pkg || !pkg->packTarg) continue;
            if (entry.targType == 0) {
                pkg->packTarg->target.handle = entry.origHandle;
            } else {
                pkg->packTarg->target.object = entry.origObject;
            }
        }
        if (!_retargetedPackages.empty()) {
            SKSE::log::info("ClearPlayerPackages: Restored {} retargeted package targets", _retargetedPackages.size());
        }
        _retargetedPackages.clear();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        if (auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase())) {
            playerBase->aiPackages.packages.clear();
        }
    }

}
