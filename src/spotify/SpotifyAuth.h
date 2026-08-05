#pragma once

// Turns the long-lived refresh token into short-lived access tokens.
//
// Spotify refresh tokens do not expire unless revoked, so the device mints an
// access token at boot and re-mints it 60s before expiry. Refresh is proactive
// rather than reactive on a 401, though a 401 still forces one retry because
// clock drift happens.

#include <cstdint>
#include <string>

#include "../core/AppState.h"

class SpotifyAuth {
 public:
  SpotifyAuth(const char *client_id, const char *client_secret,
              const char *refresh_token);

  // Refreshes if the current token is missing or close to expiry.
  bool ensureFresh(uint32_t now_ms);

  // Forces a refresh on the next ensureFresh(). Called after a 401.
  void invalidate() { expires_at_ms_ = 0; }

  const std::string &token() const { return access_token_; }
  LinkStatus status() const { return status_; }

 private:
  std::string client_id_;
  std::string client_secret_;
  std::string refresh_token_;
  std::string access_token_;

  uint32_t expires_at_ms_ = 0;
  uint32_t retry_after_ms_ = 0;
  int consecutive_failures_ = 0;
  LinkStatus status_ = LinkStatus::Booting;
};
