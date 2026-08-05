#if defined(EMULATOR)

#include "NetWorker.h"

#include <chrono>

#include "../core/Clock.h"
#include "NetLog.h"

NetWorker::NetWorker(const char *client_id, const char *client_secret,
                     const char *refresh_token)
    : source_(client_id, client_secret, refresh_token) {
  state_.link = LinkStatus::Connecting;
}

NetWorker::~NetWorker() { stop(); }

void NetWorker::start(const std::string &cache_dir) {
  cache_dir_ = cache_dir;
  source_.begin(cache_dir_);
  running_ = true;
  thread_ = std::thread(&NetWorker::run, this);
}

void NetWorker::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

void NetWorker::submit(const Command &c) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (c.type == CommandType::SetVolume) {
    // Only the final volume matters; replace any pending one.
    cmds_.pushCoalesced(c);
  } else {
    cmds_.push(c);
  }
}

AppState NetWorker::snapshot() {
  std::lock_guard<std::mutex> lk(mtx_);
  return state_;
}

void NetWorker::merge(const AppState &from, bool polled, uint32_t now_ms) {
  std::lock_guard<std::mutex> lk(mtx_);

  state_.link = from.link;

  // A toast raised by the net thread wins only if it outlives whatever the UI
  // is already showing, so a stale message cannot clobber a fresher one.
  if (from.toast_until_ms > state_.toast_until_ms) {
    setStr(state_.toast, sizeof(state_.toast), from.toast);
    state_.toast_until_ms = from.toast_until_ms;
  }

  if (!polled) return;

  const bool track_changed =
      std::strcmp(state_.pb.track_id, from.pb.track_id) != 0;

  // Metadata is the net thread's alone.
  setStr(state_.pb.track_id, ID_LEN, from.pb.track_id);
  setStr(state_.pb.album_id, ID_LEN, from.pb.album_id);
  setStr(state_.pb.title, TEXT_LEN, from.pb.title);
  setStr(state_.pb.artist, TEXT_LEN, from.pb.artist);
  setStr(state_.pb.art_path, PATH_LEN, from.pb.art_path);
  state_.pb.has_track = from.pb.has_track;
  state_.pb.has_device = from.pb.has_device;
  state_.pb.duration_ms = from.pb.duration_ms;
  state_.pb.progress_ms = from.pb.progress_ms;

  // Settle windows: leave alone anything the user just changed optimistically,
  // so a response already in flight cannot snap it back.
  if (now_ms >= state_.settle_playing_until_ms) {
    state_.pb.is_playing = from.pb.is_playing;
  }
  if (now_ms >= state_.settle_volume_until_ms) {
    state_.pb.volume_pct = from.pb.volume_pct;
  }
  if (now_ms >= state_.settle_liked_until_ms) {
    state_.pb.liked = from.pb.liked;
  }

  // Never downgrade known -> unknown for the same track. When the API cannot
  // report saved-state, the user's own like in this session is the only truth
  // we have, and it should not evaporate when the settle window closes.
  if (track_changed || from.pb.liked_known) {
    state_.pb.liked_known = from.pb.liked_known;
  }
}

void NetWorker::run() {
  NETLOG("worker thread started");
  int iter = 0;
  while (running_) {
    const uint32_t now = nowMs();
    if (iter < 3) NETLOG("iteration %d (t=%u)", iter, now);
    ++iter;

    AppState scratch;
    CommandQueue<> local;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      scratch = state_;
      Command c;
      while (cmds_.pop(&c)) local.push(c);
    }

    // All network I/O happens here, holding no lock.
    source_.step(&scratch, &local, now);
    if (iter <= 3) NETLOG("step returned, polled=%d", (int)source_.polledThisStep());

    merge(scratch, source_.polledThisStep(), nowMs());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

#endif  // EMULATOR
