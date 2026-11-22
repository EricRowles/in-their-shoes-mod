#pragma once

#include <cstdint>

namespace InTheirShoes::ScriptEventFilter {

    // Keeps scripts from seeing item moves that aren't the donor's doing (#52). Mods guard a follower's inventory
    // from alias scripts (Daegon re-equips her outfit and takes back pieces that leave her), and the player holds
    // the donor's aliases, so those guards fight the plugin's own transfers and the player's menus.

    // While one is alive, the item and equip events the engine sends to scripts from this thread are dropped. Wraps
    // the plugin's item transfers between the Dragonborn, the donor, the puppet and the storage chest: the items
    // change hands, not owners, so neither side's scripts should react.
    class TransferScope {
    public:
        TransferScope();
        ~TransferScope();
        TransferScope(const TransferScope&) = delete;
        TransferScope& operator=(const TransferScope&) = delete;
    };

    // Whether a script event shouldn't reach a_handle. Called from the VirtualMachine::SendEvent hook (Papyrus.cpp),
    // on any thread.
    bool ShouldDrop(RE::BSScript::IVirtualMachine* a_vm, RE::VMHandle a_handle, const RE::BSFixedString& a_eventName);

}
