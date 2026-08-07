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
#include "core/Power.h"
#include "core/Clock.h"
#include "art/ArtRenderer.h"
#include "core/Diag.h"
#include "net/NetLog.h"
#include "core/CommandQueue.h"
#include "input/Buttons.h"
#include "platform/native/FrameDump.h"
#include "sources/FakeSource.h"
#include "core/ProgressClock.h"
#include "ui/StatusScreen.h"
#include "ui/ViewManager.h"

#if defined(EMULATOR)
#include "platform/native/Harness.h"
#endif
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

// The device always compiles the live stack: its credentials can arrive from
// the setup portal (NVS) with no secrets.h in the build at all. The emulator
// still needs compiled secrets to go live — it has no portal.
#if defined(HAVE_SECRETS) || !defined(EMULATOR)
#include "config/DeviceConfig.h"
#include "net/NetLog.h"
#include "net/NetWorker.h"
#include "platform/esp32/Esp32Storage.h"
#define CAN_GO_LIVE 1
#endif
#if !defined(EMULATOR)
#include "platform/esp32/Esp32Portal.h"
#endif
#if defined(EMULATOR)
#include "ui/SetupScreen.h"
#endif

namespace {

// Settle window from the spec: a field changed optimistically is protected from
// an in-flight poll response for this long.
constexpr uint32_t SETTLE_MS = 1500;

// Volume changes during a hold are coalesced into one call this long after the
// last adjustment, instead of one call per 5% step.
constexpr uint32_t VOLUME_COALESCE_MS = 300;
constexpr int VOLUME_STEP = 5;

uint32_t dimAfterMs() {
#if defined(EMULATOR)
  // Overridable so the visual suite can exercise dimming without a 30s test.
  if (const char *v = std::getenv("EMU_DIM_AFTER_MS")) {
    return static_cast<uint32_t>(std::atoi(v));
  }
#endif
  return 30000;
}
constexpr uint32_t SLEEP_AFTER_IDLE_MS = 180000;

// A status screen sleeps sooner than paused music does. There is nothing to
// look at, and a lit CONNECTING beacon sitting on a nightstand all night is
// exactly the sort of thing that gets a device unplugged and never plugged
// back in.
constexpr uint32_t SLEEP_AFTER_STATUS_MS = 45000;

// Playback we can actually vouch for.
//
// pb.is_playing alone was keeping the screen awake forever: when the link drops
// the flag keeps its last value, so if music had been playing it stayed true
// and reset the idle timer on every frame. The device could never sleep while
// disconnected — which is the one time it most obviously should.
bool confirmedPlaying(const AppState &st) {
  return st.link == LinkStatus::Online && st.pb.has_track && st.pb.is_playing;
}

#if defined(EMULATOR)
// Two exit hooks. EMU_EXIT_MS is wall-clock and is the one to use when
// waiting on the network: loop() runs as fast as SDL allows, so a frame count
// can elapse in well under a second and kill the process mid-request.
void emuExitTick(uint32_t now) {
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
}
#endif

}  // namespace

AppState g_state;
CommandQueue<> g_cmds;
ViewManager g_screen;
ProgressClock g_clock;

namespace {

FakeSource g_fake;
StatusScreen g_status;

// The PMIC is on I2C and reports in 25% steps, so there is nothing to gain from
// reading it faster than this.
constexpr uint32_t POWER_SAMPLE_MS = 2000;
uint32_t g_power_sampled_ms = 0;
bool g_power_sampled = false;
int8_t g_battery_pct = -1;
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

bool g_volume_dirty = false;
uint32_t g_volume_changed_at_ms = 0;
#if defined(EMULATOR)
bool g_portal_preview = false;
#endif

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
    st.settle_volume.arm(now_ms, SETTLE_MS);
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

