#include "PixelAlbumMode.h"

#include <cstdio>

#include "../../art/ArtRenderer.h"
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
}

void PixelAlbumMode::tick(const AppState &, const ViewCtx &, uint32_t) {
  // Transport, heart, volume and timecodes all live in the shared StatusStrip.
}
