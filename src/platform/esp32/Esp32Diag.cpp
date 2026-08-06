#if !defined(EMULATOR)

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <M5Unified.h>

#include "../../core/Diag.h"
#include "Esp32Storage.h"

namespace {
// An mbedTLS handshake wants tens of KB. Below this, HTTPS starts failing in
// ways that look like network problems rather than memory ones.
constexpr uint32_t HEAP_FLOOR = 40000;
constexpr uint32_t REPORT_EVERY_MS = 30000;

// Generous on purpose. The UI normally runs at hundreds of frames a second,
// but an artwork decode and a TLS handshake can each take a second or more, and
// a watchdog that false-trips is worse than none.
constexpr uint32_t WDT_TIMEOUT_S = 30;

// Survive this long and the boot is considered good.
constexpr uint32_t STABLE_AFTER_MS = 120000;

uint32_t g_crash_streak = 0;
bool g_streak_cleared = false;

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "external";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "PANIC (crash)";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG (hang)";
    case ESP_RST_WDT:      return "other watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (bad power)";
    case ESP_RST_DEEPSLEEP:return "deep sleep";
    default:               return "unknown";
  }
}
}  // namespace

uint32_t crashStreak() { return g_crash_streak; }

void watchdogBegin() {
  esp_task_wdt_init(WDT_TIMEOUT_S, true);  // true: panic and reboot on timeout
}

void watchdogSubscribe() { esp_task_wdt_add(nullptr); }

void watchdogFeed() {
  esp_task_wdt_reset();

  // Clear the crash streak only after the device has proven it can stay up.
  // Clearing at boot would erase the evidence of a reboot loop.
  if (!g_streak_cleared && millis() > STABLE_AFTER_MS) {
    g_streak_cleared = true;
    Preferences p;
    if (p.begin("m5spotify", false)) {
      p.putUInt("crashes", 0);
      p.end();
    }
  }
}

void bootBanner(bool sd_ok) {
  Serial.begin(115200);
  delay(50);

  const esp_reset_reason_t reason = esp_reset_reason();
  const bool abnormal = reason == ESP_RST_PANIC || reason == ESP_RST_TASK_WDT ||
                        reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT ||
                        reason == ESP_RST_BROWNOUT;

  Preferences p;
  if (p.begin("m5spotify", false)) {
    g_crash_streak = p.getUInt("crashes", 0);
    if (abnormal) {
      ++g_crash_streak;
      p.putUInt("crashes", g_crash_streak);
    }
    p.end();
  }

  Serial.println();
  Serial.println("=== m5 spotify ===");
  Serial.printf("reset     : %s\n", resetReasonName(reason));
  if (g_crash_streak > 0) {
    Serial.printf("crashes   : %lu consecutive abnormal resets\n",
                  (unsigned long)g_crash_streak);
  }
  Serial.printf("chip      : %s rev%d, %d core(s) @ %luMHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("flash     : %lu KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("psram     : %s\n", ESP.getPsramSize() ? "present" : "none (expected)");
  Serial.printf("heap free : %lu bytes\n", ESP.getFreeHeap());
  Serial.printf("largest   : %lu bytes contiguous\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  if (sd_ok) {
    Serial.printf("sd card   : mounted at %luMHz\n", storageClockHz() / 1000000);
  } else {
    Serial.printf("sd card   : not mounted (missing, or not FAT32)\n");
  }
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
