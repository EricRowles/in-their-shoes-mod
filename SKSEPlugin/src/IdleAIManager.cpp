#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "DialogueManager.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "RE/P/PlayerControls.h"
#include "RE/T/TESPackage.h"
#include "RE/T/TESQuest.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/B/BGSRefAlias.h"

namespace InTheirShoes {

    void IdleAIManager::Initialize() {
        if (!_initialized) {
            auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton();
            if (inputDispatcher) {
                inputDispatcher->AddEventSink<RE::InputEvent*>(this);
                SKSE::log::info("IdleAIManager registered input event sink successfully.");
            } else {
                SKSE::log::warn("IdleAIManager failed to get BSInputDeviceManager singleton.");
            }

            _initialized = true;
        }
    }

    void IdleAIManager::Update() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->IsInCombat()) {
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
        } else {
            CheckIdleAI();
        }

        if (_aiActive.load(std::memory_order_acquire) && ImpersonationManager::GetSingleton().IsActive()) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<float>(now - _lastPackageSyncTime).count() >= 1.5f) {
                _lastPackageSyncTime = now;
                SyncDonorUpdatesToPlayer();
            }

            // Check if player has an active dialogue/force-greet package targeting puppet
            auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
            if (player && puppet) {
                auto* curPkg = player->GetCurrentPackage();
                if (curPkg && curPkg->packTarg) {
                    if (curPkg->packTarg->target.handle == puppet->GetHandle() || curPkg->packTarg->target.object == puppet) {
                        float dist = player->GetDistance(puppet, false, false);
                        if (dist <= 250.0f) {
                            SKSE::log::info("Player force-greet package reached puppet target (dist: {:.1f}) - opening self-dialogue.", dist);
                            DialogueManager::GetSingleton().OpenSelfDialogue(puppet);
                        }
                    }
                }
            }
        }
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        bool hasUserAction = false;
        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* button = static_cast<const RE::ButtonEvent*>(event);
                if (button->IsPressed() || button->IsDown() || button->IsHeld()) {
                    hasUserAction = true;
                    break;
                }
            } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                const auto* stick = static_cast<const RE::ThumbstickEvent*>(event);
                if (std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f) {
                    hasUserAction = true;
                    break;
                }
            }
        }

        if (hasUserAction) {
            OnUserInput();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnUserInput() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _lastInputTime = std::chrono::steady_clock::now();
        if (_aiActive.load(std::memory_order_acquire)) {
            StopIdleAI();
        }
    }

    void IdleAIManager::CheckIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_enabled.load(std::memory_order_acquire) || !ImpersonationManager::GetSingleton().IsActive()) {
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat() || IsBlockingMenuOpen()) {
            _lastInputTime = std::chrono::steady_clock::now();
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        float timeout = _idleTimeoutSeconds.load(std::memory_order_acquire);
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _lastInputTime).count();

        if (elapsed >= timeout && !_aiActive.load(std::memory_order_acquire)) {
            StartIdleAI();
        }
    }

    void IdleAIManager::StartIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!ImpersonationManager::GetSingleton().IsActive() || !_enabled.load(std::memory_order_acquire)) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat() || IsBlockingMenuOpen()) return;

        if (auto* pc = RE::PlayerControls::GetSingleton()) {
            _savedUserRunning = pc->data.running;
        }

        _aiActive.store(true, std::memory_order_release);
        _lastPackageSyncTime = std::chrono::steady_clock::now();

        SyncDonorUpdatesToPlayer();

        player->AllowBleedoutDialogue(true);
        player->SetAIDriven(true);
        player->EvaluatePackage(true, true);

        if (auto* currentPkg = player->GetCurrentPackage()) {
            SKSE::log::info("Player idle timeout reached - engaged autonomous AI package: {:08X} ({})",
                currentPkg->GetFormID(), currentPkg->GetFormEditorID() ? currentPkg->GetFormEditorID() : "No EDID");
        } else {
            SKSE::log::warn("Player idle timeout reached - engaged autonomous AI, but GetCurrentPackage() returned null!");
        }
    }

    void IdleAIManager::StopIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pc = RE::PlayerControls::GetSingleton();
        if (player) {
            player->SetAIDriven(false);
            ClearPlayerPackages();
            player->EvaluatePackage(false, false);

            if (_aiActive.load(std::memory_order_acquire)) {
                SKSE::log::info("Player input detected - disengaged autonomous AI.");
            }
        }
        if (pc) {
            pc->data.autoMove = false;
            pc->data.moveInputVec = { 0.0f, 0.0f };
            pc->data.running = _savedUserRunning;
        }
        _aiActive.store(false, std::memory_order_release);
        _lastInputTime = std::chrono::steady_clock::now();
    }

    void IdleAIManager::OnImpersonationChanged(bool a_active) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_active) {
            StopIdleAI();
        } else {
            if (auto* pc = RE::PlayerControls::GetSingleton()) {
                _savedUserRunning = pc->data.running;
            }
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager: impersonation active = {}", a_active);
    }

    std::vector<RE::TESPackage*> IdleAIManager::GetAliasPackages(RE::Actor* a_actor) const {
        std::vector<RE::TESPackage*> result;
        if (!a_actor) return result;

        struct AliasPkgEntry {
            std::int8_t questPriority;
            RE::TESPackage* pkg;
        };
        std::vector<AliasPkgEntry> entries;

        auto collectFromActor = [&](RE::Actor* actor, bool a_onlySwapped) {
            if (!actor) return;
            auto* aliasArray = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (aliasArray) {
                RE::BSReadLockGuard locker(aliasArray->lock);
                for (auto* inst : aliasArray->aliases) {
                    if (!inst || !inst->quest || !inst->instancedPackages) continue;
                    if (!inst->quest->IsRunning()) continue;
                    if (a_onlySwapped &&
                        (!inst->alias || !ImpersonationManager::GetSingleton().IsSwappedAlias(inst->quest->GetFormID(), inst->alias->aliasID))) {
                        continue;
                    }

                    std::int8_t priority = inst->quest->data.priority;
                    for (auto* pkg : *inst->instancedPackages) {
                        if (pkg && pkg->GetFormType() == RE::FormType::Package) {
                            entries.push_back({ priority, pkg });
                        }
                    }
                }
            }
        };

        collectFromActor(a_actor, false);

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && a_actor != player) {
            collectFromActor(player, true);
        }

        std::stable_sort(entries.begin(), entries.end(), [](const AliasPkgEntry& a, const AliasPkgEntry& b) {
            return a.questPriority > b.questPriority;
        });

        for (const auto& entry : entries) {
            if (std::find(result.begin(), result.end(), entry.pkg) == result.end()) {
                result.push_back(entry.pkg);
            }
        }

        return result;
    }

    void IdleAIManager::SyncDonorUpdatesToPlayer() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!ImpersonationManager::GetSingleton().IsActive()) return;
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        auto* player = ImpersonationManager::GetSingleton().GetPlayer();
        if (!donor || !player || !_aiActive.load(std::memory_order_acquire)) return;

        auto* donorBase = donor->GetActorBase();
        auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
        if (!donorBase || !playerBase) return;

        std::vector<RE::TESPackage*> donorPkgs;
        std::unordered_set<RE::TESPackage*> retargetablePkgs;

        for (auto* pkg : GetAliasPackages(donor)) {
            if (pkg) {
                donorPkgs.push_back(pkg);
                retargetablePkgs.insert(pkg);
            }
        }

        for (auto* pkg : donorBase->aiPackages.packages) {
            if (pkg && std::find(donorPkgs.begin(), donorPkgs.end(), pkg) == donorPkgs.end()) {
                donorPkgs.push_back(pkg);
                retargetablePkgs.insert(pkg);
            }
        }

        if (auto* defaultSandbox = RE::TESForm::LookupByID<RE::TESPackage>(0x000BFB6B)) {
            if (std::find(donorPkgs.begin(), donorPkgs.end(), defaultSandbox) == donorPkgs.end()) {
                donorPkgs.push_back(defaultSandbox);
            }
        }

        if (auto* puppet = ImpersonationManager::GetSingleton().GetPuppet()) {
            auto playerHandle = player->GetHandle();
            auto puppetHandle = puppet->GetHandle();
            RE::TESForm* playerBaseForm = player->GetBaseObject();

            for (auto* pkg : retargetablePkgs) {
                if (!pkg || !pkg->packTarg) continue;

                bool alreadyTracked = false;
                for (const auto& entry : _retargetedPackages) {
                    if (entry.package == pkg) {
                        alreadyTracked = true;
                        break;
                    }
                }

                std::uint8_t tType = static_cast<std::uint8_t>(pkg->packTarg->targType);
                if (tType == 0) {
                    if (pkg->packTarg->target.handle == playerHandle) {
                        if (!alreadyTracked) {
                            RetargetedPackageTarget rec;
                            rec.package = pkg;
                            rec.packageFormID = pkg->GetFormID();
                            rec.targType = tType;
                            rec.origHandle = pkg->packTarg->target.handle;
                            _retargetedPackages.push_back(rec);
                        }
                        pkg->packTarg->target.handle = puppetHandle;
                        SKSE::log::info("SyncDonorUpdatesToPlayer: Retargeted package {:08X} handle target from Player to Puppet {:08X}",
                            pkg->GetFormID(), puppet->GetFormID());
                    }
                } else if (tType == 1) {
                    if (pkg->packTarg->target.object == player || pkg->packTarg->target.object == playerBaseForm) {
                        if (!alreadyTracked) {
                            RetargetedPackageTarget rec;
                            rec.package = pkg;
                            rec.packageFormID = pkg->GetFormID();
                            rec.targType = tType;
                            rec.origObject = pkg->packTarg->target.object;
                            _retargetedPackages.push_back(rec);
                        }
                        pkg->packTarg->target.object = puppet;
                        SKSE::log::info("SyncDonorUpdatesToPlayer: Retargeted package {:08X} object target from Player to Puppet {:08X}",
                            pkg->GetFormID(), puppet->GetFormID());
                    }
                }
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
            SKSE::log::info("Synchronized updated donor AI packages to player.");
        }
    }

    void IdleAIManager::ClearPlayerPackages() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (const auto& entry : _retargetedPackages) {
            RE::TESPackage* pkg = entry.package;
            if (!pkg && entry.packageFormID != 0) {
                pkg = RE::TESForm::LookupByID<RE::TESPackage>(entry.packageFormID);
            }
            if (pkg && pkg->packTarg) {
                if (entry.targType == 0) {
                    pkg->packTarg->target.handle = entry.origHandle;
                } else if (entry.targType == 1) {
                    pkg->packTarg->target.object = entry.origObject;
                }
                SKSE::log::info("ClearPlayerPackages: Restored original target on package {:08X}", pkg->GetFormID());
            }
        }
        _retargetedPackages.clear();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        if (auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase())) {
            playerBase->aiPackages.packages.clear();
        }
    }

}
