#include "NowPlayingScreen.h"

#include <cstdio>
#include <cstring>

#include "../art/ArtRenderer.h"
#include "../net/NetLog.h"
#include "../platform/esp32/Esp32Storage.h"
#include "Anim.h"
#include "TextWrap.h"
#include "Theme.h"

namespace {
constexpr int TITLE_LINES = 3;
constexpr int TITLE_ARTIST_GAP = 6;
}  // namespace

void NowPlayingScreen::release() { scene_.release(); }

void NowPlayingScreen::drawArtRegion(const AppState &st, uint16_t tint) {
  using namespace theme;
  const bool ok = drawArt(st.pb.art_path, ART_X, ART_Y, ART_SIZE);
  if (ok) {
    // Tint comes from ViewManager, already sampled outside the transaction.
    // Sampling it here re-read the card while startWrite() held the shared SPI
    // bus, which deadlocked the UI thread outright — and decoded the same
    // 46KB JPEG a second time for nothing.
    scene_.setTint(tint);

    // A tinted frame, drawn inset over the cover's outer pixels. It ties the
    // artwork to everything else on screen that already follows the album, and
    // costs four line draws. Inset, not outset: the gutter next to the text
    // column must stay empty.
    const uint16_t glow = anim::lerp565(tint, 0xFFFF, 0.25f);
    M5.Display.drawRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, glow);
    M5.Display.drawRect(ART_X + 1, ART_Y + 1, ART_SIZE - 2, ART_SIZE - 2,
                        anim::lerp565(pal.bg, tint, 0.55f));
  } else if (st.pb.art_loading) {
    // The cover is still in flight — say so honestly. This used to fall into
    // the failure branch below, so every uncached album opened with a claim of
    // "no artwork" that then contradicted itself two seconds later.
    M5.Display.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, pal.bg);
    const int cx = ART_X + ART_SIZE / 2, cy = ART_Y + ART_SIZE / 2 - 8;
    // A vinyl record as the placeholder: grooves, tinted label, spindle.
    M5.Display.fillCircle(cx, cy, 56, pal.bar_bg);
    for (int r = 50; r > 26; r -= 6) {
      M5.Display.drawCircle(cx, cy, r, anim::lerp565(pal.bar_bg, pal.dim, 0.4f));
    }
    M5.Display.fillCircle(cx, cy, 20, anim::lerp565(pal.bg, tint, 0.75f));
    M5.Display.fillCircle(cx, cy, 3, pal.bg);
    M5.Display.setFont(fontSmall());
    M5.Display.setTextColor(pal.dim);
    const char *msg = "fetching cover";
    M5.Display.setCursor(cx - M5.Display.textWidth(msg) / 2, cy + 68);
    M5.Display.print(msg);
    scene_.setTint(tint);
  } else {
    // Missing or undecodable artwork must degrade, never blank the device.
    // Name the actual cause: an absent card looks identical to a decode
    // failure otherwise, and sends you debugging the wrong thing.
    M5.Display.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, pal.bar_bg);
    M5.Display.setFont(fontSmall());
    M5.Display.setTextColor(pal.dim);
    const bool no_card = !storageAvailable();
    const char *line1 = no_card ? "no sd card" : "no artwork";
    const char *line2 = no_card ? "fat32 card for art" : "";
    M5.Display.setCursor(ART_X + 8, ART_Y + ART_SIZE / 2 - 10);
    M5.Display.print(line1);
    if (line2[0]) {
      M5.Display.setCursor(ART_X + 8, ART_Y + ART_SIZE / 2 + 4);
      M5.Display.print(line2);
    }
    scene_.setTint(pal.accent);
  }
}

void NowPlayingScreen::drawTextColumn(const AppState &st) {
  using namespace theme;

  // Top-aligned: the space below belongs to the scene panel, so centring would
  // push the text down into it.
  M5.Display.fillRect(COL_X, COL_Y, COL_W, TEXT_H, pal.bg);

  // Line spacing comes from the font itself, so changing face or size cannot
  // silently break the vertical maths.
  M5.Display.setFont(fontTitle());
  const int title_lead = M5.Display.fontHeight();
  char lines[TITLE_LINES][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, COL_W, lines, TITLE_LINES);

  M5.Display.setFont(fontArtist());
  const int artist_lead = M5.Display.fontHeight();
  char alines[2][WRAP_MAX_LINE];
  const int an = wrapText(st.pb.artist, COL_W, alines, 2);

  int y = COL_Y;

  M5.Display.setFont(fontTitle());
  M5.Display.setTextColor(pal.text);
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(COL_X, y);
    M5.Display.print(lines[i]);
    y += title_lead;
  }

  y += TITLE_ARTIST_GAP;
  M5.Display.setFont(fontArtist());
  M5.Display.setTextColor(pal.dim);
  for (int i = 0; i < an; ++i) {
    M5.Display.setCursor(COL_X, y);
    M5.Display.print(alines[i]);
    y += artist_lead;
  }
}

void NowPlayingScreen::render(const AppState &st, uint32_t now_ms,
                              uint16_t tint) {
  using namespace theme;
  (void)now_ms;

  const bool album_changed = std::strcmp(last_album_, st.pb.album_id) != 0 ||
                             st.pb.art_loading != last_art_loading_;
  const bool track_changed = std::strcmp(last_track_, st.pb.track_id) != 0;

  M5.Display.startWrite();

  if (force_) {
    // Only down to the strip: everything below STRIP_Y belongs to StatusStrip,
    // which ViewManager invalidates alongside this screen.
    M5.Display.fillRect(0, 0, SCREEN_W, STRIP_Y, pal.bg);
    scene_.invalidate();
  }

  if (force_ || album_changed) drawArtRegion(st, tint);
  if (force_ || track_changed) {
    drawTextColumn(st);
    scene_.onTrackChange(st.pb.track_id);  // a new song gets a new scene
  }

  // Every frame: continuously animated, and the one element whose whole job is
  // to look alive.
  scene_.render(st.pb.is_playing, st.pb.volume_pct, st.pb.progress_ms,
                st.pb.duration_ms, now_ms);

  // A link problem shows as a small amber marker. Top-LEFT corner: the
  // top-right is the battery badge's plate, which repainted over the marker
  // every second — an offline device looked fine.
  if (force_ || st.link != last_link_) {
    M5.Display.fillRect(2, 2, 4, 4,
                        st.link == LinkStatus::Online ? pal.bg : pal.warn);
  }

  M5.Display.endWrite();

  setStr(last_album_, ID_LEN, st.pb.album_id);
  setStr(last_track_, ID_LEN, st.pb.track_id);
  last_art_loading_ = st.pb.art_loading;
  last_link_ = st.link;
  force_ = false;
}
