#pragma once

// Boot banner and heap reporting.
//
// When the hardware arrives, the serial log is the only instrument available.
// Free heap in particular is the number that decides whether this design
// survives on a board with no PSRAM, and the failure mode without it — a silent
// reboot loop — is miserable to diagnose after the fact.

#include <cstdint>

void bootBanner(bool sd_ok);

// Call once per loop. Reports free heap periodically, and shouts once if it
// ever falls below a level where TLS would start failing.
void heapTick(uint32_t now_ms);
