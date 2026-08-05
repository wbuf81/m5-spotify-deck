#pragma once

// Single source of truth. Sources write it, screens read it.

#include <cstdint>

#include "PlaybackState.h"

enum class LinkStatus : uint8_t {
  Booting,
  Connecting,
  Online,
  Offline,
  AuthError,
  ReauthNeeded,
};

struct AppState {
  PlaybackState pb;
  LinkStatus link = LinkStatus::Booting;

  // Transient message shown in the bottom strip in place of the time row.
  char toast[64] = {};
  uint32_t toast_until_ms = 0;

  // Fields recently changed optimistically must not be overwritten by a poll
  // response already in flight. See the settle window in the design spec.
  uint32_t settle_playing_until_ms = 0;
  uint32_t settle_volume_until_ms = 0;
  uint32_t settle_liked_until_ms = 0;

  void showToast(const char *msg, uint32_t now_ms, uint32_t duration_ms = 2000) {
    setStr(toast, sizeof(toast), msg);
    toast_until_ms = now_ms + duration_ms;
  }

  bool toastActive(uint32_t now_ms) const {
    return toast[0] != '\0' && now_ms < toast_until_ms;
  }
};
