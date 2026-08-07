#pragma once
#include "ViewMode.h"

#include "../daisy/DaisyAssets.h"

// Daisy's room: the album art hangs like a poster and Daisy reacts to the
// music — trotting, sniffing, digging or zooming while it plays, a wag burst
// when a song gets liked, and asleep when it pauses.
class DaisyMode : public ViewMode {
 public:
  const char *name() const override { return "daisy"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  daisy::DaisyAnim pickPlayingAnim(const AppState &st) const;

  daisy::DaisyAnim anim_ = daisy::Daisy_Trot;
  int frame_ = -1;
  uint32_t wag_until_ms_ = 0;
  bool last_liked_ = false;
  bool last_liked_known_ = false;
  uint32_t seed_ = 0;
};
