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

// The UI task only. It runs at hundreds of frames a second; the longest thing
// it legitimately does is decode a cover off SD, well under a second. Ten
// seconds is far beyond any honest stall and well short of a user's patience.
constexpr uint32_t WDT_TIMEOUT_S = 10;

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
  // Arduino has already initialised the watchdog with its own timeout, and a
  // second init is rejected rather than applied — so deinit first or the
  // configured value here is silently ignored.
  esp_task_wdt_deinit();
  const esp_err_t err = esp_task_wdt_init(WDT_TIMEOUT_S, true);  // panic+reboot
  Serial.printf("watchdog  : %us%s\n", WDT_TIMEOUT_S,
                err == ESP_OK ? "" : " (init FAILED)");

  // Power source and battery, printed once so a device that misbehaves on
  // battery can be told apart from one with a real fault.
  const int8_t lvl = M5.Power.getBatteryLevel();
  const int32_t mv = M5.Power.getBatteryVoltage();
  const auto chg = M5.Power.isCharging();
  Serial.printf("power     : type=%d level=%d%% v=%dmV charging=%d\n",
                (int)M5.Power.getType(), (int)lvl, (int)mv, (int)chg);

  // Raw IP5306 registers.
  //
  // getBatteryLevel() reads the chip's LED gauge, which reports a cheerful
  // 100% even with no cell attached, so it cannot answer "is there actually a
  // battery here". These bits can: SYS_CTL0 says whether the boost converter
  // is enabled and whether it stays up once USB is pulled, and SYS_CTL1 says
  // whether the chip shuts itself off when it decides the load is too small.
  constexpr uint8_t IP5306 = 0x75;
  constexpr uint8_t IP5306_SYS_CTL0_R = 0x00;
  constexpr uint32_t I2C_HZ = 100000;
  const uint8_t ctl0 = M5.In_I2C.readRegister8(IP5306, 0x00, I2C_HZ);
  const uint8_t ctl1 = M5.In_I2C.readRegister8(IP5306, 0x01, I2C_HZ);
  const uint8_t ctl2 = M5.In_I2C.readRegister8(IP5306, 0x02, I2C_HZ);
  const uint8_t rd0  = M5.In_I2C.readRegister8(IP5306, 0x70, I2C_HZ);
  const uint8_t rd1  = M5.In_I2C.readRegister8(IP5306, 0x71, I2C_HZ);
  const uint8_t rd4  = M5.In_I2C.readRegister8(IP5306, 0x78, I2C_HZ);
  Serial.printf("ip5306    : CTL0=%02X CTL1=%02X CTL2=%02X RD0=%02X RD1=%02X RD4=%02X\n",
                ctl0, ctl1, ctl2, rd0, rd1, rd4);
  Serial.printf("            boost=%d keep-on=%d charger=%d low-load-off=%d\n",
                (ctl0 >> 5) & 1, (ctl0 >> 1) & 1, (ctl0 >> 4) & 1,
                (ctl1 >> 7) & 1);
  // charge_done is the one that tells us a real cell is on the end of the
  // wire. A 110mAh pack tops up in minutes; one that charges forever without
  // ever completing is usually not there at all.
  // charging= is NOT trustworthy: measured on this board, READ0 bit3 simply
  // mirrors the charger-enable bit in SYS_CTL0 and stays 1 forever. Disabling
  // the charger flips it; unplugging USB would not. Do not build behaviour on
  // it — the strip's battery glyph ignores it entirely.
  Serial.printf("            charging=%d charge_done=%d gauge_raw=%02X\n",
                (rd0 >> 3) & 1, (rd1 >> 3) & 1, rd4 & 0xF0);
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
