#include "PixelAlbumMode.h"

#include <cmath>
#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../Anim.h"
#include "../Crt.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {
// 80x60 cells of 4px fills 320x240 exactly.
//
// The first pass used 8px cells and an RGB332 palette, which is NES territory:
// 40 visible columns and 256 colours. The SNES ran 256x224 with 15-bit colour,
// so the look wants the opposite of what that did — finer cells AND a richer
// palette. 4px lands between "obviously pixel art" and "just a small photo".
// 40 rows of art: the shared StatusStrip owns y192+, and the text band above
// it needs 32px, so the cover gets 160.
constexpr int COLS = 80, ROWS = 40, CELL = 4;
constexpr int PANEL_Y = 160;

// 4x4 Bayer, only lightly applied now.
constexpr int BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// 4 bits per channel, roughly the SNES's 15-bit colour space rather than the
// 8-bit RGB332 this used before. Banding is mild at this depth, so the dither
// is a gentle nudge to break gradients rather than the heavy checkerboard that
// RGB332 needed — that texture was most of what read as "too pixelly".
uint16_t posterize(uint16_t c, int cx, int cy) {
  // Quarter-step, not half. A full half-step is textbook ordered dithering and
  // it is correct for banding, but on a flat bright area — a moon, a sky — it
  // shows as a checkerboard that reads as an artefact rather than as art.
  const int t = (BAYER[cy & 3][cx & 3] - 8) / 2;
  const int r = clamp8((((c >> 11) & 0x1F) << 3) + t) & 0xF0;
  const int g = clamp8((((c >> 5) & 0x3F) << 2) + t) & 0xF0;
  const int b = clamp8(((c & 0x1F) << 3) + t) & 0xF0;
  return M5.Display.color565(r | (r >> 4), g | (g >> 4), b | (b >> 4));
}
}  // namespace

void PixelAlbumMode::release() {
  if (!shimmer_strip_) return;
  shimmer_strip_->deleteSprite();
  delete shimmer_strip_;
  shimmer_strip_ = nullptr;
}

void PixelAlbumMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(theme::pal.bg);

  // Decode once at cell resolution; the panel gets blocks, not a scaled image.
  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  const bool ok = src.createSprite(COLS, COLS);  // square source, cropped below
  if (ok) {
    src.fillSprite(theme::pal.bg);
    drawArtInto(&src, ctx.art_path, 0, 0, COLS);

    // Composite a row of cells in RAM and push it as one blit.
    //
    // Drawing each cell straight to the panel meant 80*60 = 4800 fillRect
    // calls, and every one of those opens and closes its own SPI transaction.
    // It was the slowest view to appear by a wide margin. A 320x4 strip is
    // 2560 bytes and turns the whole cover into 60 transfers.
    //
    // The art is already decoded into src at this point, so no SD read happens
    // inside startWrite — doing that on this board deadlocks the shared bus.
    M5Canvas strip(&M5.Display);
    strip.setColorDepth(16);
    const bool strips = strip.createSprite(COLS * CELL, CELL);
    M5.Display.startWrite();
    for (int y = 0; y < ROWS; ++y) {
      // Centre-crop the square cover into a 4:3 screen rather than stretching.
      const int sy = y + ((COLS - ROWS) / 2);
      for (int x = 0; x < COLS; ++x) {
        const uint16_t c = posterize(src.readPixel(x, sy), x, y);
        cells_[y * COLS + x] = c;  // kept for the shimmer sweep
        if (strips) {
          strip.fillRect(x * CELL, 0, CELL, CELL, c);
        } else {
          M5.Display.fillRect(x * CELL, y * CELL, CELL, CELL, c);
        }
      }
      if (strips) strip.pushSprite(0, y * CELL);
    }
    M5.Display.endWrite();
    if (strips) strip.deleteSprite();
    src.deleteSprite();
    have_cells_ = true;
  }

  // Text sits on a hard-edged band; drop shadows and gradients would fight the
  // 8-bit look.
  M5.Display.fillRect(0, PANEL_Y, 320, theme::STRIP_Y - PANEL_Y, theme::pal.bg);
  // A 2px rule with a cell-aligned notch pattern, so the band edge belongs to
  // the same 4px grid as the artwork instead of being the one hairline on a
  // chunky screen.
  M5.Display.fillRect(0, PANEL_Y, 320, 2, ctx.tint);
  for (int x = 0; x < 320; x += 8) {
    M5.Display.fillRect(x, PANEL_Y + 2, 4, 2, ctx.tint);
  }

  // One size down from the classic faces: the band is 32px tall and the title
  // deserves to fit whole more than it deserves to be large. Full width — the
  // transport that used to share this band lives in the strip below.
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[1][WRAP_MAX_LINE];
  wrapText(st.pb.title, 300, lines, 1);
  M5.Display.setCursor(10, PANEL_Y + 5);
  M5.Display.print(lines[0]);

  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 300, lines, 1);
  M5.Display.setCursor(10, PANEL_Y + 20);
  M5.Display.print(lines[0]);

  // No scanlines over the artwork: the 8px cells are already the texture, and
  // the two together read as interference.
  last_ms_ = 0;
  band_last_row_ = -1000;
}

void PixelAlbumMode::tick(const AppState &st, const ViewCtx &, uint32_t now_ms) {
  // A CRT shimmer: a soft bright band sweeping slowly down the artwork, the
  // way a filmed monitor shows its refresh. Rows are redrawn from the kept
  // cell buffer, so the sweep never touches the JPEG decoder. It advances a
  // cell row at a time and only while playing — a paused screen holds still.
  if (!have_cells_) return;
  const float dt =
      last_ms_ == 0 ? 0.016f : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;
  if (!st.pb.is_playing) return;
  clock_ += dt;

  // One full sweep every ~7 seconds, then a rest; cycle 18s.
  constexpr int SWEEP_ROWS = ROWS + 6;
  const float cycle = std::fmod(clock_, 18.0f);
  const int band_row =
      cycle < 7.0f ? static_cast<int>((cycle / 7.0f) * SWEEP_ROWS) - 3 : -1000;
  if (band_row == band_last_row_) return;

  // Repaint the rows the band is leaving and the rows it now brightens.
  if (!shimmer_strip_) {
    shimmer_strip_ = new M5Canvas(&M5.Display);
    shimmer_strip_->setColorDepth(16);
    if (!shimmer_strip_->createSprite(COLS * CELL, CELL)) {
      delete shimmer_strip_;
      shimmer_strip_ = nullptr;
    }
  }
  M5Canvas *stripp = shimmer_strip_;
  const bool strips = stripp != nullptr;
  M5.Display.startWrite();
  for (int y = band_last_row_ - 3; y <= band_row + 3; ++y) {
    if (y < 0 || y >= ROWS) continue;
    const int d = y - band_row;
    // Brightness falls off around the band centre; outside it, plain cells.
    const float lift = (d >= -1 && d <= 1) ? (d == 0 ? 0.30f : 0.14f) : 0.0f;
    for (int x = 0; x < COLS; ++x) {
      uint16_t c = cells_[y * COLS + x];
      if (lift > 0.0f) c = anim::lerp565(c, 0xFFFF, lift);
      if (strips) {
        stripp->fillRect(x * CELL, 0, CELL, CELL, c);
      } else {
        M5.Display.fillRect(x * CELL, y * CELL, CELL, CELL, c);
      }
    }
    if (strips) stripp->pushSprite(0, y * CELL);
  }
  M5.Display.endWrite();
  band_last_row_ = band_row;
}
