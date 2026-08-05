#pragma once

#include <M5Unified.h>

#include <cstdint>

#include "../core/AppState.h"
#include "Indicators.h"
#include "Scenes.h"

// Layout and orchestration for the now-playing screen.
//
// Redrawing everything every frame is fine in the emulator and far too slow on
// hardware, where repainting artwork means decoding a JPEG again. So each
// region repaints only when its own inputs change: artwork on album change,
// text on track change, the strip once a second. The animated pieces own
// themselves — see Indicators.h and Scenes.h.
class NowPlayingScreen {
 public:
  void invalidate() { force_ = true; }
  void render(const AppState &st, uint32_t now_ms);

  // Frees sprite memory while this screen is not showing. The two screens are
  // mutually exclusive, so only one need hold buffers at a time.
  void release();

 private:
  void drawArtRegion(const AppState &st);
  void drawTextColumn(const AppState &st);
  void drawColumnFoot(const AppState &st);
  void drawProgressBar(const AppState &st);
  void drawTimeRow(const AppState &st, bool clear_first);
  void drawToastRow(const AppState &st);

  ScenePanel scene_;
  HeartIndicator heart_;
  PlayGlyph glyph_;

  bool force_ = true;
  char last_album_[ID_LEN] = {};
  char last_track_[ID_LEN] = {};
  int last_progress_sec_ = -1;
  int last_volume_ = -2;
  bool last_liked_ = false;
  bool last_liked_known_ = false;
  bool last_playing_ = false;
  bool last_toast_active_ = false;
  char last_toast_[64] = {};
  LinkStatus last_link_ = LinkStatus::Booting;
};
