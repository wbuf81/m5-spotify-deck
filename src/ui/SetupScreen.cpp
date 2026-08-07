#include "SetupScreen.h"

#include <M5Unified.h>

#include <lgfx/utility/lgfx_qrcode.h>

#include <cstdio>
#include <cstdlib>

#include "Theme.h"

namespace setupscreen {

void draw(const char *ap, const char *url) {
  using namespace theme;
  M5.Display.fillScreen(pal.bg);

  M5.Display.setFont(fontTitle());
  M5.Display.setTextColor(pal.accent, pal.bg);
  M5.Display.setCursor(12, 10);
  M5.Display.print("SETUP MODE");

  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(pal.text, pal.bg);
  M5.Display.setCursor(12, 44);
  M5.Display.print("1. join this wifi:");
  M5.Display.setTextColor(pal.accent, pal.bg);
  M5.Display.setCursor(24, 60);
  M5.Display.print(ap);

  M5.Display.setTextColor(pal.text, pal.bg);
  M5.Display.setCursor(12, 84);
  M5.Display.print("2. open in a browser:");
  M5.Display.setTextColor(pal.accent, pal.bg);
  M5.Display.setCursor(24, 100);
  M5.Display.print(url);

  M5.Display.setTextColor(pal.dim, pal.bg);
  M5.Display.setCursor(12, 124);
  M5.Display.print("3. enter your keys");
  M5.Display.setCursor(12, 140);
  M5.Display.print("   device reboots when saved");

  M5.Display.setCursor(12, 216);
  M5.Display.print("power off to cancel");

  // QR that joins the network in one scan. Encoded by lgfx_qrcode, but the
  // module bitmap is read DIRECTLY rather than through getModule(): the
  // header's C-mode "Alpine gcc fix" typedefs bool as unsigned char, so the
  // C-compiled accessor returns raw masked values like 0x40 — and a C++
  // caller, whose real bool contract is 0-or-1, tests only the low bit.
  // Result: seven of every eight modules read false, which is also why
  // LGFX's own qrcode() wrapper (a C++ caller too) paints scattered noise.
  // White quiet zone included: readers genuinely fail without one on dark
  // themes.
  char wifi_qr[96];
  std::snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", ap);
  QRCode qr;
  static uint8_t qrbuf[256];  // v3 needs ~106 bytes; static, not stack
  if (lgfx_qrcode_initText(&qr, qrbuf, 3, 0, wifi_qr) == 0) {
    constexpr int MOD = 3, QUIET = 6;
    const int span = qr.size * MOD;  // 29 * 3 = 87
    const int qx = 209, qy = 44;
    M5.Display.fillRect(qx - QUIET, qy - QUIET, span + QUIET * 2,
                        span + QUIET * 2, TFT_WHITE);
    for (int y = 0; y < qr.size; ++y) {
      for (int x = 0; x < qr.size; ++x) {
        const uint32_t off =
            static_cast<uint32_t>(y) * qr.size + static_cast<uint32_t>(x);
        if (qr.modules[off >> 3] & (1u << (7 - (off & 7u)))) {
          M5.Display.fillRect(qx + x * MOD, qy + y * MOD, MOD, MOD, TFT_BLACK);
        }
      }
    }
  }

  M5.Display.setTextColor(pal.dim, pal.bg);
  M5.Display.setCursor(214, 152);
  M5.Display.print("scan to join");
}

}  // namespace setupscreen
