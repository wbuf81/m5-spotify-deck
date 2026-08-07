// Device HTTP over TLS.
//
// Certificate validation is on. The alternative, setInsecure(), is one line
// shorter and would send the Spotify client secret and refresh token over a
// connection anyone on the network could impersonate. The ESP-IDF root CA
// bundle ships with the Arduino core, so validation costs nothing to maintain —
// no pinned certificates to rotate when Spotify's expire.
//
// The TLS client is reused across requests. A fresh handshake costs roughly a
// second and tens of KB of heap; at a 2s poll interval, reconnecting every time
// would leave the device permanently mid-handshake.

#if !defined(EMULATOR)

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

#include "../../net/HttpClient.h"
#include "../../core/Deadline.h"
#include "../../net/NetLog.h"

// Linked in by the Arduino core when the certificate bundle is enabled.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace {

WiFiClientSecure *makeClient() {
  auto *c = new WiFiClientSecure();
  c->setCACertBundle(rootca_crt_bundle_start);
  // Spotify answers well inside this; the ceiling exists so a black-holed
  // connection cannot wedge the net task indefinitely.
  c->setTimeout(15);
  return c;
}

// One session per host, deliberately.
//
// Sharing a single client between api.spotify.com and i.scdn.co meant every
// artwork download tore down the API connection, so the next poll paid a full
// TLS handshake. Measured on hardware: /me/player was costing ~900ms where
// session reuse should make it a quarter of that.
WiFiClientSecure *g_api = nullptr;

WiFiClientSecure &apiClient() {
  if (!g_api) g_api = makeClient();
  return *g_api;
}

// Throw the TLS session away so the next request builds a clean one.
//
// Needed because a poisoned session does not heal on its own. Spotify closes an
// idle keep-alive socket after about a minute; HTTPClient then writes the next
// request into the dead socket and waits out the read timeout. From that point
// every reconnect failed with start_ssl_client: -1, permanently — the device
// went offline at 69s and never came back, with plenty of free heap. Calling
// stop() releases the socket and the mbedTLS context; the client is rebuilt
// lazily on the next call.
void resetApiSession(HTTPClient *http) {
  http->end();
  if (g_api) {
    g_api->stop();
    delete g_api;
    g_api = nullptr;
  }
}

WiFiClientSecure *g_cdn = nullptr;

WiFiClientSecure &cdnClient() {
  if (!g_cdn) g_cdn = makeClient();
  return *g_cdn;
}

// Same disease, same cure as resetApiSession(). The CDN session is MORE
// exposed to it than the API one: album changes are typically minutes apart,
// far past the ~60s idle close, so nearly every download after the first would
// have found a dead socket.
void resetCdnSession(HTTPClient *http) {
  http->end();
  if (g_cdn) {
    g_cdn->stop();
    delete g_cdn;
    g_cdn = nullptr;
  }
}

// Artwork download bounds. DL_STALL_MS is reset by every byte received, so a
// slow-but-progressing download is never killed; only a silent one is.
constexpr uint32_t DL_STALL_MS = 8000;
constexpr uint32_t DL_TOTAL_MS = 30000;

}  // namespace

