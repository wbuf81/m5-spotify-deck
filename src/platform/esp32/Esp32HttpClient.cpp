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
WiFiClientSecure &apiClient() {
  static WiFiClientSecure *c = makeClient();
  return *c;
}

WiFiClientSecure &cdnClient() {
  static WiFiClientSecure *c = makeClient();
  return *c;
}

}  // namespace

namespace http {

bool request(const char *method, const std::string &url,
             const std::vector<std::string> &headers, const std::string &body,
             HttpResponse *out) {
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
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
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
    NETLOG("%s %s failed: %s", method, url.c_str(),
           HTTPClient::errorToString(code).c_str());
    http.end();
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

  // Responses here are small — /me/player measured 3.8KB — so buffering is
  // safe. Anything large enough to matter is artwork, which streams to SD.
  //
  // No reserve(): it was an optimisation that could throw bad_alloc under heap
  // pressure, and an uncaught throw on the ESP32 is abort(). Assigned directly
  // from the Arduino String rather than round-tripping through c_str(), which
  // allocated the body twice.
  {
    String payload = http.getString();
    out->body.assign(payload.c_str(), payload.length());
  }

  http.end();
  return true;
}

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

  static HTTPClient http;
  http.setReuse(true);  // its own session, so keeping it alive is free
  http.setConnectTimeout(10000);
  http.setTimeout(20000);
  if (!http.begin(cdnClient(), url.c_str())) {
    std::fclose(f);
    std::remove(tmp.c_str());
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    NETLOG("artwork GET -> %d", code);
    std::fclose(f);
    std::remove(tmp.c_str());
    http.end();
    return false;
  }

  // Streamed in small chunks: a 640px cover is tens of KB and must never sit
  // in heap in one piece on a board with no PSRAM.
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  int remaining = http.getSize();
  bool ok = true;

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    const size_t avail = stream->available();
    if (avail == 0) {
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
