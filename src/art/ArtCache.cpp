#include "ArtCache.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>

#include "../net/HttpClient.h"

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

  if (!http::downloadToFile(url, path)) return "";
  return fileExists(path) ? path : "";
}
