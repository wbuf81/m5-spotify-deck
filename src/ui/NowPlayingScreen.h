#pragma once

#include <M5Unified.h>

#include <cstdint>

#include "../core/AppState.h"
#include "Scenes.h"

// Layout and orchestration for the now-playing screen's upper region: artwork,
// the text column, and the ambient scene panel.
//
// Redrawing everything every frame is fine in the emulator and far too slow on
// hardware, where repainting artwork means decoding a JPEG again. So each
// region repaints only when its own inputs change.
//
// The bottom 48px — bar, timecodes, play glyph, heart, volume, toasts — is NOT
// here. That is StatusStrip, shared by every view and owned by ViewManager;
// this screen ends at STRIP_Y.
class NowPlayingScreen {
 public:
  void invalidate() { force_ = true; }
  // `tint` is sampled by ViewManager, outside any display transaction. This
  // screen must not sample it itself: the LCD and SD share one SPI bus on this
  // board, so reading the card inside startWrite() deadlocks the display.
  void render(const AppState &st, uint32_t now_ms, uint16_t tint);

  // Frees sprite memory while this screen is not showing. The two screens are
  // mutually exclusive, so only one need hold buffers at a time.
  void release();

 private:
  void drawArtRegion(const AppState &st, uint16_t tint);
  void drawTextColumn(const AppState &st);

  ScenePanel scene_;

  bool force_ = true;
  char last_album_[ID_LEN] = {};
  char last_track_[ID_LEN] = {};
  bool last_art_loading_ = false;
  LinkStatus last_link_ = LinkStatus::Booting;
};
