#pragma once

// Mounts the SD card. Returns false if absent, in which case artwork simply
// never caches and the UI falls back to a flat block — the device must still
// run without a card.
#if !defined(EMULATOR)
bool mountStorage();
#else
inline bool mountStorage() { return true; }
#endif