namespace http {

namespace {
bool requestOnce(const char *method, const std::string &url,
                 const std::vector<std::string> &headers,
                 const std::string &body, HttpResponse *out, int attempt);
}  // namespace

bool request(const char *method, const std::string &url,
             const std::vector<std::string> &headers, const std::string &body,
             HttpResponse *out) {
  // Two attempts, because attempt 0 may be spent discovering that a reused
  // connection has already been closed by the far end. Attempt 1 always starts
  // from a fresh session, so a genuine failure still reports quickly.
  if (requestOnce(method, url, headers, body, out, 0)) return true;
  return requestOnce(method, url, headers, body, out, 1);
}

namespace {
bool requestOnce(const char *method, const std::string &url,
                 const std::vector<std::string> &headers,
                 const std::string &body, HttpResponse *out, int attempt) {
  out->body.clear();
  out->status = 0;
  out->retry_after_s = 0;

  // One persistent HTTPClient, not a fresh one per request.
  //
  // Keep-alive state lives on HTTPClient, not on the WiFiClientSecure it wraps,
  // so constructing one per call threw the connection away every time and paid
  // a full TLS handshake. Measured: /me/player cost ~930ms on every poll, two
  // seconds apart, which is a handshake rather than a request.
  static HTTPClient http;
  http.setReuse(true);
  // Bounded so one bad connection cannot wedge the net task for half a minute.
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  if (!http.begin(apiClient(), url.c_str())) {
    NETLOG("http.begin failed for %s", url.c_str());
    return false;
  }

  for (const auto &h : headers) {
    const size_t colon = h.find(':');
    if (colon == std::string::npos) continue;
    std::string name = h.substr(0, colon);
    std::string value = h.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    http.addHeader(name.c_str(), value.c_str());
  }

  // Spotify's control endpoints are PUT/POST with no body, and its frontend
  // rejects those with 411 Length Required unless Content-Length is present.
  // HTTPClient only adds the header when there IS a payload, so an empty-bodied
  // request goes out without it and every button press fails.
  //
  // libcurl sets this automatically, which is exactly why the emulator never
  // showed the problem and only real hardware did.
  if (body.empty() && std::strcmp(method, "GET") != 0) {
    http.addHeader("Content-Length", "0");
  }

  // Needed so a 429 can be honoured rather than guessed at.
  const char *collect[] = {"Retry-After"};
  http.collectHeaders(collect, 1);

  const uint32_t t_send = millis();
  int code;
  if (body.empty()) {
    code = http.sendRequest(method);
  } else {
    code = http.sendRequest(method, reinterpret_cast<uint8_t *>(
                                        const_cast<char *>(body.data())),
                            body.size());
  }

  if (code < 0) {
    // Reusing a connection races the server's idle close, so a transport error
    // is expected roughly once a minute and is not a real outage. Reset the
    // session and say so, rather than reporting offline for a poll interval
    // every time.
    NETLOG("%s %s failed: %s — resetting session", method, url.c_str(),
           HTTPClient::errorToString(code).c_str());
    resetApiSession(&http);
    if (attempt == 0) return false;  // caller retries on a fresh connection
    return false;
  }

#if defined(TRACE_RENDER)
  NETLOG("  %s took %ums", method, (unsigned)(millis() - t_send));
#else
  (void)t_send;
#endif

  out->status = code;
  if (http.hasHeader("Retry-After")) {
    out->retry_after_s = http.header("Retry-After").toInt();
  }

  // Read the body only when there is one.
  //
  // This guard is the difference between working and hanging forever. A 204 (or
  // 304) carries no body and no Content-Length, so getSize() returns -1, and
  // getString() on a -1 length reads until the peer closes the connection. With
  // keep-alive on, Spotify never closes it, so the call blocks indefinitely and
  // takes the whole net task with it.
  //
  // /me/player answers 204 whenever playback is idle, so this was not an edge
  // case: with nothing playing, the first poll after boot wedged the task
  // before the link could ever reach online, and the display sat on CONNECTING
  // until something else rebooted the device. It only ever worked because
  // testing happened with music playing.
  const int size = http.getSize();
  const bool has_body = code != 204 && code != 304 && size != 0;
  if (has_body) {
    // Responses here are small — /me/player measured 3.8KB — so buffering is
    // safe. Anything large enough to matter is artwork, which streams to SD.
    //
    // No reserve(): it was an optimisation that could throw bad_alloc under
    // heap pressure, and an uncaught throw on the ESP32 is abort(). Assigned
    // directly from the Arduino String rather than round-tripping through
    // c_str(), which allocated the body twice.
    String payload = http.getString();
    out->body.assign(payload.c_str(), payload.length());
  }

  http.end();
  return true;
}
}  // namespace

bool downloadToFile(const std::string &url, const std::string &path) {
  // Separate instance: mixing the CDN into the API's client would evict its
  // keep-alive on every album change.
  // Open the destination BEFORE fetching. The first version did the GET first,
  // so with unusable storage it downloaded ~30KB over TLS every poll and threw
  // it away — a permanent request storm against Spotify's CDN.
  const std::string tmp = path + ".part";
  FILE *f = std::fopen(tmp.c_str(), "wb");
  if (!f) {
    NETLOG("cannot open %s for write — skipping download", tmp.c_str());
    return false;
  }

  NETLOG("artwork GET %s", url.c_str());

  static HTTPClient http;
  http.setReuse(true);  // its own session, so keeping it alive is free
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(cdnClient(), url.c_str())) {
    std::fclose(f);
    std::remove(tmp.c_str());
    return false;
  }

