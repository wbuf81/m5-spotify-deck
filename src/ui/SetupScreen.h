#pragma once

// The screen shown while the setup portal is running.
//
// Drawn once — the portal loop is busy serving DNS and HTTP, so this stays
// static. Shared with the emulator (EMU_PORTAL=1) so the layout is testable
// without hardware; only the access point itself is hardware-only.

namespace setupscreen {

// ap: the network name to join; url: where the form lives.
void draw(const char *ap, const char *url);

}  // namespace setupscreen
