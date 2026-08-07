#if !defined(EMULATOR)

#include <M5Unified.h>

#include "../../core/Power.h"

namespace {
constexpr uint8_t IP5306_ADDR = 0x75;
constexpr uint8_t IP5306_SYS_CTL0 = 0x00;
constexpr uint8_t IP5306_SYS_CTL1 = 0x01;
constexpr uint32_t I2C_HZ = 100000;

// SYS_CTL0 bit 1, "boost output normally open": keeps the boost converter
// running when VIN goes away. Bit 2 auto-powers the rail when a load appears.
constexpr uint8_t BOOST_KEEP_ON = 1 << 1;
constexpr uint8_t AUTO_POWER_ON_LOAD = 1 << 2;

// SYS_CTL1 bit 5 controls whether the boost may be switched on by the KEY /
// stays available once VIN is gone. Documentation for this part disagrees with
// itself on the exact wording, so this is set empirically alongside the CTL0
// bits rather than on authority. Bit 2 ("boost after VIN pull-out" in
// M5Stack's own driver) already reads 1 on this board.
constexpr uint8_t BOOST_SWITCHABLE = 1 << 5;
// Light-load auto-shutdown: the chip cuts the boost when it decides nothing is
// drawing. Kept explicitly off.
constexpr uint8_t LOW_LOAD_SHUTDOWN = 1 << 7;
}  // namespace

// Unplugging USB killed the device outright, because the chip comes up with
// SYS_CTL0 = 0x31 — boost enabled, but NOT latched on. The moment VIN went
// away the 5V rail collapsed and the ESP32 died mid-instruction; the battery
// was never the problem. M5Stack's own library writes these bits at startup
// and M5Unified does not, so nothing had ever set them here.
//
// The register is volatile and reverts whenever the PMIC itself loses power,
// so this has to run on every boot rather than once at the factory.
void powerBegin() {
  const uint8_t before = M5.In_I2C.readRegister8(IP5306_ADDR, IP5306_SYS_CTL0, I2C_HZ);
  const uint8_t want = before | BOOST_KEEP_ON | AUTO_POWER_ON_LOAD;
  bool ok = true;
  if (want != before) {
    ok = M5.In_I2C.writeRegister8(IP5306_ADDR, IP5306_SYS_CTL0, want, I2C_HZ);
  }
  const uint8_t after = M5.In_I2C.readRegister8(IP5306_ADDR, IP5306_SYS_CTL0, I2C_HZ);

  const uint8_t c1_before = M5.In_I2C.readRegister8(IP5306_ADDR, IP5306_SYS_CTL1, I2C_HZ);
  const uint8_t c1_want =
      (c1_before | BOOST_SWITCHABLE) & static_cast<uint8_t>(~LOW_LOAD_SHUTDOWN);
  if (c1_want != c1_before) {
    ok = M5.In_I2C.writeRegister8(IP5306_ADDR, IP5306_SYS_CTL1, c1_want, I2C_HZ) && ok;
  }
  const uint8_t c1_after = M5.In_I2C.readRegister8(IP5306_ADDR, IP5306_SYS_CTL1, I2C_HZ);

  Serial.printf("battery   : SYS_CTL0 %02X -> %02X  SYS_CTL1 %02X -> %02X%s\n",
                before, after, c1_before, c1_after,
                (after & BOOST_KEEP_ON) ? "" : "  (KEEP-ON NOT SET)");
  if (!ok) Serial.println("battery   : PMIC write FAILED");
}

PowerInfo readPower() {
  PowerInfo p;
  p.pct = M5.Power.getBatteryLevel();
  // isCharging() is deliberately not read: on this board it mirrors the
  // charger-enable bit and reports true forever. See the boot banner probe.
  return p;
}

#endif  // !EMULATOR
