// SD card mount.
//
// Arduino's SD library registers with the ESP-IDF VFS at /sd, so the ordinary
// fopen/mkdir/rename in ArtCache and ArtRenderer work unchanged on the device.
// That is why the storage code needed no porting at all.

#if !defined(EMULATOR)

#include <SD.h>
#include <SPI.h>

#include "../../net/NetLog.h"
#include "Esp32Storage.h"

bool mountStorage() {
  // GPIO4 is the SD chip select on the M5Stack Core.
  if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) {
    NETLOG("SD mount failed — running without an artwork cache");
    return false;
  }
  NETLOG("SD mounted, %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

#endif  // !EMULATOR
