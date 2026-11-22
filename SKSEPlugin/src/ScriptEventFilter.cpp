#include "PCH.h"
#include "ScriptEventFilter.h"
#include "AliasSwapManager.h"
#include "Session.h"

#include <array>

namespace InTheirShoes::ScriptEventFilter {

    namespace {
        // The engine sends a reference's events to its scripts and, through SkyrimVM::RelayEvent, to its aliases and
        // magic effects, each by VirtualMachine::SendEvent on the thread that moved the item. The alias receivers are
        // picked then: an event queued while the donor still held an alias runs on that alias's script after the
        // alias has moved to the player, where AsActorReference(self) is the player. Daegon's OnItemRemoved, run for
        // every stack leaving the donor at impersonate, moved her outfit pieces from the player to the player and
        // re-equipped them, over and over.
        thread_local std::uint32_t t_transferDepth = 0;
        thread_local std::uint32_t t_dropped = 0;

        bool IsItemEvent(const RE::BSFixedString& a_name) {
            static const RE::BSFixedString added{ "OnItemAdded" };
            static const RE::BSFixedString removed{ "OnItemRemoved" };
            return a_name == added || a_name == removed;
        }

        bool IsInventoryEvent(const RE::BSFixedString& a_name) {
            static const std::array<RE::BSFixedString, 3> others{ "OnObjectEquipped", "OnObjectUnequipped", "OnContainerChanged" };
            return IsItemEvent(a_name) || std::ranges::contains(others, a_name);
        }

        // The menus the player moves items between its inventory and another's through
        bool IsItemTransferMenuOpen() {
            auto* ui = RE::UI::GetSingleton();
            return ui && (ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) || ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) ||
                             ui->IsMenuOpen(RE::GiftMenu::MENU_NAME));
        }

        // Whether a_handle is the script handle of one of the donor's aliases the player holds
        bool IsSwappedAliasHandle(RE::BSScript::IVirtualMachine* a_vm, RE::VMHandle a_handle) {
            const auto* policy = a_vm ? a_vm->GetObjectHandlePolicy() : nullptr;
            if (!policy || !policy->HandleIsType(RE::BGSRefAlias::VMTYPEID, a_handle)) return false;
            const auto* alias = static_cast<const RE::BGSRefAlias*>(policy->GetObjectForHandle(RE::BGSRefAlias::VMTYPEID, a_handle));
            return alias && alias->owningQuest &&
                   AliasSwapManager::GetSingleton()->IsSwappedAlias(alias->owningQuest->GetFormID(), alias->aliasID);
        }
    }

    TransferScope::TransferScope() {
        ++t_transferDepth;
    }

    TransferScope::~TransferScope() {
        if (--t_transferDepth > 0) return;
        // None dropped would mean the engine sent the events later, from elsewhere, and the scope missed them
        SKSE::log::info("Item transfer: withheld {} item/equip events from scripts", t_dropped);
        t_dropped = 0;
    }

    bool ShouldDrop(RE::BSScript::IVirtualMachine* a_vm, RE::VMHandle a_handle, const RE::BSFixedString& a_eventName) {
        if (!IsInventoryEvent(a_eventName)) return false;
        if (t_transferDepth > 0) {
            ++t_dropped;
            return true;
        }
        // Items the player moves through a container, barter or gift menu are the Dragonborn's doing: the donor never
        // opens those menus itself. Without this, Daegon's alias took back every piece the player put in her outfit
        // chest, since it saw her outfit leave her.
        if (!Session::GetSingleton()->IsActive() || !IsItemEvent(a_eventName) || !IsItemTransferMenuOpen() ||
            !IsSwappedAliasHandle(a_vm, a_handle)) {
            return false;
        }
        SKSE::log::info("Withheld {} from donor alias handle {:X}: the player moved the item through a menu", a_eventName.c_str(), a_handle);
        return true;
    }

}
