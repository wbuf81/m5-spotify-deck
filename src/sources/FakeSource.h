#pragma once

// Stands in for the Spotify Web API so the whole UI can be built and judged
// before any hardware or OAuth exists.
//
// It deliberately models the real thing's awkward parts rather than being a
// convenient fiction:
//
//   * It owns a private "remote" playback state. The UI never writes it.
//   * Commands take effect after a simulated round-trip delay, so optimistic
//     updates are actually doing visible work.
//   * It only publishes into AppState on a poll interval, so the UI has to
//     extrapolate progress between polls exactly as it will on hardware.
//   * It honours the settle windows, so a poll in flight cannot stomp a field
//     the user just changed.

#include <cstdint>

#include "../core/AppState.h"
#include "../core/CommandQueue.h"

// Simulated Spotify API round trip. Real calls measured 200-500ms.
constexpr uint32_t FAKE_LATENCY_MS = 250;
// Matches the design's playing-state poll interval.
constexpr uint32_t FAKE_POLL_MS = 2000;

class FakeSource {
 public:
  void begin(AppState *st, uint32_t now_ms);
  void poll(AppState *st, CommandQueue<> *cmds, uint32_t now_ms);

  // True on the ticks where state was actually published, so the UI can resync
  // its extrapolated progress.
  bool publishedThisTick() const { return published_; }

 private:
  struct Pending {
    Command cmd;
    uint32_t apply_at_ms;
    bool used;
  };

  void loadTrack(int index, uint32_t now_ms);
  void applyToRemote(const Command &c, uint32_t now_ms);
  void publish(AppState *st, uint32_t now_ms);

  PlaybackState remote_;
  Pending pending_[6] = {};
  int index_ = 0;
  uint32_t last_advance_ms_ = 0;
  uint32_t last_publish_ms_ = 0;
  bool published_ = false;
};
