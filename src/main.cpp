// M5Stack Spotify desk controller.
//
// Wiring only — the behaviour lives in the modules this pulls together.
//
// Controls (emulator keys / device buttons):
//   Left  / A          previous          hold: volume down
//   Space / S          play / pause      hold: save to Liked Songs
//   Right / D          next              hold: volume up
//
// Data source is chosen at startup: the real Spotify Web API when
// src/config/secrets.h exists, otherwise the offline fixture source. Force the
// fixtures with EMU_FAKE=1.

#include <M5Unified.h>

#include "core/AppState.h"
#include "core/Clock.h"
#include "core/CommandQueue.h"
#include "input/Buttons.h"
#include "platform/native/FrameDump.h"
#include "sources/FakeSource.h"
#include "core/ProgressClock.h"
#include "ui/NowPlayingScreen.h"
#include "ui/StatusScreen.h"
#include "ui/Theme.h"

#if defined(EMULATOR)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

#if defined(__has_include)
#if __has_include("config/secrets.h")
#include "config/secrets.h"
#define HAVE_SECRETS 1
#endif
#endif

#if defined(HAVE_SECRETS)
#include "net/NetLog.h"
#include "net/NetWorker.h"
#include "platform/esp32/Esp32Storage.h"
#define CAN_GO_LIVE 1
#endif

namespace {

// Settle window from the spec: a field changed optimistically is protected from
// an in-flight poll response for this long.
constexpr uint32_t SETTLE_MS = 1500;

// Volume changes during a hold are coalesced into one call this long after the
// last adjustment, instead of one call per 5% step.
constexpr uint32_t VOLUME_COALESCE_MS = 300;
constexpr int VOLUME_STEP = 5;

constexpr uint32_t DIM_AFTER_MS = 30000;
constexpr uint32_t SLEEP_AFTER_IDLE_MS = 180000;

AppState g_state;
CommandQueue<> g_cmds;
FakeSource g_fake;
NowPlayingScreen g_screen;
StatusScreen g_status;
bool g_showing_status = false;
Buttons g_buttons;

bool g_live = false;
#if defined(CAN_GO_LIVE)
NetWorker *g_net = nullptr;
#endif

uint32_t g_last_interaction_ms = 0;
uint32_t g_last_frame_ms = 0;
uint32_t g_not_playing_since_ms = 0;
uint8_t g_brightness = theme::BRIGHT_ACTIVE;

ProgressClock g_clock;

bool g_volume_dirty = false;
uint32_t g_volume_changed_at_ms = 0;

// Routes a command to whichever source is running.
void submit(const Command &c) {
#if defined(CAN_GO_LIVE)
  if (g_live && g_net) {
    g_net->submit(c);
    return;
  }
#endif
  g_cmds.push(c);
}

// Applies an optimistic local edit. In live mode it must happen under the net
// thread's lock, or a merge landing mid-edit could read a half-updated state.
template <typename Fn>
void mutateState(Fn fn) {
#if defined(CAN_GO_LIVE)
  if (g_live && g_net) {
    g_net->mutate(fn);
    return;
  }
#endif
  fn(g_state);
}

void nudgeVolume(int delta, uint32_t now_ms) {
  bool unsupported = false;
  mutateState([&](AppState &st) {
    if (st.pb.volume_pct < 0) {
      unsupported = true;
      return;
    }
    int v = st.pb.volume_pct + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    st.pb.volume_pct = v;
    st.settle_volume_until_ms = now_ms + SETTLE_MS;
  });

  if (unsupported) {
    mutateState([&](AppState &st) { st.showToast("Volume not supported", now_ms); });
    return;
  }
  g_volume_dirty = true;
  g_volume_changed_at_ms = now_ms;
}

void handleButtons(uint32_t now_ms) {
  const BtnEvent a = g_buttons.event(Btn::A);
  const BtnEvent b = g_buttons.event(Btn::B);
  const BtnEvent c = g_buttons.event(Btn::C);

  if (a == BtnEvent::Tap) {
    submit({CommandType::Previous, 0});
  } else if (a == BtnEvent::LongStart || a == BtnEvent::LongRepeat) {
    nudgeVolume(-VOLUME_STEP, now_ms);
  }

  if (b == BtnEvent::Tap) {
    // Optimistic: flip locally so the glyph responds before the network does.
    mutateState([&](AppState &st) {
      st.pb.is_playing = !st.pb.is_playing;
      st.settle_playing_until_ms = now_ms + SETTLE_MS;
    });
    submit({CommandType::PlayPause, 0});
  } else if (b == BtnEvent::LongStart) {
    // Like is one action, so repeats while held are ignored.
    mutateState([&](AppState &st) {
      st.pb.liked = !st.pb.liked;
      // We just set it, so we know it even if the API will not tell us.
      st.pb.liked_known = true;
      st.settle_liked_until_ms = now_ms + SETTLE_MS;
      st.showToast(st.pb.liked ? "Saved to Liked Songs" : "Removed from Liked",
                   now_ms);
    });
    submit({CommandType::ToggleLike, 0});
  }

  if (c == BtnEvent::Tap) {
    submit({CommandType::Next, 0});
  } else if (c == BtnEvent::LongStart || c == BtnEvent::LongRepeat) {
    nudgeVolume(VOLUME_STEP, now_ms);
  }
}

void updateBrightness(uint32_t now_ms) {
  uint8_t want;
  const bool idle_long =
      !g_state.pb.is_playing &&
      (now_ms - g_not_playing_since_ms) >= SLEEP_AFTER_IDLE_MS;

  if ((now_ms - g_last_interaction_ms) < DIM_AFTER_MS) {
    want = theme::BRIGHT_ACTIVE;
  } else if (idle_long) {
    want = theme::BRIGHT_OFF;
  } else {
    want = theme::BRIGHT_IDLE;
  }

  if (want != g_brightness) {
    g_brightness = want;
    theme::applyBrightness(want);
    g_screen.invalidate();  // palette changed, everything on screen is stale
  }
}

}  // namespace

