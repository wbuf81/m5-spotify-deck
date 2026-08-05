#if !defined(EMULATOR)

#include <Arduino.h>
#include <M5Unified.h>

#include "../../core/Diag.h"

namespace {
// An mbedTLS handshake wants tens of KB. Below this, HTTPS starts failing in
// ways that look like network problems rather than memory ones.
constexpr uint32_t HEAP_FLOOR = 40000;
constexpr uint32_t REPORT_EVERY_MS = 30000;
}  // namespace

void bootBanner(bool sd_ok) {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("=== m5 spotify ===");
  Serial.printf("chip      : %s rev%d, %d core(s) @ %luMHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("flash     : %lu KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("psram     : %s\n", ESP.getPsramSize() ? "present" : "none (expected)");
  Serial.printf("heap free : %lu bytes\n", ESP.getFreeHeap());
  Serial.printf("largest   : %lu bytes contiguous\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  Serial.printf("sd card   : %s\n", sd_ok ? "mounted" : "ABSENT (no artwork cache)");
  Serial.printf("display   : %dx%d\n", M5.Display.width(), M5.Display.height());
  Serial.println("==================");
}

void heapTick(uint32_t now_ms) {
  static uint32_t last = 0;
  static bool warned = false;

  const uint32_t heap = ESP.getFreeHeap();
  if (heap < HEAP_FLOOR && !warned) {
    warned = true;
    Serial.printf("[heap] LOW: %lu bytes free, min ever %lu. TLS will fail.\n",
                  heap, ESP.getMinFreeHeap());
  }

  if (now_ms - last < REPORT_EVERY_MS) return;
  last = now_ms;
  Serial.printf("[heap] free %lu, min %lu, largest block %lu\n", heap,
                ESP.getMinFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

#endif  // !EMULATOR
