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
// the 96 sets the camera height.
inline int32_t depthFor(int i) { return (96 * FP) / (i + 6); }

// How much the cover is magnified on the plane.
//
// At 1 the bottom row fits about 3.6 tiles, and at that size nobody could tell
// which album they were looking at — the whole point of putting the cover on
// the floor was lost. At 2 it fits about 1.8, the near rows show real detail,
// and the nearest texel is about 2.8 pixels across. That is chunky, and it is
// meant to be: this is the look the view is copying.
//
// It divides the texel step rather than scaling the texture, so it costs
// nothing per pixel and needs no extra memory.
constexpr int32_t TEX_SCALE = 2;

// How fast the plane recedes. This is the number that decides whether anyone
// can tell which album is on the floor, and it is not the tile size.
//
// The anisotropy at floor row i is DEPTH_K / (i + 6). At the old value of 24 the
// bottom row came out at 0.18: a texel was 2.8 pixels wide and 15 pixels deep,
// so every cover was smeared into vertical streaks and doubling the tile size
// only made the streaks wider. At 132 the bottom row is 1.0, the texels are
// square there, and the picture appears.
//
// The cost is at the other end. Anisotropy at the horizon row is DEPTH_K / 6, so
// raising it compresses the far field hard and the band under the horizon gets
// busy. That band is already fogged and already samples the 16x16 mip, which is
// what keeps it from turning into pure noise.
constexpr int32_t DEPTH_K = 132;

// The scroll offset advances by this much per unit of clock, and wraps here.
//
// Both must exist. The rate carries the scroll into the same texel space the
// sampler uses, so the plane keeps the speed it always had: TEX_SCALE makes
// each texel twice as wide, DEPTH_K makes it far shorter in depth, and the two
// corrections together cancel out on screen.
//
// The wrap is the load-bearing one. The offset used to be derived from a clock
// that runs for as long as playback does, and clock * 8192 crosses INT32_MAX
// after about 70 minutes — signed overflow, and the floor jumps. Scaling it for
// DEPTH_K would have brought that down to about 26 minutes, which anyone
// listening to an album would hit. Wrapping at exactly one texture is invisible
// because the plane tiles, and it keeps the arithmetic in range forever.
constexpr float V_PER_CLOCK = (FP / 8.0f) * DEPTH_K / 24.0f / TEX_SCALE;
constexpr float V_SPAN = static_cast<float>(TEX) * FP;  // one full texture

// The racer. Hand-built from primitives at a depth-driven scale: a wedge
// body, canopy, rear wing, and a jet flame that flickers on the clock. The
// shadow is what seats it ON the plane.
//
// It renders into a small keyed sprite, and the floor pass composites the
// sprite's rows into each scanline before pushing. The first version drew it
// AFTER the 127 row blits, which meant the car was absent from the panel for
// most of every frame — visible as constant blinking.
constexpr int CAR_W = 48, CAR_H = 34;
constexpr int CAR_OX = CAR_W / 2;   // sprite coords of the car's anchor
constexpr int CAR_OY = 18;
constexpr uint16_t CAR_KEY = 0xF81F;  // magenta: never produced by the art

struct RacerPose {
  int cx, cy;   // anchor on screen
  float s;      // depth scale
};

RacerPose racerPose(float clock) {
  // Smooth wander: two incommensurate sines each for x and depth.
  const float wx = std::sin(clock * 0.61f) * 0.7f + std::sin(clock * 1.53f) * 0.3f;
  const float wd = std::sin(clock * 0.37f + 2.0f) * 0.5f +
                   std::sin(clock * 0.91f) * 0.2f;
  RacerPose p;
  const int row = 72 + static_cast<int>(wd * 26.0f);   // floor row 39..97
  p.s = 0.55f + (row - 40) / 60.0f * 0.75f;
  p.cx = 160 + static_cast<int>(wx * (60.0f + 40.0f * p.s));
  p.cy = HORIZON + 1 + row;
  return p;
}