void setup(void) {
  auto cfg = M5.config();
  M5.begin(cfg);

  theme::applyBrightness(theme::BRIGHT_ACTIVE);
  M5.Display.fillScreen(theme::pal.bg);

  const uint32_t now = nowMs();
  g_last_interaction_ms = now;
  g_last_frame_ms = now;
  g_not_playing_since_ms = now;

#if defined(CAN_GO_LIVE)
#if defined(EMULATOR)
  const bool force_fake = std::getenv("EMU_FAKE") != nullptr;
  const char *cache_dir = ".cache/art";
#else
  const bool force_fake = false;
  // Arduino's SD library mounts at /sd through the ESP-IDF VFS, so the same
  // POSIX file code serves both platforms.
  mountStorage();
  const char *cache_dir = "/sd/art";
#endif
  if (!force_fake) {
    g_live = true;
    g_net = new NetWorker(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET,
                          SPOTIFY_REFRESH_TOKEN);
    g_net->start(cache_dir, WIFI_SSID, WIFI_PASSWORD);
    NETLOG("NetWorker started");
  }
#endif

  if (!g_live) {
    g_fake.begin(&g_state, now);
#if defined(EMULATOR)
    std::fprintf(stderr, "[src] fixtures\n");
#endif
  }

#if defined(EMULATOR)
  // EMU_TOAST=<text> raises a toast at startup so the toast layout, and the
  // restore of the row underneath it, can be captured without a keypress.
  if (const char *t = std::getenv("EMU_TOAST")) {
    mutateState([&](AppState &st) { st.showToast(t, now); });
  }
#endif

  g_screen.invalidate();
}

