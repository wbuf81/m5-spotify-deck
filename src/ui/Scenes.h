#pragma once

// Ambient scenes for the panel under the song info.
//
// These replaced a spectrum-bar visualiser. The bars were the problem: that
// shape promises beat-sync regardless of how it moves, and beat-sync is not
// available — the device never sees the audio, the Core Basic has no
// microphone, and Spotify's /audio-features and /audio-analysis both answer 403
// for this app tier. Anything shaped like an equaliser will therefore always
// look broken. Atmosphere does not make that promise, so it can be honest.
//
// Everything here is driven only by state we genuinely have: album colour,
// track progress, real volume, and whether playback is actually running.

#include <M5Unified.h>

#include <cstdint>

struct SceneCtx {
  bool playing;
  int volume_pct;    // -1 when the device does not report one
  float progress01;  // 0..1 through the current track
  float clock;       // seconds, advancing only while playing
  uint16_t tint;     // sampled from the album art
};

class Scene {
 public:
  virtual ~Scene() = default;
  virtual void reset(uint32_t seed) = 0;
  virtual void render(M5Canvas *cv, const SceneCtx &ctx) = 0;
  virtual const char *name() const = 0;
};

// Owns the scenes and rotates between them.
class ScenePanel {
 public:
  void setTint(uint16_t tint);

  // Chooses a scene for this track. Deterministic on the id, so a song always
  // gets the same scene and the tests can assert on it, while still spreading
  // evenly across a library. Never repeats the previous scene back to back.
  void onTrackChange(const char *track_id);

  void render(bool playing, int volume_pct, uint32_t progress_ms,
              uint32_t duration_ms, uint32_t now_ms);

  void invalidate() { force_ = true; }
  const char *currentName() const;

  static constexpr int SCENE_COUNT = 4;

 private:
  void ensure();

  M5Canvas *cv_ = nullptr;
  Scene *scenes_[SCENE_COUNT] = {};
  int current_ = 0;
  bool have_tint_ = false;
  uint16_t tint_ = 0;
  bool force_ = true;

  // Only advances while playing, so every scene freezes on pause without each
  // having to implement that itself.
  float clock_ = 0.0f;
  uint32_t last_ms_ = 0;
};
