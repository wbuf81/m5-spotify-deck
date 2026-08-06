#pragma once

// Boot banner and heap reporting.
//
// When the hardware arrives, the serial log is the only instrument available.
// Free heap in particular is the number that decides whether this design
// survives on a board with no PSRAM, and the failure mode without it — a silent
// reboot loop — is miserable to diagnose after the fact.

#include <cstdint>

void bootBanner(bool sd_ok);

// Hardware watchdog.
//
// When the display deadlocked on the shared SPI bus, the device did not reboot
// — it sat frozen until someone unplugged it. That is the difference between a
// gadget you own and one you gave away: nobody else is going to power-cycle it
// for you. Every long-lived task subscribes, and a task that stops feeding
// causes a reboot rather than a permanent freeze.
void watchdogBegin();
void watchdogSubscribe();
void watchdogFeed();

// Consecutive abnormal resets, counted in NVS and cleared once the device has
// run for a while. A device that quietly reboots every 20 seconds looks the
// same as one that is simply slow, unless it is counted.
uint32_t crashStreak();

// Call once per loop. Reports free heap periodically, and shouts once if it
// ever falls below a level where TLS would start failing.
void heapTick(uint32_t now_ms);
