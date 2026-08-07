#pragma once

// Chooses and drives the full-screen presentation.
//
// Mode selection mirrors the scene panel: hashed from the track ID, skipping
// whatever played last. Even spread across a library, never the same twice
// running, stable for a given song, and deterministic so the tests can assert
// on it — none of which true randomness gives you.

#include <cstdint>

#include "../core/AppState.h"
#include "NowPlayingScreen.h"
#include "StatusStrip.h"
#include "modes/CyberdeckMode.h"
#include "modes/DaisyMode.h"
#include "modes/NesMode.h"
#include "modes/SnesMode.h"
#include "modes/GameBoyMode.h"
#include "modes/PixelAlbumMode.h"
#include "modes/SynthwaveMode.h"

class ViewManager {
 public:

  void invalidate() { force_ = true; }

  // Runtime pinning, for the interactive harness. -2 means "rotate per track",
  // -1 is classic, 0..5 the full-screen modes.
  void pin(int mode) {
    pinned_ = mode;
    force_ = true;
    entered_ = false;
  }
  // Advances the manual selection: classic, then each full-screen mode, then
  // back to rotating. Returns the new name for the on-screen confirmation.
  const char *cycleMode();

  int pinned() const { return pinned_; }
  int current() const { return current_; }
  void render(const AppState &st, uint32_t now_ms);
  void release();
  const char *currentName() const;

  // Classic is index 0 so it stays the default on first boot.
  static constexpr int MODE_COUNT = 8;  // classic + the 7 full-screen modes

 private:
  void selectFor(const char *track_id);

  NowPlayingScreen classic_;
  StatusStrip strip_;
  PixelAlbumMode pixel_;
  GameBoyMode gameboy_;
  CyberdeckMode cyberdeck_;
  DaisyMode daisy_;
  SnesMode snes_;
  NesMode nes_;
  SynthwaveMode synthwave_;

  ViewMode *modes_[MODE_COUNT - 1] = {};
  int current_ = -1;  // -1 == classic
  int pinned_ = -2;   // -2 == rotate
  bool force_ = true;
  bool entered_ = false;

  // Daisy's like celebration on mode views. The classic screen runs its own
  // strip-aware lap; DaisyMode wags instead. 0 = idle.
  uint32_t lap_start_ = 0;
  int lap_prev_x_ = 0;
  int lap_prev_frame_ = -1;
  bool lap_last_liked_ = false;
  bool lap_last_known_ = false;
  uint16_t tint_ = 0;
  char last_track_[ID_LEN] = {};
  char art_path_[PATH_LEN] = {};
};
