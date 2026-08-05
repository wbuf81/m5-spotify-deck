#if defined(EMULATOR)

#include "NetWorker.h"

#include <chrono>

#include "../core/Clock.h"
#include "../core/MergePolicy.h"
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
  mergePlayback(&state_, from, polled, now_ms);
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
