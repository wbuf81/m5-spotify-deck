// Device HTTP — not yet implemented.
//
// Kept as a stub so the esp32 environment still compiles and links while the
// shared Spotify code is developed against the emulator. The real version needs
// WiFiClientSecure plus streaming: the /me/player response must be parsed from
// the stream rather than buffered, and artwork must be spooled to SD, because
// neither fits comfortably in heap on a board with no PSRAM.

#if !defined(EMULATOR)

#include "../../net/HttpClient.h"

namespace http {

bool request(const char *, const std::string &, const std::vector<std::string> &,
             const std::string &, HttpResponse *out) {
  if (out) {
    out->status = 0;
    out->body.clear();
  }
  return false;
}

bool downloadToFile(const std::string &, const std::string &) { return false; }

}  // namespace http

#endif  // !EMULATOR
