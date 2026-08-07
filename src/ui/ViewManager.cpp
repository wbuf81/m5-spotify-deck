#include "ViewManager.h"
#include "../core/Hash.h"

#include <cstdlib>
#include <cstring>

#include "../art/ArtRenderer.h"
#include "../core/Clock.h"
#include "../net/NetLog.h"
#include "Theme.h"
#include "daisy/DaisySprite.h"

namespace {

}  // namespace

void ViewManager::release() {
  classic_.release();
  strip_.release();
  for (auto *m : modes_) {
    if (m) m->release();
  }
}

const char *ViewManager::currentName() const {
  if (current_ < 0) return "classic";
  return modes_[current_] ? modes_[current_]->name() : "";
}

void ViewManager::selectFor(const char *track_id) {
  if (!modes_[0]) {
    modes_[0] = &pixel_;
    modes_[1] = &gameboy_;
    modes_[2] = &cyberdeck_;
    modes_[3] = &synthwave_;
    modes_[4] = &daisy_;
    modes_[5] = &snes_;
    modes_[6] = &nes_;
  }

  const uint32_t h = fnv1a(track_id);

#if defined(FORCE_VIEW)
  // Compile-time pin, for testing a single view on hardware where there is no
  // environment to read EMU_MODE from. -1 is classic, 0..3 the full-screen
  // modes. Build with: PLATFORMIO_BUILD_FLAGS="-DFORCE_VIEW=-1"
  current_ = FORCE_VIEW;
  NETLOG("view: %s (compile-time pin)", currentName());
  return;
#endif

  if (pinned_ != -2) {
    current_ = pinned_;
    return;
  }

#if defined(EMULATOR)
  // EMU_MODE pins one view, for inspection and for the visual tests.
  if (const char *forced = std::getenv("EMU_MODE")) {
    const int n = std::atoi(forced);
    current_ = (n <= 0) ? -1 : ((n - 1) % (MODE_COUNT - 1));
    return;
  }
#endif

  // -1 is classic, 0..3 the full-screen modes.
  int pick = static_cast<int>(h % MODE_COUNT) - 1;
  if (pick == current_) pick = (pick + 2 > MODE_COUNT - 2) ? -1 : pick + 1;
  current_ = pick;
  NETLOG("view: %s", currentName());
}

const char *ViewManager::cycleMode() {
  // -2 rotate -> -1 classic -> 0..3 -> back to -2.
  if (pinned_ == -2) {
    pinned_ = -1;
  } else if (pinned_ >= MODE_COUNT - 2) {
    pinned_ = -2;
  } else {
    pinned_ += 1;
  }

  if (pinned_ != -2) current_ = pinned_;
  force_ = true;
  entered_ = false;
  classic_.invalidate();
  strip_.invalidate();
  return pinned_ == -2 ? "auto" : currentName();
}

void ViewManager::render(const AppState &st, uint32_t now_ms) {
  const bool track_changed = std::strcmp(last_track_, st.pb.track_id) != 0;

  // Artwork arrives on a LATER poll than the track it belongs to. The cover is
  // downloaded in the gap between polls so the new title can reach the screen
  // without waiting on a transfer, which means pb.art_path is still empty when
  // the track first changes and fills in a second or so afterwards.
  //
  // Watching only track_changed missed that second update entirely, so the
  // view kept whatever it drew when the cover was unavailable and the art did
  // not appear until some unrelated repaint happened to come along — the 30s
  // dim, or the next track. It looked like artwork took forever to load, and
  // it was worst in the pixel view, where the artwork IS the whole screen.
  const bool art_changed = std::strcmp(art_path_, st.pb.art_path) != 0;

  if (track_changed || force_ || art_changed) {
    if (track_changed || force_) {
      if (track_changed || pinned_ != -2) {
        // Leaving a mode: hand back its buffers before the next one allocates,
        // so the two are never resident at once.
        if (current_ >= 0 && modes_[current_]) modes_[current_]->release();
        classic_.release();
        selectFor(st.pb.track_id);
      } else if (current_ < 0 && !entered_) {
        selectFor(st.pb.track_id);
      }
    }

    setStr(art_path_, PATH_LEN, st.pb.art_path);
    tint_ = sampleArtTint(art_path_, theme::pal.accent);
    entered_ = false;
    classic_.invalidate();
    strip_.invalidate();
  }

  const ViewCtx ctx{tint_, art_path_};

  if (current_ < 0) {
    // Classic does its own dirty tracking, so it only needs render().
    classic_.render(st, now_ms, tint_);
  } else {
    ViewMode *m = modes_[current_];
    if (!m) return;
    if (!entered_) {
      // Timed because enter() is the one place a view can visibly stall: it
      // repaints the whole screen from a freshly decoded cover, and the cost
      // differs by an order of magnitude between modes.
      const uint32_t t0 = nowMs();
      m->enter(st, ctx);
        NETLOG("view enter %s: %ums", currentName(), (unsigned)(nowMs() - t0));
      entered_ = true;
      strip_.invalidate();  // enter() painted the whole panel, strip included
      lap_start_ = 0;       // a fresh screen never inherits a lap mid-flight
    }
    m->tick(st, ctx, now_ms);
  }

  // The shared transport strip, identical on every view. Drawn after the view
  // so nothing a mode paints can sit on top of it, and BEFORE the lap so its
  // once-a-second row updates never paint over a running dog.
  strip_.render(st, now_ms, tint_);

  // Daisy's victory lap, on whatever view is up. The first version lived only
  // in the classic screen, and with six views in rotation that meant the
  // celebration usually played to an empty room. DaisyMode is exempt — she is
  // already on screen there and wags instead.
  const bool on_daisy_mode = current_ >= 0 && modes_[current_] == &daisy_;
  const bool like_toggled = !track_changed && !force_ && lap_last_known_ &&
                            st.pb.liked_known && st.pb.liked != lap_last_liked_;
  if (like_toggled && st.pb.liked && !on_daisy_mode && lap_start_ == 0) {
    lap_start_ = now_ms;
    lap_prev_x_ = -daisy::SPRITE_COLS;
    lap_prev_frame_ = -1;
  }
  if (lap_start_ != 0) {
    constexpr uint32_t LAP_MS = 1700;
    constexpr int LAP_Y = 240 - daisy::SPRITE_ROWS - 2;
    const uint32_t t = now_ms - lap_start_;
    if (t >= LAP_MS) {
      // She only ever crosses the strip, and the strip knows how to rebuild
      // itself — no need to replay the whole view.
      lap_start_ = 0;
      strip_.invalidate();
    } else {
      const int x = -daisy::SPRITE_COLS +
                    static_cast<int>((static_cast<int64_t>(320 + daisy::SPRITE_COLS) * t) /
                                     LAP_MS);
      const int f = daisy::frameAt(daisy::Daisy_Zoomies, t);
      if (x != lap_prev_x_ || f != lap_prev_frame_) {
        if (x > lap_prev_x_) {
          M5.Display.fillRect(lap_prev_x_, LAP_Y, x - lap_prev_x_,
                              daisy::SPRITE_ROWS, theme::pal.strip);
        }
        daisy::draw(daisy::Daisy_Zoomies, f, x, LAP_Y, 1, theme::pal.strip);
        lap_prev_x_ = x;
        lap_prev_frame_ = f;
      }
    }
  }
  lap_last_liked_ = st.pb.liked;
  lap_last_known_ = st.pb.liked_known;

  setStr(last_track_, ID_LEN, st.pb.track_id);
  force_ = false;
}