void renderRacerSprite(M5Canvas *cv, float s, float clock, float dimf) {
  auto C = [dimf](uint32_t rgb) {
    return M5.Display.color565(
        static_cast<uint8_t>(((rgb >> 16) & 0xFF) * dimf),
        static_cast<uint8_t>(((rgb >> 8) & 0xFF) * dimf),
        static_cast<uint8_t>((rgb & 0xFF) * dimf));
  };
  auto px = [s](float v) { return static_cast<int>(v * s + 0.5f); };
  const int cx = CAR_OX, cy = CAR_OY;

  cv->fillSprite(CAR_KEY);

  // Shadow, slightly ahead of the body the way a low sun would put it.
  cv->fillEllipse(cx, cy + px(3), px(16), px(4), C(0x060810));

  // Rear wing.
  cv->fillRect(cx - px(15), cy - px(6), px(30), px(3), C(0x8890A0));
  cv->fillRect(cx - px(15), cy - px(9), px(4), px(6), C(0x606878));
  cv->fillRect(cx + px(11), cy - px(9), px(4), px(6), C(0x606878));

  // Body: blue wedge, nose toward the viewer.
  cv->fillTriangle(cx - px(11), cy - px(5), cx + px(11), cy - px(5), cx,
                   cy + px(9), C(0x2A5ADF));
  cv->fillRect(cx - px(11), cy - px(6), px(22), px(4), C(0x3A6AEF));
  // Nose stripe and canopy.
  cv->fillTriangle(cx - px(3), cy - px(2), cx + px(3), cy - px(2), cx,
                   cy + px(7), C(0xE8B020));
  cv->fillEllipse(cx, cy - px(3), px(4), px(2), C(0xB8E8FF));

  // Jet flame, flickering: two frames on the clock, hidden at tiny scale.
  if (s > 0.6f) {
    const bool flick = (static_cast<int>(clock * 14.0f) & 1) != 0;
    const uint32_t flame = flick ? 0xFFB030 : 0xFF6820;
    cv->fillRect(cx - px(6), cy - px(9), px(4), px(2), C(flame));
    cv->fillRect(cx + px(2), cy - px(9), px(4), px(2), C(flame));
  }
}

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
  if (car_) {
    car_->deleteSprite();
    delete car_;
    car_ = nullptr;
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
  if (!car_) {
    car_ = new M5Canvas(&M5.Display);
    car_->setColorDepth(16);
    if (!car_->createSprite(CAR_W, CAR_H)) {
      NETLOG("snes: car sprite FAILED");
      delete car_;
      car_ = nullptr;
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
  const float dclock = st.pb.is_playing ? dt * (18.0f + 46.0f * vol) : 0.0f;
  clock_ += dclock;
  scroll_ += dclock * V_PER_CLOCK;
  while (scroll_ >= V_SPAN) scroll_ -= V_SPAN;

  const int32_t v0s = static_cast<int32_t>(scroll_);
  if (v0s == last_v0_) return;
  last_v0_ = v0s;

  const uint16_t *texbuf = static_cast<const uint16_t *>(tex_->getBuffer());
  uint16_t *row = static_cast<uint16_t *>(line_->getBuffer());
  const uint16_t sky = pal.bg;
  const float dimf = theme::dimFactor();

  // The racer, rendered into its keyed sprite BEFORE the floor pass so each
  // scanline can composite it in. Key compare happens in buffer domain, so
  // the key constant is pre-swapped like the pixels are.
  const RacerPose pose = racerPose(clock_ * 0.12f);
  const uint16_t *carbuf = nullptr;
  int car_x0 = 0, car_y0 = 0;
  if (car_) {
    renderRacerSprite(car_, pose.s, clock_ * 0.12f, dimf);
    carbuf = static_cast<const uint16_t *>(car_->getBuffer());
    car_x0 = pose.cx - CAR_OX;
    car_y0 = pose.cy - CAR_OY;
  }
  const uint16_t key_raw = static_cast<uint16_t>((CAR_KEY >> 8) | (CAR_KEY << 8));

  M5.Display.startWrite();
  for (int i = 0; i < FLOOR_H; ++i) {
    const int32_t z = depthFor(i);

    // Distance fog: the far rows blend into the sky, which is both how Mode 7
    // games hid the horizon and free antialiasing here.
    const int fog = i < 34 ? (255 * (34 - i)) / 34 : 0;  // 0..255

    // Texels crossed per screen pixel. Everything below steps in zs, not z,
    // which is the whole of the magnification.
    const int32_t zs = z / TEX_SCALE;

    // Far field samples the mip: past this depth adjacent pixels step several
    // texels apart and full-res sampling is shimmer, not detail. Testing zs
    // rather than z moves the switch further away on its own — magnified texels
    // stay clean for more rows, so more of the floor shows the real cover.
    const bool far_field = zs > (3 * FP) / 2;
    const int shift = far_field ? 2 : 0;      // 64 -> 16 texel space
    const int mask = far_field ? 15 : TEX - 1;
    const uint16_t *texrow;
    if (far_field) {
      const int32_t v = (((v0s + zs * DEPTH_K) >> 16) >> shift) & mask;
      texrow = mip_ + v * 16;
    } else {
      const int32_t v = ((v0s + zs * DEPTH_K) >> 16) & mask;
      texrow = texbuf + v * TEX;
    }

    int32_t u = -160 * zs;             // texture u at x=0, fixed point
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
      u += zs;
    }

    // Composite the racer's slice of this scanline.
    if (carbuf) {
      const int sy = (HORIZON + 1 + i) - car_y0;
      if (sy >= 0 && sy < CAR_H) {
        const uint16_t *crow = carbuf + sy * CAR_W;
        for (int cxp = 0; cxp < CAR_W; ++cxp) {
          const int dx = car_x0 + cxp;
          if (dx < 0 || dx >= SCREEN_W) continue;
          const uint16_t c = crow[cxp];
          if (c != key_raw) row[dx] = c;
        }
      }
    }

    line_->pushSprite(0, HORIZON + 1 + i);
  }
  M5.Display.endWrite();
}
