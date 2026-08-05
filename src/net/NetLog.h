#pragma once

// Opt-in network tracing, enabled with SPOTIFY_DEBUG=1.
//
// Logs status codes, URLs and Spotify's own error strings. Never logs the
// client secret, the refresh token, or an access token — a debug flag must not
// become the thing that leaks the credentials into a terminal scrollback.

#include <cstdio>
#include <cstdlib>

inline bool netDebug() {
  static const bool on = std::getenv("SPOTIFY_DEBUG") != nullptr;
  return on;
}

#define NETLOG(...)                        \
  do {                                     \
    if (netDebug()) {                      \
      std::fprintf(stderr, "[net] ");      \
      std::fprintf(stderr, __VA_ARGS__);   \
      std::fprintf(stderr, "\n");          \
    }                                      \
  } while (0)