void loop(void) {
  M5.update();

  const uint32_t now = nowMs();
  const uint32_t frame_dt = now - g_last_frame_ms;
  g_last_frame_ms = now;

  g_buttons.update(now);

  // A press that wakes the screen is swallowed, matching phone behaviour — else
  // tapping to see what's playing would skip the track.
  const bool was_asleep = (g_brightness == theme::BRIGHT_OFF);
  if (g_buttons.anyActivity()) {
    g_last_interaction_ms = now;
    if (was_asleep) {
      g_brightness = theme::BRIGHT_ACTIVE;
      theme::applyBrightness(g_brightness);
      g_screen.invalidate();
    } else {
      handleButtons(now);
    }
  }

  if (g_volume_dirty && (now - g_volume_changed_at_ms) >= VOLUME_COALESCE_MS) {
    int vol = 0;
    mutateState([&](AppState &st) { vol = st.pb.volume_pct; });
    submit({CommandType::SetVolume, vol});
    g_volume_dirty = false;
  }

#if defined(CAN_GO_LIVE)
  if (g_live && g_net) {
    // The snapshot is the net thread's last published state, re-read every
    // frame. Copying it wholesale would overwrite the locally extrapolated
    // progress on every frame, leaving the clock to advance only when a poll
    // lands — a 2s stutter instead of a 1s tick. So keep our own progress and
    // resync it only when the sequence number says the data is actually new.
    const AppState snap = g_net->snapshot();
    g_state = snap;
    g_clock.sync(snap.publish_seq, snap.pb.progress_ms);
    g_state.pb.progress_ms = g_clock.value();
  }
#endif

  if (!g_live) {
    g_fake.poll(&g_state, &g_cmds, now);
    g_clock.reset(g_state.pb.progress_ms);
  }

  // Extrapolate between publishes so the display ticks every second even
  // though the source only reports every two.
  if (g_state.pb.has_track) {
    g_clock.advance(frame_dt, g_state.pb.is_playing, g_state.pb.duration_ms);
    g_state.pb.progress_ms = g_clock.value();
  }

  if (g_state.pb.is_playing) {
    g_not_playing_since_ms = now;
  }

  updateBrightness(now);

  if (g_brightness != theme::BRIGHT_OFF) {
    const bool want_status = StatusScreen::shouldShow(g_state);
    if (want_status != g_showing_status) {
      g_showing_status = want_status;
      // Switching screens leaves the whole panel stale.
      g_status.invalidate();
      g_screen.invalidate();
    }
    if (want_status) {
      g_status.render(g_state, now);
    } else {
      g_screen.render(g_state, now);
    }
  } else if (!was_asleep) {
    M5.Display.fillScreen(TFT_BLACK);
    g_screen.invalidate();
  }

#if defined(EMULATOR)
  // EMU_FIRE=<like|unlike|playpause> with EMU_FIRE_MS fires a user action at a
  // known time, so animations can be sampled frame by frame without a keypress.
  if (const char *what = std::getenv("EMU_FIRE")) {
    static bool fired = false;
    const char *at_s = std::getenv("EMU_FIRE_MS");
    const uint32_t at = at_s ? static_cast<uint32_t>(std::atoi(at_s)) : 500;
    static const uint32_t t0 = now;
    if (!fired && (now - t0) >= at) {
      fired = true;
      // Mirror a real button press exactly: optimistic local edit AND the
      // command. Mutating only local state left the source disagreeing, so
      // once the settle window expired its next poll undid the change.
      if (std::strcmp(what, "playpause") == 0) {
        mutateState([&](AppState &st) {
          st.pb.is_playing = !st.pb.is_playing;
          st.settle_playing_until_ms = now + SETTLE_MS;
        });
        submit({CommandType::PlayPause, 0});
      } else {
        const bool want = std::strcmp(what, "like") == 0;
        bool changed = false;
        mutateState([&](AppState &st) {
          changed = (st.pb.liked != want) || !st.pb.liked_known;
          st.pb.liked = want;
          st.pb.liked_known = true;
          st.settle_liked_until_ms = now + SETTLE_MS;
        });
        if (changed) submit({CommandType::ToggleLike, 0});
      }
    }
  }

  // EMU_LINK=<connecting|offline|autherror|reauth|notrack> forces a link state
  // so the status screen can be inspected and tested without unplugging.
  if (const char *l = std::getenv("EMU_LINK")) {
    if (std::strcmp(l, "connecting") == 0) g_state.link = LinkStatus::Connecting;
    else if (std::strcmp(l, "offline") == 0) g_state.link = LinkStatus::Offline;
    else if (std::strcmp(l, "autherror") == 0) g_state.link = LinkStatus::AuthError;
    else if (std::strcmp(l, "reauth") == 0) g_state.link = LinkStatus::ReauthNeeded;
    else if (std::strcmp(l, "notrack") == 0) g_state.pb.has_track = false;
  }

  // Two exit hooks. EMU_EXIT_MS is wall-clock and is the one to use when
  // waiting on the network: loop() runs as fast as SDL allows, so a frame count
  // can elapse in well under a second and kill the process mid-request.
  bool quit = false;
  if (const char *ms = std::getenv("EMU_EXIT_MS")) {
    static const uint32_t started = now;
    if (now - started >= static_cast<uint32_t>(std::atoi(ms))) quit = true;
  }
  if (const char *nstr = std::getenv("EMU_EXIT_AFTER")) {
    static int frame = 0;
    if (++frame >= std::atoi(nstr)) quit = true;
  }
  if (quit) {
    if (const char *path = std::getenv("EMU_DUMP")) dumpFrameBmp(path);
    std::exit(0);
  }
#endif
}

#if defined(SDL_h_)
__attribute__((weak)) int user_func(bool *running) {
  setup();
  while (*running) {
    loop();
  }
  return 0;
}

int main(int, char **) { return lgfx::Panel_sdl::main(user_func, 16); }
#endif
