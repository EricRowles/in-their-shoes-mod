#pragma once

namespace InTheirShoes::NativeRedirect {

    // While impersonating, ObjectReference and Actor natives that other mods' scripts call on the donor (or on
    // the puppet while it speaks for the donor), or pass it to, run on the player instead. Teammate status and
    // relationships stay on the donor. Installed once, after every plugin has registered its natives (kDataLoaded).
    // The redirect runs on VM threads and reads only Session. Can be turned off in the MCM; nothing it does is
    // kept, so it can be toggled mid-session.
    void Install();

}
