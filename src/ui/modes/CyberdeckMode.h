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
  int last_sec_ = -1;
  bool cursor_on_ = false;
};
