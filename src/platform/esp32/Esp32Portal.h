#pragma once

// The captive setup portal. Opens an access point, serves the credential
// form, saves to NVS, reboots. Never returns.
//
// Runs INSTEAD of the app: no net task, no watchdog subscription, nothing
// else on the radio. Entered when the device has no complete configuration,
// or when button A is held through power-on.

#include "../../config/DeviceConfig.h"

[[noreturn]] void runSetupPortal(const DeviceConfig &current);
