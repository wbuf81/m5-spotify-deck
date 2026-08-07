#pragma once
#include "ViewMode.h"

// The handheld on the left; the song on the right as a game cartridge.
//
// Each track IS a cart: the album art is its label sticker. On a track change
// the cart drops in from the top, the LCD runs the DMG boot ritual — pale
// screen, dark logo-bar sliding down, blink — and only then does the cover
// appear on the little screen. The playlist reads as a shoebox of carts.
class GameBoyMode : public ViewMode {
 public:
  const char *name() const override { return "gameboy"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  uint32_t start_ms_ = 0;  // 0 = animation not started yet
  int phase_ = 0;          // 0 slide-in, 1 boot, 2 settled
  int last_cart_y_ = 0;
  int last_bar_y_ = 0;
};
