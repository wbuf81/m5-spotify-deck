#include "SpotifySource.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>

#include "../net/HttpClient.h"
#include "../net/NetLog.h"
#include "../ui/Theme.h"

namespace {

constexpr const char *API = "https://api.spotify.com/v1";

// Poll cadence from the design: 2s playing, 5s paused, 20s once asleep. The
// asleep case is driven by the UI thread lowering the rate, so this covers the
// first two.
constexpr uint32_t POLL_PLAYING_MS = 2000;
constexpr uint32_t POLL_PAUSED_MS = 5000;

// Builds the filter that keeps the /me/player response from exhausting heap.
// The raw payload carries available_markets arrays with hundreds of country
// codes on both the track and the album; parsing it unfiltered is the single
// most likely way to hard-reset the device.
void buildPlayerFilter(JsonDocument *filter) {
  (*filter)["is_playing"] = true;
  (*filter)["progress_ms"] = true;
  (*filter)["device"]["name"] = true;
  (*filter)["device"]["volume_percent"] = true;
  (*filter)["item"]["id"] = true;
  (*filter)["item"]["name"] = true;
  (*filter)["item"]["duration_ms"] = true;
  (*filter)["item"]["artists"][0]["name"] = true;
  (*filter)["item"]["album"]["id"] = true;
  (*filter)["item"]["album"]["images"][0]["url"] = true;
  (*filter)["item"]["album"]["images"][0]["width"] = true;
}

// Spotify serves 640 / 300 / 64. Take the smallest that still covers the
// artwork region, so we neither upscale nor download a 640 we will discard.
const char *pickImage(JsonArrayConst images, int want) {
  const char *best = nullptr;
  int best_w = 1 << 30;
  const char *largest = nullptr;
  int largest_w = -1;

  for (JsonObjectConst img : images) {
    const char *url = img["url"];
    if (!url) continue;
    const int w = img["width"] | 0;
    if (w > largest_w) {
      largest_w = w;
      largest = url;
    }
    if (w >= want && w < best_w) {
      best_w = w;
      best = url;
    }
  }
  return best ? best : largest;
}

}  // namespace

SpotifySource::SpotifySource(const char *client_id, const char *client_secret,
                             const char *refresh_token)
    : auth_(client_id, client_secret, refresh_token) {}

void SpotifySource::begin(const std::string &cache_dir) {
  art_.begin(cache_dir);
}

bool SpotifySource::authHeaders(std::vector<std::string> *headers,
                                uint32_t now_ms) {
  if (!auth_.ensureFresh(now_ms)) return false;
  headers->clear();
  headers->push_back("Authorization: Bearer " + auth_.token());
  return true;
}

bool SpotifySource::call(const char *method, const std::string &url,
                         const std::string &body, HttpResponse *resp,
                         AppState *out, uint32_t now_ms) {
  std::vector<std::string> headers;
  if (!authHeaders(&headers, now_ms)) {
    NETLOG("%s %s: no valid token (link=%d)", method, url.c_str(),
           static_cast<int>(auth_.status()));
    out->link = auth_.status();
    return false;
  }

  if (!http::request(method, url, headers, body, resp)) {
    NETLOG("%s %s: transport failure", method, url.c_str());
    out->link = LinkStatus::Offline;
    return false;
  }
  NETLOG("%s %s -> %d", method, url.c_str(), resp->status);
  if (resp->status >= 400) {
    // Spotify's error bodies name the missing scope or restriction outright.
    NETLOG("  body: %.300s", resp->body.c_str());
  }

  if (resp->status == 401) {
    // Token rejected despite our expiry maths. Refresh once and retry.
    auth_.invalidate();
    if (!authHeaders(&headers, now_ms)) {
      out->link = auth_.status();
      return false;
    }
    if (!http::request(method, url, headers, body, resp)) {
      out->link = LinkStatus::Offline;
      return false;
    }
  }

  if (resp->status == 429) {
    const long wait = resp->retry_after_s > 0 ? resp->retry_after_s : 5;
    rate_limited_until_ms_ = now_ms + static_cast<uint32_t>(wait) * 1000;
    out->showToast("Rate limited", now_ms, 3000);
    return false;
  }

  out->link = LinkStatus::Online;
  return true;
}

void SpotifySource::runCommand(const Command &c, AppState *out,
                               uint32_t now_ms) {
  HttpResponse resp;
  std::string url;
  const char *method = "PUT";
  std::string body;

  switch (c.type) {
    case CommandType::PlayPause:
      // The optimistic local flip already happened, so send whichever verb
      // matches the state the user asked for.
      url = std::string(API) + (out->pb.is_playing ? "/me/player/play"
                                                   : "/me/player/pause");
      method = "PUT";
      break;
    case CommandType::Next:
      url = std::string(API) + "/me/player/next";
      method = "POST";
      break;
    case CommandType::Previous:
      url = std::string(API) + "/me/player/previous";
      method = "POST";
      break;
    case CommandType::SetVolume: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s/me/player/volume?volume_percent=%d",
                    API, c.arg);
      url = buf;
      method = "PUT";
      break;
    }
    case CommandType::ToggleLike: {
      if (out->pb.track_id[0] == '\0') return;
      url = std::string(API) + "/me/tracks?ids=" + out->pb.track_id;
      method = out->pb.liked ? "PUT" : "DELETE";
      break;
    }
    case CommandType::None:
      return;
  }

  if (!call(method, url, body, &resp, out, now_ms)) return;

  if (resp.status == 404) {
    // By far the most common real failure: nothing has an active session.
    out->showToast("No active device", now_ms);
    out->pb.has_device = false;
  } else if (resp.status == 403) {
    out->showToast(c.type == CommandType::SetVolume ? "Volume not supported"
                                                    : "Not allowed",
                   now_ms);
  } else if (resp.status >= 400) {
    out->showToast("Command failed", now_ms);
  } else {
    out->pb.has_device = true;
    // Poll promptly so the confirmed state lands soon after the settle window.
    next_poll_ms_ = now_ms + 700;
  }
}

