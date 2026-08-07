#pragma once

// The shared transport strip: the bottom 48px of every player view.
//
// Before this existed each view invented its own bottom edge — pixel had its
// own play glyph, gameboy its own heart, cyberdeck a phosphor gauge, synthwave
// nothing at all, and toasts rendered only on the classic screen, so "No
// active device" was invisible five-sixths of the time. One component means
// the answer to "is it playing, is it liked, how far in, how loud" reads the
// same on every screen, and the tests can assert it once for all of them.
//
// Layout, matching the classic geometry the no-blink rules were tuned on:
//   y=192  band (pal.strip)
//   y=200  progress bar, album-tinted, comet head
//   y=206  row: elapsed | heart | play/pause glyph | volume | remaining
// A toast borrows the whole row while active and the row is rebuilt after.

#include <cstdint>

#include "../core/AppState.h"
#include "Indicators.h"

class StatusStrip {
 public:
  // Call every frame with the current tint. Redraws only what changed.
  void render(const AppState &st, uint32_t now_ms, uint16_t tint);

  // The whole strip is stale (view repaint, palette change, Daisy ran over it).
  void invalidate() { force_ = true; }

  // Hand back sprite memory while a strip-less screen (status) is up.
  void release();

 private:
  void drawBar(const AppState &st, uint16_t tint);
  void drawRow(const AppState &st, bool clear_first);
  void drawToast(const AppState &st);
  void drawVolume(const AppState &st);
  void drawBattery(const AppState &st);

  HeartIndicator heart_;
  PlayGlyph glyph_;

  bool force_ = true;
  int last_sec_ = -1;
  bool last_playing_ = false;
  bool last_liked_ = false;
  bool last_liked_known_ = false;
  int last_volume_ = -2;
  int last_battery_ = -2;
  uint16_t last_tint_ = 0;
  bool last_toast_active_ = false;
  char last_toast_[64] = {};
  char last_track_[ID_LEN] = {};
};

// Strip layout, shared with the tests. The glyph anchors dead centre; volume
// sits alone on the left half and heart + battery on the right, because the
// volume group (~44px) visually weighs the same as those two together (~41px).
// The first arrangement crammed battery and volume against the right edge and
// left a hole between the elapsed time and the glyph.
//
//   0:42     vol      >      heart   batt     -3:18
constexpr int STRIP_VOL_X = 78;
constexpr int STRIP_HEART_X = 191;
constexpr int STRIP_HEART_Y = 204;
constexpr int STRIP_BATT_X = 237;
