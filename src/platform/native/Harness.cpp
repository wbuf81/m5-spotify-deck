#if defined(EMULATOR)

#include "Harness.h"

#include <M5Unified.h>
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>

#include "../../core/CommandQueue.h"
#include "../../core/ProgressClock.h"
#include "../../ui/Crt.h"
#include "../../ui/Theme.h"
#include "../../ui/ViewManager.h"

// Wired up by main so the harness can drive them.
extern ViewManager g_screen;
extern ProgressClock g_clock;
extern void harnessSubmit(const Command &c);

namespace harness {
namespace {

Overrides g_ov;
bool g_overlay = true;
bool g_on = false;
bool g_checked = false;

uint8_t g_prev[SDL_NUM_SCANCODES] = {};
char g_note[64] = "ready";

const char *MODE_NAMES[] = {"classic", "pixel",     "gameboy", "cassette",
                            "scoreboard", "cyberdeck", "synthwave"};

const char *LINK_NAMES[] = {"booting", "connecting", "online",
                            "offline", "autherror",  "reauth"};

void note(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(g_note, sizeof(g_note), fmt, ap);
  va_end(ap);
}

bool justPressed(const Uint8 *ks, SDL_Scancode sc) {
  const bool down = ks[sc] != 0;
  const bool was = g_prev[sc] != 0;
  g_prev[sc] = down ? 1 : 0;
  return down && !was;
}

}  // namespace

bool active() {
  if (!g_checked) {
    g_checked = true;
    g_on = std::getenv("EMU_HARNESS") != nullptr;
  }
  return g_on;
}

bool overlayVisible() { return g_overlay; }

void printKeymap() {
  std::fprintf(stderr,
      "\n"
      "  ── review harness ───────────────────────────────────────────\n"
      "   1..7   pin view: classic pixel gameboy cassette scoreboard\n"
      "                    cyberdeck synthwave\n"
      "   0      unpin (rotate per track)\n"
      "   [ ]    previous / next fixture track\n"
      "   , .    scrub -10s / +10s      m  jump to near the end\n"
      "   p      play / pause           f  toggle like (animates)\n"
      "   v b    volume -5 / +5         t  fire a toast\n"
      "   n      cycle link state       k  toggle 'nothing playing'\n"
      "   d      cycle brightness       c  toggle CRT scanlines\n"
      "   h      hide/show this bar     q  quit\n"
      "  ─────────────────────────────────────────────────────────────\n"
      "  Click the window first, or the keys go to your terminal.\n\n");
}

// EMU_WINCHECK: inject a plain '2' and report the SDL window size either side
// of it. This is the only way to catch Panel_sdl's built-in shortcuts stealing
// a key — the framebuffer stays 320x240 no matter what the window does, so no
// pixel assertion can see it.
void windowSizeCheck(uint32_t now_ms) {
  static int phase = 0;
  static uint32_t t0 = 0;
  static int w0 = 0, h0 = 0;
  if (t0 == 0) t0 = now_ms;

  // Panel_sdl keeps its SDL_Window private, and the id is not guaranteed to be
  // 1, so scan the low ids for it.
  SDL_Window *win = nullptr;
  for (Uint32 id = 1; id <= 12 && !win; ++id) win = SDL_GetWindowFromID(id);
  if (!win) {
    if (now_ms - t0 > 4000) {
      std::fprintf(stderr, "[wincheck] no SDL window found; cannot verify\n");
      std::exit(2);
    }
    return;
  }

  if (phase == 0 && now_ms - t0 > 1200) {
    SDL_GetWindowSize(win, &w0, &h0);
    std::fprintf(stderr, "[wincheck] before: %dx%d\n", w0, h0);

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.type = SDL_KEYDOWN;
    e.key.state = SDL_PRESSED;
    // windowID matters: Panel_sdl looks the monitor up by it, and an event
    // without one is dropped before the shortcut handler ever sees it. The
    // first version of this probe omitted it and "passed" against the unfixed
    // build, which is worse than no check at all.
    e.key.windowID = SDL_GetWindowID(win);
    // '3', not '2'. On a Retina display the library computes
    // nw = frame_width * scale * window/renderer, and at scale 2 that lands
    // back on 320 exactly — the one value where the bug is invisible.
    e.key.keysym.sym = SDLK_3;
    e.key.keysym.scancode = SDL_SCANCODE_3;
    e.key.keysym.mod = KMOD_NONE;
    SDL_PushEvent(&e);
    phase = 1;
  } else if (phase == 1 && now_ms - t0 > 2000) {
    int w1, h1;
    SDL_GetWindowSize(win, &w1, &h1);
    std::fprintf(stderr, "[wincheck] after plain '3': %dx%d  -> %s\n", w1, h1,
                 (w1 == w0 && h1 == h0) ? "UNCHANGED (ok)" : "RESIZED (bug)");
    phase = 2;
    std::exit((w1 == w0 && h1 == h0) ? 0 : 1);
  }
}

const Overrides &update(AppState *st, uint32_t now_ms) {
  if (std::getenv("EMU_WINCHECK")) windowSizeCheck(now_ms);

  const Uint8 *ks = SDL_GetKeyboardState(nullptr);
  if (!ks) return g_ov;

  static const SDL_Scancode MODE_KEYS[7] = {
      SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
      SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7};
  for (int i = 0; i < 7; ++i) {
    if (justPressed(ks, MODE_KEYS[i])) {
      g_screen.pin(i - 1);  // -1 == classic
      note("pinned %s", MODE_NAMES[i]);
    }
  }
  if (justPressed(ks, SDL_SCANCODE_0)) {
    g_screen.pin(-2);
    note("rotating per track");
  }

  if (justPressed(ks, SDL_SCANCODE_RIGHTBRACKET)) {
    harnessSubmit({CommandType::Next, 0});
    note("next track");
  }
  if (justPressed(ks, SDL_SCANCODE_LEFTBRACKET)) {
    harnessSubmit({CommandType::Previous, 0});
    note("previous track");
  }

  // Scrubbing is the point of the harness for anything progress-driven — the
  // cassette reels, the sinking sun, the scoreboard clock.
  if (justPressed(ks, SDL_SCANCODE_PERIOD)) {
    uint32_t v = g_clock.value() + 10000;
    if (st->pb.duration_ms && v > st->pb.duration_ms) v = st->pb.duration_ms;
    g_clock.reset(v);
    note("scrub +10s");
  }
  if (justPressed(ks, SDL_SCANCODE_COMMA)) {
    const uint32_t v = g_clock.value();
    g_clock.reset(v > 10000 ? v - 10000 : 0);
    note("scrub -10s");
  }
  if (justPressed(ks, SDL_SCANCODE_M)) {
    if (st->pb.duration_ms > 6000) g_clock.reset(st->pb.duration_ms - 6000);
    note("jump to end");
  }

  if (justPressed(ks, SDL_SCANCODE_P)) {
    st->pb.is_playing = !st->pb.is_playing;
    harnessSubmit({CommandType::PlayPause, 0});
    note(st->pb.is_playing ? "play" : "pause");
  }
  if (justPressed(ks, SDL_SCANCODE_F)) {
    st->pb.liked = !st->pb.liked;
    st->pb.liked_known = true;
    st->settle_liked.arm(now_ms, 1500);
    harnessSubmit({CommandType::ToggleLike, 0});
    note(st->pb.liked ? "liked" : "unliked");
  }
  if (justPressed(ks, SDL_SCANCODE_V) || justPressed(ks, SDL_SCANCODE_B)) {
    const int d = ks[SDL_SCANCODE_B] ? 5 : -5;
    int v = (st->pb.volume_pct < 0 ? 50 : st->pb.volume_pct) + d;
    st->pb.volume_pct = v < 0 ? 0 : (v > 100 ? 100 : v);
    st->settle_volume.arm(now_ms, 1500);
    note("volume %d%%", st->pb.volume_pct);
  }
  if (justPressed(ks, SDL_SCANCODE_T)) {
    st->showToast("Saved to Liked Songs", now_ms);
    note("toast");
  }

  if (justPressed(ks, SDL_SCANCODE_N)) {
    static int i = 0;
    static const int CYCLE[] = {-1, (int)LinkStatus::Connecting,
                                (int)LinkStatus::Offline,
                                (int)LinkStatus::AuthError,
                                (int)LinkStatus::ReauthNeeded};
    i = (i + 1) % 5;
    g_ov.link = CYCLE[i];
    note("link: %s", g_ov.link < 0 ? "auto" : LINK_NAMES[g_ov.link]);
  }
  if (justPressed(ks, SDL_SCANCODE_K)) {
    g_ov.force_notrack = !g_ov.force_notrack;
    note(g_ov.force_notrack ? "nothing playing" : "track restored");
  }

  if (justPressed(ks, SDL_SCANCODE_D)) {
    static int i = 0;
    static const int LEVELS[] = {-1, theme::BRIGHT_ACTIVE, theme::BRIGHT_IDLE,
                                 theme::BRIGHT_OFF};
    i = (i + 1) % 4;
    g_ov.brightness = LEVELS[i];
    note("brightness: %s",
         g_ov.brightness < 0 ? "auto" : (g_ov.brightness == 0 ? "off" : "fixed"));
  }
  if (justPressed(ks, SDL_SCANCODE_C)) {
    crt::setEnabled(!crt::enabled());
    g_screen.invalidate();
    note("crt %s", crt::enabled() ? "on" : "off");
  }
  if (justPressed(ks, SDL_SCANCODE_H)) {
    g_overlay = !g_overlay;
    g_screen.invalidate();
  }
  if (justPressed(ks, SDL_SCANCODE_Q)) std::exit(0);

  return g_ov;
}

void drawOverlay() {
  if (!g_overlay) return;

  // Deliberately overwrites the top of whatever mode is showing. This is a dev
  // tool, and knowing what is pinned matters more than those ten rows.
  M5.Display.fillRect(0, 0, 320, 11, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 2);

  const int p = g_screen.pinned();
  const int c = g_screen.current();
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s%-10s %-22s", p == -2 ? "~" : "*",
                MODE_NAMES[(c < -1 ? -1 : c) + 1], g_note);
  M5.Display.print(buf);
}

}  // namespace harness

#endif  // EMULATOR
