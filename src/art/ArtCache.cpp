#include "ArtCache.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>

#include "../net/HttpClient.h"
#include "../net/NetLog.h"
#include "../platform/esp32/Esp32Storage.h"

namespace {

bool fileExists(const std::string &path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

// Album IDs are base62 from Spotify, but this is going into a filesystem path,
// so refuse anything that is not plainly safe rather than trusting the input.
bool safeId(const std::string &id) {
  if (id.empty() || id.size() > 40) return false;
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

void ArtCache::begin(const std::string &dir) {
  dir_ = dir;
  // Create each path component. A single mkdir of ".cache/art" fails with
  // ENOENT because ".cache" does not exist yet, and every later download would
  // then fail with no obvious cause.
  std::string partial;
  for (size_t i = 0; i <= dir_.size(); ++i) {
    if (i == dir_.size() || dir_[i] == '/') {
      if (!partial.empty()) ::mkdir(partial.c_str(), 0755);
    }
    if (i < dir_.size()) partial += dir_[i];
  }
}

std::string ArtCache::ensure(const std::string &album_id,
                             const std::string &url) {
  if (!safeId(album_id) || url.empty()) return "";

  const std::string path = dir_ + "/" + album_id + ".jpg";
  if (fileExists(path)) return path;

  // No usable storage: never even open the connection. There is nowhere to put
  // the result, and attempting anyway is a request per poll for as long as the
  // device runs.
  if (!storageAvailable()) return "";

  // Already failed for this album — do not retry until the track changes.
  if (failed_album_ == album_id) return "";

  if (!http::downloadToFile(url, path) || !fileExists(path)) {
    NETLOG("artwork unavailable for %s; not retrying until the album changes",
           album_id.c_str());
    failed_album_ = album_id;
    return "";
  }
  failed_album_.clear();
  return path;
}
