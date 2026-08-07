#pragma once

#include "ViewMode.h"

// Phosphor terminal readout. The most legible of the modes at a glance, which
// is worth having among a set that is otherwise atmosphere-first.
class CyberdeckMode : public ViewMode {
 public:
  const char *name() const override { return "cyberdeck"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  void pushLog(const char *line);
  void drawLog();

  // Rolling three-line event log: the thing that makes the terminal a
  // terminal. Lines arrive from real state changes.
  char log_[3][28] = {};
  bool log_dirty_ = false;

  char last_track_[40] = {};
  int last_play_ = -1;
  int last_vol_ = -999;
  int last_liked_ = -2;
  uint32_t last_scan_ms_ = 0;
  bool cursor_on_ = false;
};
