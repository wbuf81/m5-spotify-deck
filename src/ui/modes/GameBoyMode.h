#pragma once
#include "ViewMode.h"

// Four shades of DMG green, artwork ordered-dithered down to them.
class GameBoyMode : public ViewMode {
 public:
  const char *name() const override { return "gameboy"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  int last_sec_ = -1;
};
