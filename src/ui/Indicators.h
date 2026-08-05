#pragma once

// The two animated glyphs in the now-playing screen.
//
// Each owns its own sprite and animation state. They render into an off-screen
// buffer and land as a single blit, because drawing them directly would mean
// clear-and-repaint every frame — the same mid-draw tearing that once made the
// timecodes blink once a second.
//
// release() exists because the two screens are mutually exclusive: whichever is
// not showing has no business holding sprite memory on a board with no PSRAM.

#include <M5Unified.h>

#include <cstdint>

class HeartIndicator {
 public:
  // Begins the pop. Call only for a real like/unlike, never for a track change
  // or for saved-state merely becoming known — animating those makes the
  // display twitch on every song.
  void trigger(bool liking, uint32_t now_ms);
  bool animating() const { return anim_; }

  // known == false draws nothing at all: a dim heart would assert "not liked",
  // which cannot be claimed when the API declines to answer.
  void render(bool known, bool liked, uint32_t now_ms);
  void release();

 private:
  M5Canvas *cv_ = nullptr;
  bool anim_ = false;
  bool liking_ = false;
  uint32_t start_ms_ = 0;
};

class PlayGlyph {
 public:
  void trigger(bool to_playing, uint32_t now_ms);
  bool animating() const { return anim_; }
  void render(bool playing, uint32_t now_ms);
  void release();

 private:
  M5Canvas *cv_ = nullptr;
  bool anim_ = false;
  bool to_playing_ = false;
  uint32_t start_ms_ = 0;
};
