// M5Stack Spotify desk controller.
//
// Wiring only — the behaviour lives in the modules this pulls together.
//
// Emulator controls:
//   Left  / A          previous          hold: volume down
//   Space / S          play / pause      hold: save to Liked Songs
//   Right / D          next              hold: volume up

#include <M5Unified.h>

#include "core/AppState.h"
#include "core/Clock.h"
#include "core/CommandQueue.h"
#include "input/Buttons.h"
#include "platform/native/FrameDump.h"
#include "sources/FakeSource.h"
#include "ui/NowPlayingScreen.h"
#include "ui/Theme.h"

#if defined(EMULATOR)
#include <cstdio>
#include <cstdlib>
#endif

namespace {

// Settle window from the spec: a field changed optimistically is protected from
// an in-flight poll response for this long.
constexpr uint32_t SETTLE_MS = 1500;

// Volume changes during a hold are coalesced into one call this long after the
// last adjustment, instead of one call per 5% step.
constexpr uint32_t VOLUME_COALESCE_MS = 300;
constexpr int VOLUME_STEP = 5;

// Brightness policy.
constexpr uint32_t DIM_AFTER_MS = 30000;
constexpr uint32_t SLEEP_AFTER_IDLE_MS = 180000;

AppState g_state;
CommandQueue<> g_cmds;
FakeSource g_source;
NowPlayingScreen g_screen;
Buttons g_buttons;

uint32_t g_last_interaction_ms = 0;
uint32_t g_last_frame_ms = 0;
uint32_t g_not_playing_since_ms = 0;
uint8_t g_brightness = theme::BRIGHT_ACTIVE;

bool g_volume_dirty = false;
uint32_t g_volume_changed_at_ms = 0;

void nudgeVolume(int delta, uint32_t now_ms) {
  if (g_state.pb.volume_pct < 0) {
    g_state.showToast("Volume not supported", now_ms);
    return;
  }
  int v = g_state.pb.volume_pct + delta;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  g_state.pb.volume_pct = v;
  g_state.settle_volume_until_ms = now_ms + SETTLE_MS;
  g_volume_dirty = true;
  g_volume_changed_at_ms = now_ms;
}

void handleButtons(uint32_t now_ms) {
  const BtnEvent a = g_buttons.event(Btn::A);
  const BtnEvent b = g_buttons.event(Btn::B);
  const BtnEvent c = g_buttons.event(Btn::C);

  if (a == BtnEvent::Tap) {
    g_cmds.push({CommandType::Previous, 0});
  } else if (a == BtnEvent::LongStart || a == BtnEvent::LongRepeat) {
    nudgeVolume(-VOLUME_STEP, now_ms);
  }

  if (b == BtnEvent::Tap) {
    // Optimistic: flip locally now so the glyph responds before the network does.
    g_state.pb.is_playing = !g_state.pb.is_playing;
    g_state.settle_playing_until_ms = now_ms + SETTLE_MS;
    g_cmds.push({CommandType::PlayPause, 0});
  } else if (b == BtnEvent::LongStart) {
    // Like is a single action, so ignore repeats while the button stays down.
    g_state.pb.liked = !g_state.pb.liked;
    g_state.settle_liked_until_ms = now_ms + SETTLE_MS;
    g_state.showToast(
        g_state.pb.liked ? "Saved to Liked Songs" : "Removed from Liked", now_ms);
    g_cmds.push({CommandType::ToggleLike, 0});
  }

  if (c == BtnEvent::Tap) {
    g_cmds.push({CommandType::Next, 0});
  } else if (c == BtnEvent::LongStart || c == BtnEvent::LongRepeat) {
    nudgeVolume(VOLUME_STEP, now_ms);
  }
}

void updateBrightness(uint32_t now_ms) {
  uint8_t want;
  const bool idle_long = !g_state.pb.is_playing &&
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
    // The palette changed, so everything on screen is stale.
    g_screen.invalidate();
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
  g_source.begin(&g_state, now);
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

  // Coalesced volume send: one call after the user stops adjusting.
  if (g_volume_dirty && (now - g_volume_changed_at_ms) >= VOLUME_COALESCE_MS) {
    g_cmds.pushCoalesced({CommandType::SetVolume, g_state.pb.volume_pct});
    g_volume_dirty = false;
  }

  // Progress extrapolation between polls, so the bar ticks every second even
  // though the source only publishes every two.
  const bool playing_before = g_state.pb.is_playing;
  if (playing_before && g_state.pb.has_track) {
    g_state.pb.progress_ms += frame_dt;
    if (g_state.pb.progress_ms > g_state.pb.duration_ms) {
      g_state.pb.progress_ms = g_state.pb.duration_ms;
    }
  }

  g_source.poll(&g_state, &g_cmds, now);

  if (g_state.pb.is_playing) {
    g_not_playing_since_ms = now;
  }

  updateBrightness(now);

  if (g_brightness != theme::BRIGHT_OFF) {
    g_screen.render(g_state, now);
  } else if (was_asleep == false) {
    // Just went to sleep: blank the panel once.
    M5.Display.fillScreen(TFT_BLACK);
    g_screen.invalidate();
  }

#if defined(EMULATOR)
  if (const char *nstr = std::getenv("EMU_EXIT_AFTER")) {
    static int frame = 0;
    if (++frame >= std::atoi(nstr)) {
      if (const char *path = std::getenv("EMU_DUMP")) dumpFrameBmp(path);
      std::exit(0);
    }
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