  // A+C together cycles the theme. All six single-button actions are already
  // assigned, so a two-button hold is the only gesture left.
  //
  // While the combo is held every other action is suppressed, including the
  // taps that would fire on release. Without that, reaching for both buttons
  // would skip a track on the way.
  static bool combo = false;
  const bool both_down = g_buttons.isDown(Btn::A) && g_buttons.isDown(Btn::C);
  if (both_down && !combo) {
    combo = true;
    char msg[48];
    std::snprintf(msg, sizeof(msg), "theme: %s", g_screen.cycleMode());
    mutateState([&](AppState &st) { st.showToast(msg, now_ms, 1600); });
    return;
  }
  if (combo) {
    if (!g_buttons.isDown(Btn::A) && !g_buttons.isDown(Btn::C)) combo = false;
    return;
  }

  if (a == BtnEvent::Tap) {
    // Reset the clock immediately. Unlike play/pause there is nothing truthful
    // to show optimistically — the next track's title is unknown — but the
    // position is about to be zero either way, so the bar can acknowledge the
    // press at once instead of sitting still for a second.
    g_clock.reset(0);
    mutateState([&](AppState &st) { st.pb.progress_ms = 0; });
    submit({CommandType::Previous, 0});
  } else if (a == BtnEvent::LongStart || a == BtnEvent::LongRepeat) {
    nudgeVolume(-VOLUME_STEP, now_ms);
  }

  if (b == BtnEvent::Tap) {
    // Optimistic: flip locally so the glyph responds before the network does.
    mutateState([&](AppState &st) {
      st.pb.is_playing = !st.pb.is_playing;
      st.settle_playing.arm(now_ms, SETTLE_MS);
    });
    submit({CommandType::PlayPause, 0});
  } else if (b == BtnEvent::LongStart) {
    // Like is one action, so repeats while held are ignored.
    mutateState([&](AppState &st) {
      st.pb.liked = !st.pb.liked;
      // We just set it, so we know it even if the API will not tell us.
      st.pb.liked_known = true;
      st.settle_liked.arm(now_ms, SETTLE_MS);
      // No toast: the heart animation and Daisy's lap ARE the feedback. The
      // toast also blocked the lap outright — it owned the strip row, and the
      // lap refused to start over an active toast, so on the classic screen
      // the celebration never played.
    });
    submit({CommandType::ToggleLike, 0});
  }

  if (c == BtnEvent::Tap) {
    g_clock.reset(0);
    mutateState([&](AppState &st) { st.pb.progress_ms = 0; });
    submit({CommandType::Next, 0});
  } else if (c == BtnEvent::LongStart || c == BtnEvent::LongRepeat) {
    nudgeVolume(VOLUME_STEP, now_ms);
  }
}

void updateBrightness(uint32_t now_ms) {
  uint8_t want;
  uint32_t sleep_after = StatusScreen::shouldShow(g_state)
                             ? SLEEP_AFTER_STATUS_MS
                             : SLEEP_AFTER_IDLE_MS;
#if defined(EMULATOR)
  // Overridable so the visual suite can exercise sleep without a 45s test.
  if (const char *v = std::getenv("EMU_SLEEP_AFTER_MS")) {
    sleep_after = static_cast<uint32_t>(std::atoi(v));
  }
#endif
  const bool idle_long = !confirmedPlaying(g_state) &&
                         (now_ms - g_not_playing_since_ms) >= sleep_after;

  if ((now_ms - g_last_interaction_ms) < dimAfterMs()) {
    want = theme::BRIGHT_ACTIVE;
  } else if (idle_long) {
    want = theme::BRIGHT_OFF;
  } else {
    want = theme::BRIGHT_IDLE;
  }

#if defined(EMULATOR)
  if (std::getenv("EMU_DIM_DEBUG")) {
    static int n = 0;
    if ((n++ % 600) == 0) {
      std::fprintf(stderr, "[dim] now=%u since_touch=%u want=%d cur=%d\n", now_ms,
                   now_ms - g_last_interaction_ms, (int)want, (int)g_brightness);
    }
  }
#endif
  if (want != g_brightness) {
    NETLOG("backlight %d -> %d (%s)", (int)g_brightness, (int)want,
           want == theme::BRIGHT_OFF
               ? "asleep"
               : (want == theme::BRIGHT_IDLE ? "dim" : "active"));
    g_brightness = want;
    theme::applyBrightness(want);
    g_screen.invalidate();  // palette changed, everything on screen is stale
#if defined(CAN_GO_LIVE)
    if (g_net) g_net->setScreenAsleep(want == theme::BRIGHT_OFF);
#endif
  }
}

}  // namespace

