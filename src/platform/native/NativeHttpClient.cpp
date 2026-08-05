// libcurl-backed HTTP for the emulator.

#if defined(EMULATOR)

#include <curl/curl.h>

#include <cstdio>
#include <cstring>

#include "../../net/HttpClient.h"

namespace {

size_t writeToString(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *s = static_cast<std::string *>(userdata);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t writeToFile(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *f = static_cast<FILE *>(userdata);
  return std::fwrite(ptr, size, nmemb, f) * size;
}

size_t captureHeader(char *buffer, size_t size, size_t nitems, void *userdata) {
  auto *out = static_cast<HttpResponse *>(userdata);
  const size_t len = size * nitems;
  static const char kRetry[] = "retry-after:";
  if (len > sizeof(kRetry)) {
    std::string line(buffer, len);
    std::string lower;
    lower.reserve(line.size());
    for (char c : line) lower.push_back(static_cast<char>(::tolower(c)));
    if (lower.compare(0, sizeof(kRetry) - 1, kRetry) == 0) {
      out->retry_after_s = std::strtol(line.c_str() + sizeof(kRetry) - 1, nullptr, 10);
    }
  }
  return len;
}

struct CurlGlobal {
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};
CurlGlobal g_curl_global;

}  // namespace

namespace http {

bool request(const char *method, const std::string &url,
             const std::vector<std::string> &headers, const std::string &body,
             HttpResponse *out) {
  CURL *curl = curl_easy_init();
  if (!curl) return false;

  struct curl_slist *hdrs = nullptr;
  for (const auto &h : headers) hdrs = curl_slist_append(hdrs, h.c_str());

  out->body.clear();
  out->status = 0;
  out->retry_after_s = 0;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out->body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, captureHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, out);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "m5-spotify/0.1");

  // Only attach a body when there is one. Setting POSTFIELDS unconditionally
  // makes curl send Content-Length on a GET, which is malformed and which
  // Spotify's edge rejects with a bare 403.
  if (std::strcmp(method, "GET") == 0) {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, nullptr);
  } else {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  }

  const CURLcode rc = curl_easy_perform(curl);
  if (rc == CURLE_OK) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    out->status = static_cast<int>(code);
  }

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);
  return rc == CURLE_OK;
}

bool downloadToFile(const std::string &url, const std::string &path) {
  const std::string tmp = path + ".part";
  FILE *f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;

  CURL *curl = curl_easy_init();
  if (!curl) {
    std::fclose(f);
    std::remove(tmp.c_str());
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  const CURLcode rc = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  std::fclose(f);

  // Rename only on success, so a truncated download never becomes a cache entry
  // that later fails to decode.
  if (rc == CURLE_OK && code == 200) {
    return std::rename(tmp.c_str(), path.c_str()) == 0;
  }
  std::remove(tmp.c_str());
  return false;
}

}  // namespace http

#endif  // EMULATOR
