#include "NetWorker.h"

#include "../core/Clock.h"
#include "../core/MergePolicy.h"
#include "NetLog.h"

#if defined(EMULATOR)
#include <chrono>
#endif

namespace {

// How often the task wakes. The source decides when it actually polls; this is
// only the granularity at which queued commands get picked up, so it wants to
// be well under the settle window to keep presses feeling immediate.
constexpr uint32_t TICK_MS = 100;

#if !defined(EMULATOR)
// mbedTLS handshakes are stack-hungry. The pthread default is nowhere near
// enough and the failure mode is a stack-overflow panic partway through TLS.
constexpr uint32_t NET_TASK_STACK = 16384;
// Core 0: the Arduino loop, and therefore all rendering, runs on core 1.
constexpr BaseType_t NET_TASK_CORE = 0;
constexpr UBaseType_t NET_TASK_PRIO = 1;
#endif

void napMs(uint32_t ms) {
#if defined(EMULATOR)
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#else
  vTaskDelay(pdMS_TO_TICKS(ms));
#endif
}

}  // namespace

NetWorker::NetWorker(const char *client_id, const char *client_secret,
                     const char *refresh_token)
    : source_(client_id, client_secret, refresh_token) {
  state_.link = LinkStatus::Connecting;
}

NetWorker::~NetWorker() { stop(); }

void NetWorker::start(const std::string &cache_dir, const char *wifi_ssid,
                      const char *wifi_password) {
  cache_dir_ = cache_dir;
  ssid_ = wifi_ssid;
  password_ = wifi_password;
  source_.begin(cache_dir_);
  running_ = true;

#if defined(EMULATOR)
  thread_ = std::thread(&NetWorker::run, this);
#else
  xTaskCreatePinnedToCore(&NetWorker::taskEntry, "net", NET_TASK_STACK, this,
                          NET_TASK_PRIO, &task_, NET_TASK_CORE);
#endif
}

void NetWorker::stop() {
  running_ = false;
#if defined(EMULATOR)
  if (thread_.joinable()) thread_.join();
#endif
}

#if !defined(EMULATOR)
void NetWorker::taskEntry(void *self) {
  static_cast<NetWorker *>(self)->run();
  vTaskDelete(nullptr);
}
#endif

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

void NetWorker::run() {
  NETLOG("net task started");
  wifi_.begin(ssid_, password_);

  while (running_) {
    const uint32_t now = nowMs();

    if (!wifi_.ensureConnected(now)) {
      // No link yet. Publish the connection state so the status screen can be
      // specific about why, and attempt no HTTP at all.
      {
        std::lock_guard<std::mutex> lk(mtx_);
        state_.link = wifi_.status();
      }
      napMs(TICK_MS);
      continue;
    }

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

    {
      std::lock_guard<std::mutex> lk(mtx_);
      mergePlayback(&state_, scratch, source_.polledThisStep(), nowMs());
    }

    napMs(TICK_MS);
  }
}
