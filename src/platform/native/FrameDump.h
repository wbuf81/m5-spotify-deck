#pragma once

// Emulator-only framebuffer capture. No-op on hardware.
#if defined(EMULATOR)
bool dumpFrameBmp(const char *path);
#else
inline bool dumpFrameBmp(const char *) { return false; }
#endif
