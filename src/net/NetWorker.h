#pragma once

// Runs SpotifySource off the UI thread.
//
// This is the emulator's stand-in for the design's core-0 net task. A Spotify
// call takes 200-500ms and a token refresh longer; sharing a thread with
// rendering would freeze the buttons every time one happened.
//
// Ownership: the net thread never touches shared state while doing I/O. It
// snapshots under lock, works on its own copy, then merges under lock.

#if defined(EMULATOR)

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "../core/AppState.h"
#include "../core/CommandQueue.h"
#include "../spotify/SpotifySource.h"

class NetWorker {
 public:
  NetWorker(const char *client_id, const char *client_secret,
            const char *refresh_token);
  ~NetWorker();

  void start(const std::string &cache_dir);
  void stop();

  // UI thread: enqueue a command for the net thread to execute.
  void submit(const Command &c);

  // UI thread: copy the shared state out for rendering.
  AppState snapshot();

  // UI thread: apply a locally-optimistic edit under the same lock the net
  // thread merges with, so the two cannot interleave mid-update.
  template <typename Fn>
  void mutate(Fn fn) {
    std::lock_guard<std::mutex> lk(mtx_);
    fn(state_);
  }

 private:
  void run();
  void merge(const AppState &from, bool polled, uint32_t now_ms);

  SpotifySource source_;
  std::string cache_dir_;

  std::mutex mtx_;
  AppState state_;
  CommandQueue<> cmds_;

  std::thread thread_;
  std::atomic<bool> running_{false};
};

#endif  // EMULATOR
