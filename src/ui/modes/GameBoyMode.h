#pragma once
#include "ViewMode.h"

// The whole view IS the DMG screen.
//
// The first two versions drew the handheld as an object — a portrait of a
// Game Boy with a small cover inside its LCD, later with a cartridge ritual.
// Squeezing the shell above the shared strip squished it, and the cover was
// tiny. This inverts the idea: the full panel renders as the Game Boy's LCD,
// everything in the four DMG greens — the cover big and ordered-dithered the
// way real GB games faked shades, the track info in a Pokemon-style
// double-border dialog box, and the boot logo-drop playing full screen on
// every track change.
class GameBoyMode : public ViewMode {
 public:
  const char *name() const override { return "gameboy"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  void drawMain(const AppState &st, const ViewCtx &ctx);

  uint32_t start_ms_ = 0;  // 0 = boot not started
  int phase_ = 0;          // 0 boot bar, 1 settled
  int last_bar_y_ = -1;
};