  int code = http.GET();
  if (code < 0) {
    // Reused connection lost the race with the server's idle close. Rebuild
    // the session and try once more before reporting failure — otherwise this
    // album gets marked failed and never shows its cover.
    NETLOG("artwork GET failed (%s) — retrying on a fresh session",
           HTTPClient::errorToString(code).c_str());
    resetCdnSession(&http);
    if (!http.begin(cdnClient(), url.c_str())) {
      std::fclose(f);
      std::remove(tmp.c_str());
      return false;
    }
    code = http.GET();
  }
  if (code != HTTP_CODE_OK) {
    NETLOG("artwork GET -> %d", code);
    if (code < 0) resetCdnSession(&http);
    std::fclose(f);
    std::remove(tmp.c_str());
    http.end();
    return false;
  }

  // Streamed in small chunks: a 640px cover is tens of KB and must never sit
  // in heap in one piece on a board with no PSRAM.
  //
  // Both bounds below are load-bearing, and their absence wedged the net task
  // solid. http.setTimeout() does NOT apply here: it governs HTTPClient's own
  // reads, and this loop pumps the stream itself. Without a deadline, a CDN
  // that accepts the connection and then goes quiet spins this loop forever at
  // delay(1) — the task stays alive, so nothing looks crashed, it simply never
  // completes another iteration and playback freezes.
  //
  // The connected() test alone is not enough either. On a chunked response
  // getSize() is -1, and keep-alive holds the socket open after the final
  // chunk, so connected() stays true with nothing left to read.
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  int remaining = http.getSize();
  bool ok = true;

  Deadline no_progress, overall;
  no_progress.arm(millis(), DL_STALL_MS);
  overall.arm(millis(), DL_TOTAL_MS);

  while (remaining != 0) {
    const uint32_t now = millis();
    if (overall.elapsed(now)) {
      NETLOG("artwork download exceeded %ums — aborting", (unsigned)DL_TOTAL_MS);
      ok = false;
      break;
    }
    const size_t avail = stream->available();
    if (avail == 0) {
      // Nothing buffered and the peer is done sending: complete for a chunked
      // response, truncated for a sized one.
      if (!http.connected()) break;
      if (no_progress.elapsed(now)) {
        NETLOG("artwork download stalled %ums with %d left — aborting",
               (unsigned)DL_STALL_MS, remaining);
        ok = false;
        break;
      }
      delay(1);
      continue;
    }
    const int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n <= 0) break;
    if (std::fwrite(buf, 1, n, f) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    if (remaining > 0) remaining -= n;
    no_progress.arm(millis(), DL_STALL_MS);  // progress resets the stall clock
  }

  std::fclose(f);
  http.end();

  // Promote only on success, so a truncated download never becomes a cache
  // entry that fails to decode later.
  if (ok && remaining <= 0) {
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
  }
  std::remove(tmp.c_str());
  return false;
}

}  // namespace http

#endif  // !EMULATOR
