#pragma once

// Album artwork on local storage, keyed by album ID.
//
// A cache hit means a track change repaints with no network access at all. On
// the device this directory lives on the SD card; in the emulator it is a local
// folder. No eviction: at ~25KB a cover, filling a 16GB card takes north of half
// a million distinct albums.

#include <string>

class ArtCache {
 public:
  void begin(const std::string &dir);

  // Returns a local path to decodable artwork, downloading it if absent.
  // Empty string means unavailable — the caller must render a flat block rather
  // than failing.
  std::string ensure(const std::string &album_id, const std::string &url);

 private:
  std::string dir_;
};
