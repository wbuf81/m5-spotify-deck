#pragma once

// Mounts the SD card. Returns false if absent, in which case artwork simply
// never caches and the UI falls back to a flat block — the device must still
// run without a card.
#if !defined(EMULATOR)
bool mountStorage();

// Whether the card actually mounted. The UI uses this to say "no sd card"
// rather than the bare "no artwork" it would otherwise show, which looks like
// a decode failure and sends you debugging the wrong thing.
bool storageAvailable();
#else
inline bool mountStorage() { return true; }
inline bool storageAvailable() { return true; }
#endif
