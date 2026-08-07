#pragma once
#include <M5Unified.h>

#include "ViewMode.h"

// The whole screen is an NES game HUD.
//
//   TRACK      WORLD       TIME
//    4-2      RADIOHEAD     308
//
// The album art is "the level", quantized to the actual 54-colour NES hardware
// palette — which is not a style choice you can fake with bit-masking: the
// 2C02 PPU generated NTSC composite directly and its palette is lopsided,
// heavy in muddy blues and missing entire hue bands. Art pushed through it
// looks unmistakably 1985 in a way SNES-style channel truncation never does.
//
// Liked = a 1UP heart, volume = a coin counter, and pausing drops the classic
// centred "- PAUSED -" over the level.
//
// The margins either side of the level run attract-mode Tetris: tetrominoes
// falling in their own lanes, frozen while paused. They never enter the HUD,
// the level, or the title band.
class NesMode : public ViewMode {
 public:
  const char *name() const override { return "nes"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  void drawLevel(const ViewCtx &ctx);

  int last_time_ = -1;
  int last_play_ = -1;
  int last_liked_ = -2;
  int last_coins_ = -1;

  // Attract-mode Tetris. Position is pure f(clock), so pausing the clock
  // freezes every piece; only the previous integer y is stored, for erasing.
  float clock_ = 0.0f;
  uint32_t last_ms_ = 0;
  uint32_t seed_ = 0;
  static constexpr int PIECES = 6;
  int piece_last_y_[PIECES] = {};
};
