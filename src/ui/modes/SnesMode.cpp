#include "SnesMode.h"

#include <cmath>

#include "../../art/ArtRenderer.h"
#include "../../net/NetLog.h"
#include "../Anim.h"
#include "../TextWrap.h"
#include "../Theme.h"

namespace {

constexpr int HORIZON = 64;
constexpr int TEX = 64;             // texture is 64x64, wrap by masking
constexpr int FLOOR_H = theme::STRIP_Y - HORIZON - 1;  // 127 rows

// Fixed point 16.16 throughout the sampler: a float multiply per pixel would
// put the frame budget on the FPU, and the ESP32's FPU is single-issue.
constexpr int32_t FP = 1 << 16;

// Sprite buffers store RGB565 byte-swapped (ready for SPI). Raw texel copies
// between two buffers are consistent either way, but any arithmetic on the
// colour must unswap first and re-swap after — the fog pass without this
// painted a rainbow of nonsense across the horizon, exactly the same trap the
// frame-dump readRect hit months ago.
inline uint16_t unswap(uint16_t c) { return (c >> 8) | (c << 8); }

// Perspective: depth for floor row i (0 = just under the horizon). The +6
// keeps the nearest rows from magnifying single texels into 40px blocks, and
// the 96 sets the camera height — high enough that three-plus tiles fit
// across the bottom row, so the cover stays recognisable.
inline int32_t depthFor(int i) { return (96 * FP) / (i + 6); }

}  // namespace

void SnesMode::release() {
  if (tex_) {
    tex_->deleteSprite();
    delete tex_;
    tex_ = nullptr;
  }
  if (line_) {
    line_->deleteSprite();
    delete line_;
    line_ = nullptr;
  }
}

void SnesMode::enter(const AppState &st, const ViewCtx &ctx) {
  using namespace theme;
  M5.Display.fillRect(0, 0, SCREEN_W, STRIP_Y, pal.bg);

  // Sky: deterministic stars, denser near the horizon like a real skybox.
  for (int i = 0; i < 46; ++i) {
    const int x = (i * 5779) % 320;
    const int y = (i * 2411) % (HORIZON - 6);
    M5.Display.drawPixel(x, y, anim::lerp565(pal.bg, pal.text,
                                             0.2f + 0.5f * ((i % 5) / 5.0f)));
  }

  // Horizon glow: brightest at the line, fading up into the sky.
  for (int i = 0; i < 5; ++i) {
    M5.Display.drawFastHLine(0, HORIZON - i, 320,
                             anim::lerp565(pal.bg, ctx.tint, 0.5f - i * 0.09f));
  }

  // Title in the sky.
  M5.Display.setFont(fontArtist());
  M5.Display.setTextColor(pal.text, pal.bg);
  char lines[1][WRAP_MAX_LINE];
  wrapText(st.pb.title, 300, lines, 1);
  M5.Display.setCursor(10, 8);
  M5.Display.print(lines[0]);
  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(pal.dim, pal.bg);
  wrapText(st.pb.artist, 300, lines, 1);
  M5.Display.setCursor(10, 26);
  M5.Display.print(lines[0]);

  // The cover texture. 64x64 is small enough to sample from cache-resident
  // memory and big enough that the near rows stay recognisable.
  if (!tex_) {
    tex_ = new M5Canvas(&M5.Display);
    tex_->setColorDepth(16);
    if (!tex_->createSprite(TEX, TEX)) {
      NETLOG("snes: texture sprite FAILED");
      delete tex_;
      tex_ = nullptr;
    }
  }
  if (tex_) {
    tex_->fillSprite(pal.bg);
    if (!drawArtInto(tex_, ctx.art_path, 0, 0, TEX)) {
      // No cover yet: a two-tone tint checkerboard, so the plane still runs.
      for (int y = 0; y < TEX; ++y) {
        for (int x = 0; x < TEX; ++x) {
          const bool a = ((x >> 4) ^ (y >> 4)) & 1;
          tex_->drawPixel(x, y,
                          anim::lerp565(pal.bg, ctx.tint, a ? 0.55f : 0.25f));
        }
      }
    }
  }
  // Box-filter the texture into a 16x16 mip. The far rows sample texels many
  // apart, and raw samples up there are pure shimmer — every Mode 7 game hid
  // that region with fog for exactly this reason. Averaged texels plus fog
  // turns the noise band into a clean recede.
  if (tex_) {
    const uint16_t *tb = static_cast<const uint16_t *>(tex_->getBuffer());
    for (int my = 0; my < 16; ++my) {
      for (int mx = 0; mx < 16; ++mx) {
        int r = 0, g = 0, b = 0;
        for (int sy = 0; sy < 4; ++sy) {
          for (int sx = 0; sx < 4; ++sx) {
            const uint16_t c = unswap(tb[(my * 4 + sy) * TEX + mx * 4 + sx]);
            r += (c >> 11) & 0x1F;
            g += (c >> 5) & 0x3F;
            b += c & 0x1F;
          }
        }
        mip_[my * 16 + mx] = unswap(
            static_cast<uint16_t>(((r / 16) << 11) | ((g / 16) << 5) | (b / 16)));
      }
    }
  }

  if (!line_) {
    line_ = new M5Canvas(&M5.Display);
    line_->setColorDepth(16);
    if (!line_->createSprite(SCREEN_W, 1)) {
      NETLOG("snes: line sprite FAILED");
      delete line_;
      line_ = nullptr;
    }
  }

  last_ms_ = 0;
  last_v0_ = -1;  // force the first floor pass
}

void SnesMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  using namespace theme;
  if (!tex_ || !line_) return;

  const float dt =
      last_ms_ == 0 ? 0.016f : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;

  // Volume drives speed, like the starfield. Pausing freezes the plane
  // because the clock stops, and the unchanged-scroll check below then skips
  // the whole pass.
  const float vol = st.pb.volume_pct < 0 ? 0.7f : st.pb.volume_pct / 100.0f;
  if (st.pb.is_playing) clock_ += dt * (18.0f + 46.0f * vol);

  const int32_t v0 = static_cast<int32_t>(clock_ * FP / 8);
  if (v0 == last_v0_) return;
  last_v0_ = v0;

  const uint16_t *texbuf = static_cast<const uint16_t *>(tex_->getBuffer());
  uint16_t *row = static_cast<uint16_t *>(line_->getBuffer());
  const uint16_t sky = pal.bg;
  const float dimf = theme::dimFactor();

  M5.Display.startWrite();
  for (int i = 0; i < FLOOR_H; ++i) {
    const int32_t z = depthFor(i);

    // Distance fog: the far rows blend into the sky, which is both how Mode 7
    // games hid the horizon and free antialiasing here.
    const int fog = i < 34 ? (255 * (34 - i)) / 34 : 0;  // 0..255

    // Far field samples the mip: past this depth adjacent pixels step several
    // texels apart and full-res sampling is shimmer, not detail.
    const bool far_field = z > (3 * FP) / 2;
    const int shift = far_field ? 2 : 0;      // 64 -> 16 texel space
    const int mask = far_field ? 15 : TEX - 1;
    const uint16_t *texrow;
    if (far_field) {
      const int32_t v = (((v0 + z * 24) >> 16) >> shift) & mask;
      texrow = mip_ + v * 16;
    } else {
      const int32_t v = ((v0 + z * 24) >> 16) & mask;
      texrow = texbuf + v * TEX;
    }

    int32_t u = -160 * z;              // texture u at x=0, fixed point
    for (int x = 0; x < SCREEN_W; ++x) {
      const uint16_t c = texrow[((u >> 16) >> shift) & mask];
      if (fog == 0 && dimf >= 0.999f) {
        row[x] = c;  // buffer-domain copy: no unswap needed
      } else {
        // Split (in unswapped space), blend toward sky, scale by dim.
        const uint16_t uc = unswap(c);
        int r = (uc >> 11) & 0x1F, g = (uc >> 5) & 0x3F, b = uc & 0x1F;
        if (fog) {
          const int sr = (sky >> 11) & 0x1F, sg = (sky >> 5) & 0x3F,
                    sb = sky & 0x1F;
          r += ((sr - r) * fog) >> 8;
          g += ((sg - g) * fog) >> 8;
          b += ((sb - b) * fog) >> 8;
        }
        if (dimf < 0.999f) {
          r = static_cast<int>(r * dimf);
          g = static_cast<int>(g * dimf);
          b = static_cast<int>(b * dimf);
        }
        row[x] = unswap(static_cast<uint16_t>((r << 11) | (g << 5) | b));
      }
      u += z;
    }
    line_->pushSprite(0, HORIZON + 1 + i);
  }
  M5.Display.endWrite();
}
