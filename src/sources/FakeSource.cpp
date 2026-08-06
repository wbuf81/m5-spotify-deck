#include "FakeSource.h"

#include <cstdio>
#include <cstdlib>

namespace {

struct Fixture {
  const char *track_id;
  const char *album_id;
  const char *title;
  const char *artist;
  const char *art;
  uint32_t duration_ms;
  bool liked;
};

// Title lengths vary deliberately: the long one exists to prove the text column
// truncates rather than spilling, and the accented one to prove Latin-1 renders.
const Fixture FIXTURES[] = {
    {"t1", "aurora", "Everything In Its Right Place", "Radiohead",
     "assets/art/aurora.jpg", 251000, true},
    {"t2", "rings", "Teardrop", "Massive Attack", "assets/art/rings.jpg", 329000,
     false},
    {"t3", "monolith",
     "A Deliberately Overlong Title That Must Truncate Cleanly",
     "The Edge Case Ensemble", "assets/art/monolith.jpg", 182000, false},
    {"t4", "meridian", "Redbone", "Childish Gambino", "assets/art/meridian.jpg",
     327000, true},
    {"t5", "duskpop", "Jóga", "Björk", "assets/art/duskpop.jpg", 303000, false},
};

constexpr int FIXTURE_COUNT = sizeof(FIXTURES) / sizeof(FIXTURES[0]);

}  // namespace

void FakeSource::loadTrack(int index, uint32_t now_ms) {
  index_ = ((index % FIXTURE_COUNT) + FIXTURE_COUNT) % FIXTURE_COUNT;
  const Fixture &f = FIXTURES[index_];

  remote_.has_track = true;
  remote_.has_device = true;
  setStr(remote_.track_id, ID_LEN, f.track_id);
  setStr(remote_.album_id, ID_LEN, f.album_id);
  setStr(remote_.title, TEXT_LEN, f.title);
  setStr(remote_.artist, TEXT_LEN, f.artist);
  setStr(remote_.art_path, PATH_LEN, f.art);
  remote_.duration_ms = f.duration_ms;
  remote_.progress_ms = 0;
  remote_.liked = f.liked;
  remote_.liked_known = true;
  last_advance_ms_ = now_ms;
}

void FakeSource::begin(AppState *st, uint32_t now_ms) {
  st->link = LinkStatus::Online;

  int start = 0;
#if defined(EMULATOR)
  // EMU_TRACK=<n> starts on a chosen fixture, so specific layout cases (long
  // titles, accents) can be captured without waiting for them to come around.
  if (const char *t = std::getenv("EMU_TRACK")) start = std::atoi(t);
#endif
  loadTrack(start, now_ms);
  remote_.is_playing = true;
  remote_.volume_pct = 70;
  last_advance_ms_ = now_ms;
  last_publish_ms_ = 0;
  publish(st, now_ms);
}

void FakeSource::applyToRemote(const Command &c, uint32_t now_ms) {
  switch (c.type) {
    case CommandType::PlayPause:
      remote_.is_playing = !remote_.is_playing;
      break;
    case CommandType::Next:
      loadTrack(index_ + 1, now_ms);
      remote_.is_playing = true;
      break;
    case CommandType::Previous:
      // Player convention: restart the track unless we are near its start.
      if (remote_.progress_ms > 3000) {
        remote_.progress_ms = 0;
      } else {
        loadTrack(index_ - 1, now_ms);
      }
      remote_.is_playing = true;
      break;
    case CommandType::SetVolume:
      remote_.volume_pct = c.arg < 0 ? 0 : (c.arg > 100 ? 100 : c.arg);
      break;
    case CommandType::ToggleLike:
      remote_.liked = !remote_.liked;
      break;
    case CommandType::None:
      break;
  }
}

void FakeSource::publish(AppState *st, uint32_t now_ms) {
  setStr(st->pb.track_id, ID_LEN, remote_.track_id);
  setStr(st->pb.album_id, ID_LEN, remote_.album_id);
  setStr(st->pb.title, TEXT_LEN, remote_.title);
  setStr(st->pb.artist, TEXT_LEN, remote_.artist);
  setStr(st->pb.art_path, PATH_LEN, remote_.art_path);
  st->pb.has_track = remote_.has_track;
  st->pb.has_device = remote_.has_device;
  st->pb.duration_ms = remote_.duration_ms;

  // Resync the UI's extrapolated progress to the authoritative value.
  st->pb.progress_ms = remote_.progress_ms;

  // Settle windows: a field the user just changed optimistically is left alone
  // until the window expires, so a response already in flight cannot snap it
  // back and make the device look broken.
  if (st->settle_playing.elapsed(now_ms)) {
    st->pb.is_playing = remote_.is_playing;
  }
  if (st->settle_volume.elapsed(now_ms)) {
    st->pb.volume_pct = remote_.volume_pct;
  }
  if (st->settle_liked.elapsed(now_ms)) {
    st->pb.liked = remote_.liked;
  }
  st->pb.liked_known = remote_.liked_known;

  last_publish_ms_ = now_ms;
}

void FakeSource::poll(AppState *st, CommandQueue<> *cmds, uint32_t now_ms) {
  published_ = false;

  // Queue incoming commands with a simulated network delay.
  Command c;
  while (cmds->pop(&c)) {
    for (auto &p : pending_) {
      if (!p.used) {
        p.cmd = c;
        p.apply_at_ms = now_ms + FAKE_LATENCY_MS;
        p.used = true;
        break;
      }
    }
  }

  for (auto &p : pending_) {
    if (p.used && now_ms >= p.apply_at_ms) {
      applyToRemote(p.cmd, now_ms);
      p.used = false;
    }
  }

  // Advance the remote's own clock.
  const uint32_t elapsed = now_ms - last_advance_ms_;
  last_advance_ms_ = now_ms;
  if (remote_.is_playing && remote_.has_track) {
    remote_.progress_ms += elapsed;
    if (remote_.progress_ms >= remote_.duration_ms) {
      loadTrack(index_ + 1, now_ms);
      remote_.is_playing = true;
    }
  }

  if (now_ms - last_publish_ms_ >= FAKE_POLL_MS) {
    publish(st, now_ms);
    published_ = true;
  }
}
