#pragma once
#include "ViewMode.h"

// Stadium LED board. Team colours and a generic board rather than logos: club
// marks at this resolution read as mush, and the palette is what actually
// carries the association anyway.
class ScoreboardMode : public ViewMode {
 public:
  const char *name() const override { return "scoreboard"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

  // Alternates per track so both teams get a turn.
  void setTeam(int team) { team_ = team; }

 private:
  int team_ = 0;
  int last_sec_ = -1;
};
