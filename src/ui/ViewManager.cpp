#include "ViewManager.h"

#include <cstdlib>
#include <cstring>

#include "../art/ArtRenderer.h"
#include "../net/NetLog.h"
#include "Theme.h"

namespace {

uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (s && *s) {
    h ^= static_cast<uint8_t>(*s++);
    h *= 16777619u;
  }
  return h;
}

}  // namespace

void ViewManager::release() {
  classic_.release();
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
    modes_[2] = &cassette_;
    modes_[3] = &scoreboard_;
    modes_[4] = &cyberdeck_;
    modes_[5] = &synthwave_;
  }

  const uint32_t h = fnv1a(track_id);
  scoreboard_.setTeam(static_cast<int>((h >> 8) % 2));

#if defined(FORCE_VIEW)
  // Compile-time pin, for testing a single view on hardware where there is no
  // environment to read EMU_MODE from. -1 is classic, 0..5 the full-screen
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
    scoreboard_.setTeam(static_cast<int>(h % 2));
    return;
  }
#endif

  // -1 is classic, 0..5 the full-screen modes.
  int pick = static_cast<int>(h % MODE_COUNT) - 1;
  if (pick == current_) pick = (pick + 2 > MODE_COUNT - 2) ? -1 : pick + 1;
  current_ = pick;
  NETLOG("view: %s", currentName());
}

void ViewManager::render(const AppState &st, uint32_t now_ms) {
  const bool track_changed = std::strcmp(last_track_, st.pb.track_id) != 0;

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

    setStr(art_path_, PATH_LEN, st.pb.art_path);
    tint_ = sampleArtTint(art_path_, theme::pal.accent);
    entered_ = false;
    classic_.invalidate();
  }

  const ViewCtx ctx{tint_, art_path_};

  if (current_ < 0) {
    // Classic does its own dirty tracking, so it only needs render().
    classic_.render(st, now_ms, tint_);
  } else {
    ViewMode *m = modes_[current_];
    if (!m) return;
    if (!entered_) {
      m->enter(st, ctx);
      entered_ = true;
    }
    m->tick(st, ctx, now_ms);
  }

  setStr(last_track_, ID_LEN, st.pb.track_id);
  force_ = false;
}