#if defined(EMULATOR)
// Lets the harness drive the same command path a real button press uses.
void harnessSubmit(const Command &c) { submit(c); }
#endif

void setup(void) {
  auto cfg = M5.config();
  M5.begin(cfg);

  // Before WiFi and TLS get a chance to fragment the heap.
  initArtBuffer();

  theme::applyBrightness(theme::BRIGHT_ACTIVE);
  M5.Display.fillScreen(theme::pal.bg);

  const uint32_t now = nowMs();
  g_last_interaction_ms = now;
  g_last_frame_ms = now;
  g_not_playing_since_ms = now;

#if defined(CAN_GO_LIVE)
  // Lives for the whole run: NetWorker keeps pointers into it.
  static DeviceConfig device_cfg;
  device_cfg = DeviceConfig::load();

#if defined(EMULATOR)
  const bool force_fake = std::getenv("EMU_FAKE") != nullptr;
  const char *cache_dir = ".cache/art";
#else
  // Setup portal: entered when the device has no complete configuration, or
  // when button A is held through power-on (reconfigure at a friend's house).
  // Before the watchdog, before the net task — the portal owns the device.
  M5.update();
  if (M5.BtnA.isPressed() || !device_cfg.complete()) {
    Serial.println(M5.BtnA.isPressed() ? "portal    : requested (BtnA held)"
                                       : "portal    : no configuration");
    runSetupPortal(device_cfg);  // never returns
  }

  const bool force_fake = false;
  // Arduino's SD library mounts at /sd through the ESP-IDF VFS, so the same
  // POSIX file code serves both platforms.
  const bool sd_ok = mountStorage();
  powerBegin();
  bootBanner(sd_ok);
  watchdogBegin();
  watchdogSubscribe();
  const char *cache_dir = "/sd/art";
#endif
  if (!force_fake && device_cfg.complete()) {
    g_live = true;
    g_net = new NetWorker(device_cfg.client_id.c_str(),
                          device_cfg.client_secret.c_str(),
                          device_cfg.refresh_token.c_str());
    g_net->start(cache_dir, device_cfg.wifi_ssid.c_str(),
                 device_cfg.wifi_password.c_str());
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

#if defined(EMULATOR)
  if (harness::active()) harness::printKeymap();
  // EMU_PORTAL=1 renders the setup screen so its layout is pixel-testable;
  // the access point itself is hardware-only.
  if (std::getenv("EMU_PORTAL")) {
    setupscreen::draw("M5SPOTIFY-SETUP", "192.168.4.1");
    g_portal_preview = true;
  }
#endif

  g_screen.invalidate();
}

void loop(void) {
  M5.update();

  const uint32_t now = nowMs();

#if defined(EMULATOR)
  // Portal preview holds the setup screen exactly as setup() drew it; only
  // the exit hooks run so the visual suite can dump it.
  if (g_portal_preview) {
    emuExitTick(now);
    return;
  }
#endif
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
#if defined(CAN_GO_LIVE)
      if (g_net) g_net->setScreenAsleep(false);  // poll immediately on wake
#endif
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

  if (confirmedPlaying(g_state)) {
    g_not_playing_since_ms = now;
  }

#if defined(EMULATOR)
  // Once per frame only: update() consumes key edges, so polling it twice
  // would swallow every other keypress.
  bool brightness_forced = false;
  if (harness::active()) {
    const harness::Overrides &ov = harness::update(&g_state, now);
    if (ov.link >= 0) g_state.link = static_cast<LinkStatus>(ov.link);
    if (ov.force_notrack) g_state.pb.has_track = false;
    if (ov.brightness >= 0) {
      brightness_forced = true;
      if (ov.brightness != g_brightness) {
        g_brightness = static_cast<uint8_t>(ov.brightness);
        theme::applyBrightness(g_brightness);
        g_screen.invalidate();
        g_status.invalidate();
      }
    }
  }
  if (!brightness_forced) updateBrightness(now);
#else
  updateBrightness(now);
#endif
  if (!g_power_sampled || (now - g_power_sampled_ms) >= POWER_SAMPLE_MS) {
    g_power_sampled = true;
    g_power_sampled_ms = now;
    g_battery_pct = readPower().pct;
  }
  // Written after the snapshot so the net task's copy never clobbers it: the
  // battery is the board's business, not Spotify's.
  g_state.battery_pct = g_battery_pct;

  heapTick(now);
  watchdogFeed();

#if defined(CAN_GO_LIVE) && !defined(EMULATOR)
  // The net task is not on the hardware watchdog because it is allowed to
  // block. If it stops entirely, restart deliberately rather than sit there
  // showing stale data forever.
  if (g_live && g_net && g_net->stalled(now)) {
    NETLOG("net task stalled — restarting");
    delay(50);
    ESP.restart();
  }
#endif

#if !defined(EMULATOR) && defined(TRACE_RENDER)
  // Frame rate, because "the screen is not working" and "the screen is
  // repainting once every few seconds" look identical from across a desk.
  {
    static uint32_t win = 0;
    static uint32_t frames = 0;
    ++frames;
    if (win == 0) win = now;
    if (now - win >= 5000) {
      NETLOG("ui: %.1f fps", frames * 1000.0f / (now - win));
      frames = 0;
      win = now;
    }
  }
#endif

  if (g_brightness != theme::BRIGHT_OFF) {
    const bool want_status = StatusScreen::shouldShow(g_state);
    const bool screen_switched = want_status != g_showing_status;
    if (screen_switched) {
      g_showing_status = want_status;
      // Switching screens leaves the whole panel stale. Also hand back the
      // hidden screen's sprites: the two are mutually exclusive, so only one
      // need hold buffers, which matters on a board with no PSRAM.
      g_status.invalidate();
      g_screen.invalidate();
      if (want_status) {
        g_screen.release();
      } else {
        g_status.release();
      }
    }
    (void)screen_switched;
    if (want_status) {
      g_status.render(g_state, now);
    } else {
      // Battery lives in the StatusStrip now, rendered by ViewManager along
      // with the rest of the transport. The floating badge this replaced
      // forced every view to lay out around one corner.
      g_screen.render(g_state, now);
    }
#if defined(EMULATOR)
    if (harness::active()) harness::drawOverlay();
#endif
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
          st.settle_playing.arm(now, SETTLE_MS);
        });
        submit({CommandType::PlayPause, 0});
      } else {
        const bool want = std::strcmp(what, "like") == 0;
        bool changed = false;
        mutateState([&](AppState &st) {
          changed = (st.pb.liked != want) || !st.pb.liked_known;
          st.pb.liked = want;
          st.pb.liked_known = true;
          st.settle_liked.arm(now, SETTLE_MS);
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

  emuExitTick(now);
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

int main(int, char **) {
  // Panel_sdl claims plain keypresses for its own window shortcuts: 1-6 change
  // the zoom level, and r/l rotate the display 90 degrees by swapping the
  // window's width and height. Both default to KMOD_NONE, so they fire on an
  // unmodified press and silently collide with the harness's mode keys — which
  // is why picking a view also resized the window.
  //
  // Moving them behind Ctrl frees the digits and leaves the shortcuts usable:
  // the library compares the modifier for exact equality, so an unmodified
  // press can no longer match.
  lgfx::Panel_sdl::setShortcutKeymod(KMOD_LCTRL);

  return lgfx::Panel_sdl::main(user_func, 16);
}
#endif