void SpotifySource::pollPlayer(AppState *out, uint32_t now_ms) {
  HttpResponse resp;
  if (!call("GET", std::string(API) + "/me/player", "", &resp, out, now_ms)) {
    return;
  }

  if (resp.status == 204 || resp.body.empty()) {
    out->pb.has_track = false;
    out->pb.is_playing = false;
    polled_ = true;
    next_poll_ms_ = now_ms + POLL_PAUSED_MS;
    return;
  }
  if (resp.status != 200) {
    next_poll_ms_ = now_ms + POLL_PAUSED_MS;
    return;
  }

  JsonDocument filter;
  buildPlayerFilter(&filter);

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, resp.body, DeserializationOption::Filter(filter));
  if (err) {
    out->showToast("Bad response", now_ms);
    next_poll_ms_ = now_ms + POLL_PAUSED_MS;
    return;
  }

  JsonObjectConst item = doc["item"];
  if (item.isNull()) {
    // A valid response can omit item while the player transitions.
    out->pb.has_track = false;
    next_poll_ms_ = now_ms + POLL_PAUSED_MS;
    return;
  }

  out->pb.has_track = true;
  out->pb.has_device = true;
  out->pb.is_playing = doc["is_playing"] | false;
  out->pb.progress_ms = doc["progress_ms"] | 0u;
  out->pb.duration_ms = item["duration_ms"] | 0u;

  setStr(out->pb.track_id, ID_LEN, item["id"] | "");
  setStr(out->pb.title, TEXT_LEN, item["name"] | "");

  JsonArrayConst artists = item["artists"];
  setStr(out->pb.artist, TEXT_LEN,
         artists.size() > 0 ? (artists[0]["name"] | "") : "");

  JsonObjectConst album = item["album"];
  setStr(out->pb.album_id, ID_LEN, album["id"] | "");

  // Absent volume must stay unknown, never be coerced to zero.
  JsonVariantConst vol = doc["device"]["volume_percent"];
  out->pb.volume_pct = vol.isNull() ? -1 : (vol.as<int>());

  // Artwork only touches the network on an album change, and only on a cache
  // miss after that.
  const char *img = pickImage(album["images"], theme::ART_SIZE);
  if (img && out->pb.album_id[0]) {
    const std::string path = art_.ensure(out->pb.album_id, img);
    setStr(out->pb.art_path, PATH_LEN, path.c_str());
  } else {
    out->pb.art_path[0] = '\0';
  }

  polled_ = true;
  next_poll_ms_ =
      now_ms + (out->pb.is_playing ? POLL_PLAYING_MS : POLL_PAUSED_MS);
}

void SpotifySource::refreshLiked(AppState *out, uint32_t now_ms) {
  if (!liked_supported_) {
    out->pb.liked_known = false;
    return;
  }
  if (out->pb.track_id[0] == '\0') return;
  if (last_liked_track_ == out->pb.track_id) return;

  HttpResponse resp;
  const std::string url =
      std::string(API) + "/me/tracks/contains?ids=" + out->pb.track_id;
  if (!call("GET", url, "", &resp, out, now_ms)) return;

  if (resp.status == 403) {
    // Restricted for this app. Verified against /me/tracks, which uses the same
    // user-library-read scope and returns 200, so this is endpoint-specific
    // rather than a missing grant. Give up permanently instead of retrying at
    // the poll rate.
    NETLOG("saved-state unavailable: /me/tracks/contains is 403 for this app");
    liked_supported_ = false;
    out->pb.liked_known = false;
    return;
  }
  if (resp.status != 200) return;

  JsonDocument doc;
  if (deserializeJson(doc, resp.body) != DeserializationError::Ok) return;
  if (doc.is<JsonArray>() && doc.size() > 0) {
    out->pb.liked = doc[0] | false;
    out->pb.liked_known = true;
    last_liked_track_ = out->pb.track_id;
  }
}

// SPOTIFY_DIAG=1: probe a handful of endpoints once and log only their status
// codes, to tell "scope not actually usable" apart from "this one endpoint is
// restricted". Never logs tokens.
void SpotifySource::diagnose(AppState *out, uint32_t now_ms) {
  const char *probes[] = {
      "/me",
      "/me/player/devices",
      "/me/tracks?limit=1",
      "/me/albums?limit=1",
  };
  for (const char *p : probes) {
    HttpResponse r;
    if (call("GET", std::string(API) + p, "", &r, out, now_ms)) {
      NETLOG("DIAG %-24s -> %d %.120s", p, r.status,
             r.status >= 400 ? r.body.c_str() : "");
    } else {
      NETLOG("DIAG %-24s -> transport/auth failure", p);
    }
  }
}

void SpotifySource::step(AppState *out, CommandQueue<> *cmds, uint32_t now_ms) {
  polled_ = false;

  if (std::getenv("SPOTIFY_DIAG")) {
    static bool done = false;
    if (!done) {
      done = true;
      diagnose(out, now_ms);
    }
  }

  if (rate_limited_until_ms_ != 0) {
    if (now_ms < rate_limited_until_ms_) return;
    rate_limited_until_ms_ = 0;
  }

  Command c;
  while (cmds->pop(&c)) {
    runCommand(c, out, now_ms);
  }

  if (now_ms < next_poll_ms_) return;
  pollPlayer(out, now_ms);
  refreshLiked(out, now_ms);
}
