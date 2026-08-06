#pragma once

// The real Spotify Web API source.
//
// Same role as FakeSource: it owns remote truth and publishes into AppState.
// Screens do not know which one is running.
//
// Runs on the net thread. Every method here blocks on network I/O, which is
// exactly why it must not share a thread with rendering.

#include <cstdint>
#include <string>
#include <vector>

#include "../art/ArtCache.h"
#include "../core/AppState.h"
#include "../core/CommandQueue.h"
#include "../net/HttpClient.h"
#include "SpotifyAuth.h"

class SpotifySource {
 public:
  SpotifySource(const char *client_id, const char *client_secret,
                const char *refresh_token);

  void begin(const std::string &cache_dir);

  // One iteration: run queued commands, then poll if due. `out` is a scratch
  // state the caller merges under lock — this never touches shared memory.
  void step(AppState *out, CommandQueue<> *cmds, uint32_t now_ms);

  // True only when this step actually got a fresh player response. The caller
  // must not merge playback fields otherwise — the scratch state would be a
  // stale snapshot and would undo the UI's progress extrapolation.
  bool polledThisStep() const { return polled_; }

 private:
  bool authHeaders(std::vector<std::string> *headers, uint32_t now_ms);
  bool call(const char *method, const std::string &url, const std::string &body,
            HttpResponse *resp, AppState *out, uint32_t now_ms);
  void runCommand(const Command &c, AppState *out, uint32_t now_ms);
  void pollPlayer(AppState *out, uint32_t now_ms);
  void refreshLiked(AppState *out, uint32_t now_ms);
  void diagnose(AppState *out, uint32_t now_ms);
  void probeLibraryWrite(AppState *out, uint32_t now_ms);

  SpotifyAuth auth_;
  ArtCache art_;

  std::string last_liked_track_;
  uint32_t next_poll_ms_ = 0;
  uint32_t rate_limited_until_ms_ = 0;
  bool polled_ = false;

  // After a skip, poll rapidly until the track actually changes rather than
  // waiting a fixed delay and hoping. Spotify needs a moment to settle, and
  // that moment varies.
  std::string confirm_track_;
  uint32_t confirm_until_ms_ = 0;
  uint32_t confirm_started_ms_ = 0;
  unsigned confirm_polls_ = 0;

  // Cleared permanently once /me/tracks/contains answers 403: the restriction
  // is per-app, so retrying every poll would just burn rate limit forever.
  bool liked_supported_ = true;
};
