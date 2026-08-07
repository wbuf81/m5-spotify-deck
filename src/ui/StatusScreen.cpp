#include "StatusScreen.h"

#include <cstring>

#include "Anim.h"
#include "daisy/DaisySprite.h"
#include "Theme.h"

namespace {

// Beacon: concentric rings pulsing outward from the centre. Continuous, so it
// reads as "still trying" rather than as a frozen error page.
constexpr int BEACON = 96;
constexpr uint32_t PULSE_MS = 1800;
constexpr int RING_COUNT = 3;

struct Copy {
  const char *headline;
  const char *detail;
  bool alarming;
};

Copy copyFor(const AppState &st) {
  switch (st.link) {
    case LinkStatus::Booting:
    case LinkStatus::Connecting:
      return {"CONNECTING", "reaching spotify", false};
    case LinkStatus::Offline:
      return {"OFFLINE", "no network", true};
    case LinkStatus::AuthError:
      return {"AUTH ERROR", "retrying", true};
    case LinkStatus::ReauthNeeded:
      return {"RE-AUTH", "run get_refresh_token.py", true};
    case LinkStatus::Online:
      break;
  }
  if (!st.pb.has_device) return {"NO DEVICE", "start playback anywhere", false};
  return {"NOTHING PLAYING", "daisy is napping too", false};
}

}  // namespace

bool StatusScreen::shouldShow(const AppState &st) {
  return st.link != LinkStatus::Online || !st.pb.has_track;
}

void StatusScreen::release() {
  if (!beacon_) return;
  beacon_->deleteSprite();
  delete beacon_;
  beacon_ = nullptr;
}

void StatusScreen::drawBeacon(const AppState &st, uint32_t now_ms) {
  using namespace theme;

  if (!beacon_) {
    beacon_ = new M5Canvas(&M5.Display);
    beacon_->setColorDepth(16);
    if (!beacon_->createSprite(BEACON, BEACON)) {
      // Out of contiguous heap. The headline and detail text still render, so
      // the user still learns what is wrong — they just lose the animation.
      delete beacon_;
      beacon_ = nullptr;
      return;
    }
  }

  const Copy c = copyFor(st);
  const uint16_t tint = c.alarming ? pal.warn : pal.accent;
  const float cx = BEACON / 2.0f;
  const float cy = BEACON / 2.0f;

  beacon_->fillSprite(pal.bg);

  // Rings are evenly offset in phase so one is always emerging as another
  // fades, which keeps the motion continuous rather than strobing.
  const float base = static_cast<float>((now_ms % PULSE_MS)) / PULSE_MS;
  for (int i = 0; i < RING_COUNT; ++i) {
    float t = base + static_cast<float>(i) / RING_COUNT;
    if (t > 1.0f) t -= 1.0f;

    const float eased = anim::easeOutCubic(t);
    const int r = static_cast<int>(anim::lerp(8.0f, (BEACON / 2.0f) - 2.0f, eased));
    // Fade toward the background as the ring expands; no alpha on the panel.
    const uint16_t col = anim::lerp565(tint, pal.bg, eased);
    beacon_->drawCircle(static_cast<int>(cx), static_cast<int>(cy), r, col);
    if (r > 1) beacon_->drawCircle(static_cast<int>(cx), static_cast<int>(cy), r - 1, col);
  }

  // Solid core, breathing gently.
  const float breathe = 0.5f + 0.5f * anim::pulse(base);
  const int core = static_cast<int>(anim::lerp(5.0f, 8.0f, breathe));
  beacon_->fillCircle(static_cast<int>(cx), static_cast<int>(cy), core, tint);

  if (c.alarming) {
    // A slash through the core distinguishes a fault from a normal wait at a
    // glance, without relying on colour alone.
    beacon_->drawLine(static_cast<int>(cx - 11), static_cast<int>(cy - 11),
                      static_cast<int>(cx + 11), static_cast<int>(cy + 11), pal.bg);
    beacon_->drawLine(static_cast<int>(cx - 11), static_cast<int>(cy - 10),
                      static_cast<int>(cx + 11), static_cast<int>(cy + 12), pal.bg);
  }

  beacon_->pushSprite((SCREEN_W - BEACON) / 2, 18);
}

void StatusScreen::render(const AppState &st, uint32_t now_ms) {
  using namespace theme;

  const Copy c = copyFor(st);
  const bool changed = force_ || st.link != last_link_ ||
                       st.pb.has_track != last_has_track_;

  if (changed) {
    M5.Display.fillScreen(pal.bg);

    M5.Display.setFont(fontTitle());
    M5.Display.setTextColor(c.alarming ? pal.warn : pal.text, pal.bg);
    int w = M5.Display.textWidth(c.headline);
    M5.Display.setCursor((SCREEN_W - w) / 2, 132);
    M5.Display.print(c.headline);

    M5.Display.setFont(fontSmall());
    M5.Display.setTextColor(pal.dim, pal.bg);
    w = M5.Display.textWidth(c.detail);
    M5.Display.setCursor((SCREEN_W - w) / 2, 164);
    M5.Display.print(c.detail);

    // The strip becomes the button legend, aligned with the three physical
    // buttons below the panel. It shows exactly when a guest is staring at an
    // idle device wondering what the buttons do — which the old "m5 spotify"
    // wordmark answered for no one.
    M5.Display.fillRect(0, STRIP_Y, SCREEN_W, STRIP_H, pal.strip);
    M5.Display.setFont(fontSmall());
    struct { int cx; const char *tap; const char *hold; } keys[3] = {
        {65, "<< prev", "hold vol-"},
        {160, "play", "hold like"},
        {255, "next >>", "hold vol+"},
    };
    for (const auto &k : keys) {
      M5.Display.setTextColor(pal.dim, pal.strip);
      w = M5.Display.textWidth(k.tap);
      M5.Display.setCursor(k.cx - w / 2, STRIP_Y + 8);
      M5.Display.print(k.tap);
      M5.Display.setTextColor(pal.bar_bg, pal.strip);
      w = M5.Display.textWidth(k.hold);
      M5.Display.setCursor(k.cx - w / 2, STRIP_Y + 26);
      M5.Display.print(k.hold);
    }
  }

  // Nothing playing while healthy is not a fault, so it does not get the
  // radar beacon — it gets Daisy asleep, breathing on the sleep cycle. The
  // beacon stays for connecting/offline/auth, where "still trying" is the
  // message.
  if (st.link == LinkStatus::Online) {
    constexpr int DS = 2;
    constexpr int DX = (SCREEN_W - daisy::SPRITE_COLS * DS) / 2;
    constexpr int DY = 22;
    const int f = daisy::frameAt(daisy::Daisy_Sleep, now_ms);
    if (changed || daisy_frame_ < 0) {
      release();  // the beacon sprite is not needed while she is on screen
      daisy::draw(daisy::Daisy_Sleep, f, DX, DY, DS, pal.bg);
    } else if (f != daisy_frame_) {
      daisy::drawDiff(daisy::Daisy_Sleep, daisy_frame_, daisy::Daisy_Sleep, f,
                      DX, DY, DS, pal.bg);
    }
    daisy_frame_ = f;
  } else {
    daisy_frame_ = -1;
    // The beacon animates continuously, so it repaints every frame regardless.
    drawBeacon(st, now_ms);
  }

  last_link_ = st.link;
  last_has_track_ = st.pb.has_track;
  force_ = false;
}
