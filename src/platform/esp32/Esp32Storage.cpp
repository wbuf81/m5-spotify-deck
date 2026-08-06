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

namespace {
bool g_mounted = false;
}

bool storageAvailable() { return g_mounted; }

bool mountStorage() {
  // GPIO4 is the SD chip select on the M5Stack Core.
  if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) {
    // Distinguish the two causes: the driver logs "no valid FAT volume" when
    // a card is present but formatted exFAT, which is how most cards over 32GB
    // ship. Saying "absent" there sends you to check the slot instead of the
    // format.
    NETLOG("SD not mounted — card missing, or not FAT32 (exFAT will not work)");
    g_mounted = false;
    return false;
  }
  NETLOG("SD mounted, %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
  g_mounted = true;
  return true;
}

#endif  // !EMULATOR
