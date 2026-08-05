#pragma once

// Shown whenever there is nothing worth showing on the now-playing screen:
// connecting, offline, an auth problem, or simply nothing playing.
//
// The previous behaviour was a 4px amber dot in a corner, which told you
// something was wrong but not what, and looked like a rendering artefact.

#include <M5Unified.h>

#include <cstdint>

#include "../core/AppState.h"

class StatusScreen {
 public:
  void invalidate() { force_ = true; }
  void render(const AppState &st, uint32_t now_ms);

  // True when this screen should be showing instead of now-playing.
  static bool shouldShow(const AppState &st);

 private:
  void drawBeacon(const AppState &st, uint32_t now_ms);

  M5Canvas *beacon_ = nullptr;
  bool force_ = true;
  LinkStatus last_link_ = LinkStatus::Booting;
  bool last_has_track_ = false;
};
