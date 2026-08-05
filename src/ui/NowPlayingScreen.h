#pragma once

#include <M5Unified.h>

#include <cstdint>

#include "../core/AppState.h"
#include "Scenes.h"

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
  void ensureSprites();
  void drawHeartRegion(const AppState &st, uint32_t now_ms);
  void drawGlyphRegion(const AppState &st, uint32_t now_ms);
  void drawArtRegion(const AppState &st);
  ScenePanel vis_;
  void drawTextColumn(const AppState &st);
  void drawColumnFoot(const AppState &st);
  void drawProgressBar(const AppState &st);
  void drawTimeRow(const AppState &st, bool clear_first);
  void drawToastRow(const AppState &st);


  bool force_ = true;
  char last_album_[ID_LEN] = {};
  char last_track_[ID_LEN] = {};
  int last_progress_sec_ = -1;
  int last_volume_ = -2;
  bool last_liked_ = false;
  bool last_liked_known_ = false;
  bool last_playing_ = false;
  // Animation state. Both animations render into their own small sprite and
  // are pushed as a single blit, so redrawing them every frame cannot produce
  // the mid-draw tearing that a direct clear-and-repaint would.
  M5Canvas *heart_cv_ = nullptr;
  M5Canvas *glyph_cv_ = nullptr;

  bool heart_anim_active_ = false;
  bool heart_anim_liking_ = false;
  uint32_t heart_anim_start_ms_ = 0;

  bool glyph_anim_active_ = false;
  bool glyph_anim_to_playing_ = false;
  uint32_t glyph_anim_start_ms_ = 0;

  bool last_toast_active_ = false;
  char last_toast_[64] = {};
  LinkStatus last_link_ = LinkStatus::Booting;
};
