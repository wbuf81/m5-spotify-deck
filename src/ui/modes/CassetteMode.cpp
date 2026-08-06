#include "CassetteMode.h"

#include <cmath>
#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../Anim.h"
#include "../Crt.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {
// Fills the screen like every other mode. The first pass left a 24px margin of
// bare background on each side, which read as the render having shrunk rather
// than as deliberate framing.
constexpr int SHELL_X = 8, SHELL_Y = 14, SHELL_W = 304, SHELL_H = 192;
constexpr int WIN_X = SHELL_X + 34, WIN_Y = SHELL_Y + 82;
constexpr int WIN_W = SHELL_W - 68, WIN_H = 92;
constexpr int HUB_Y = WIN_Y + (WIN_H / 2);
constexpr int HUB_L_X = WIN_X + 58, HUB_R_X = WIN_X + WIN_W - 58;
constexpr int HUB_R = 14;
constexpr int TAPE_MIN = 16, TAPE_MAX = 42;
}  // namespace

void CassetteMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(theme::pal.bg);

  const uint16_t shell = anim::lerp565(theme::pal.bg, ctx.tint, 0.22f);
  const uint16_t edge = anim::lerp565(theme::pal.bg, ctx.tint, 0.65f);

  M5.Display.fillRoundRect(SHELL_X, SHELL_Y, SHELL_W, SHELL_H, 8, shell);
  M5.Display.drawRoundRect(SHELL_X, SHELL_Y, SHELL_W, SHELL_H, 8, edge);

  // Label card, with the cover as the sleeve art.
  M5.Display.fillRect(SHELL_X + 14, SHELL_Y + 12, SHELL_W - 28, 62,
                      theme::pal.bg);
  M5.Display.drawRect(SHELL_X + 14, SHELL_Y + 12, SHELL_W - 28, 62, edge);
  drawArt(ctx.art_path, SHELL_X + 18, SHELL_Y + 16, 54);

  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[2][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, 210, lines, 2);
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(SHELL_X + 80, SHELL_Y + 18 + i * 17);
    M5.Display.print(lines[i]);
  }
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 210, lines, 1);
  M5.Display.setCursor(SHELL_X + 80, SHELL_Y + 56);
  M5.Display.print(lines[0]);

  // Window the reels sit behind.
  M5.Display.fillRoundRect(WIN_X, WIN_Y, WIN_W, WIN_H, 4, theme::pal.bg);
  M5.Display.drawRoundRect(WIN_X, WIN_Y, WIN_W, WIN_H, 4, edge);

  crt::apply(0, 0, 320, 240);
  last_sec_ = -1;
  last_ms_ = 0;
}

void CassetteMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  const float dt =
      last_ms_ == 0 ? 0.016f : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;
  if (st.pb.is_playing) spin_ += dt * 2.2f;

  const float p = st.pb.duration_ms == 0
                      ? 0.0f
                      : std::fmin(1.0f, float(st.pb.progress_ms) / st.pb.duration_ms);

  // Tape leaves the left reel and gathers on the right. Radii are the progress.
  const float rl = TAPE_MAX - (TAPE_MAX - TAPE_MIN) * p;
  const float rr = TAPE_MIN + (TAPE_MAX - TAPE_MIN) * p;
  const uint16_t tape = anim::lerp565(theme::pal.bg, theme::pal.text, 0.28f);

  struct Reel { int x; float r; float dir; };
  const Reel reels[2] = {{HUB_L_X, rl, 1.0f}, {HUB_R_X, rr, -1.0f}};

  for (const auto &reel : reels) {
    // Clear only the reel's own footprint at maximum radius.
    M5.Display.fillCircle(reel.x, HUB_Y, TAPE_MAX + 1, theme::pal.bg);
    M5.Display.fillCircle(reel.x, HUB_Y, static_cast<int>(reel.r), tape);
    M5.Display.drawCircle(reel.x, HUB_Y, static_cast<int>(reel.r), ctx.tint);
    M5.Display.fillCircle(reel.x, HUB_Y, HUB_R, theme::pal.bg);
    M5.Display.drawCircle(reel.x, HUB_Y, HUB_R, ctx.tint);

    // Three spokes make the rotation visible; a plain disc would look static.
    for (int s = 0; s < 3; ++s) {
      const float a = spin_ * reel.dir + s * 2.0944f;
      M5.Display.drawLine(reel.x, HUB_Y,
                          reel.x + static_cast<int>(std::cos(a) * (HUB_R - 2)),
                          HUB_Y + static_cast<int>(std::sin(a) * (HUB_R - 2)),
                          ctx.tint);
    }
  }

  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  if (sec != last_sec_) {
    last_sec_ = sec;
    char buf[16], pad[16];
    M5.Display.setFont(theme::fontSmall());
    M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
    formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
    std::snprintf(pad, sizeof(pad), "%-7s", buf);
    M5.Display.setCursor(14, 220);
    M5.Display.print(pad);
    formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
    std::snprintf(pad, sizeof(pad), "%7s", buf);
    M5.Display.setCursor(320 - 14 - (7 * 6), 220);
    M5.Display.print(pad);
  }
}
