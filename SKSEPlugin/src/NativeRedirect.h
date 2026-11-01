#pragma once

namespace InTheirShoes::NativeRedirect {

    // While impersonating, ObjectReference and Actor natives that other mods' scripts call on the donor (or on
    // the puppet while it speaks for the donor), or pass it to, run on the player instead. Faction and teammate
    // state stays on the donor. Installed once, after every plugin has registered its natives (kDataLoaded).
    // The redirect runs on VM threads and reads only Session.
    void Install();

}
