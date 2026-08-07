#pragma once

// Battery state, read from the board's power-management chip.
//
// The Core Basic carries an IP5306, which reports charge in 25% steps and
// cannot report pack voltage at all — getBatteryVoltage() answers 0mV on this
// hardware. So the honest resolution here is four levels, and the percentage
// shown to the user will step 100 -> 75 -> 50 -> 25 rather than count down
// smoothly. Nothing in the UI should imply finer precision than that.

#include <cstdint>

struct PowerInfo {
  int8_t pct = -1;  // -1 when there is no reading at all
};

PowerInfo readPower();

// Configure the PMIC so the board survives losing USB. Call once, after
// M5.begin(), while still on USB — once the rail collapses it is far too late.
void powerBegin();
