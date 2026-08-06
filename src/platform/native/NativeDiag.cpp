// The host has no interesting memory story; these exist so main stays
// platform-free.
#if defined(EMULATOR)

#include <cstdio>

#include "../../core/Diag.h"

void bootBanner(bool) {}
void heapTick(uint32_t) {}
void watchdogBegin() {}
void watchdogSubscribe() {}
void watchdogFeed() {}
uint32_t crashStreak() { return 0; }

#endif  // EMULATOR
