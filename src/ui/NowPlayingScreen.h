#pragma once

#include <cstdint>

#include "../core/AppState.h"

// Renders the now-playing screen with dirty tracking.
//
// Redrawing everything every frame is fine in the emulator and far too slow on
// hardware, where repainting artwork means decoding a JPEG again. So each region
// repaints only when its own inputs change: artwork on album change, text on
// track change, the strip once a second.
class NowPlayingScreen {
 public:
  void invalidate() { force_ = true; }
  void render(const AppState &st, uint32_t now_ms);

 private:
  void drawArtRegion(const AppState &st);
  void drawTextColumn(const AppState &st);
  void drawColumnFoot(const AppState &st);
  void drawProgressBar(const AppState &st);
  void drawTimeRow(const AppState &st, bool clear_first);
  void drawToastRow(const AppState &st);
  void drawPlayGlyphBox(const AppState &st);

  bool force_ = true;
  char last_album_[ID_LEN] = {};
  char last_track_[ID_LEN] = {};
  int last_progress_sec_ = -1;
  int last_volume_ = -2;
  bool last_liked_ = false;
  bool last_playing_ = false;
  bool last_toast_active_ = false;
  char last_toast_[64] = {};
  LinkStatus last_link_ = LinkStatus::Booting;
};
