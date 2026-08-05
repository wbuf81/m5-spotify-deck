#pragma once

// Decorative spectrum bars.
//
// HONEST LIMITATION: this does not react to the audio, and cannot. The device
// never touches the audio stream, the Core Basic has no microphone, and
// Spotify's /audio-features and /audio-analysis both return 403 for this app
// tier — verified, not assumed. So there is no tempo, no energy, no spectrum
// available at any price.
//
// What it does instead is drive smooth flowing motion from layered sine waves,
// and tie everything it *can* to real state: amplitude follows the real volume,
// the bars settle flat when playback actually pauses, and the colour is sampled
// from the current album art.
//
// A fixed fake BPM was considered and rejected. A pulse locked to the wrong
// tempo reads as broken; smooth motion reads as ambient.

#include <M5Unified.h>

#include <cstdint>

class Visualizer {
 public:
  // Colour sampled from the album art. Falls back to the theme accent.
  void setTint(uint16_t tint);

  // Draws into its region. Call every frame while visible.
  void render(bool playing, int volume_pct, uint32_t now_ms);

  void invalidate() { force_ = true; }

 private:
  void ensureSprite();

  static constexpr int BARS = 15;

  M5Canvas *cv_ = nullptr;
  float level_[BARS] = {};
  uint16_t tint_ = 0;
  bool have_tint_ = false;
  bool force_ = true;
  uint32_t last_ms_ = 0;
};
