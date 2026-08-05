// Host unit tests for the display-free logic.
//
// Everything here has caused a real, user-visible bug at least once. The tests
// are written against the symptom, not just the function, so a regression says
// what broke rather than only that something did.
//
//   pio test -e test

#include <unity.h>

#include <cstring>

#include "core/CommandQueue.h"
#include "core/MergePolicy.h"
#include "core/ProgressClock.h"
#include "input/ButtonLogic.h"
#include "ui/TimeFormat.h"

// ---------------------------------------------------------------------------
// ButtonLogic
// ---------------------------------------------------------------------------

void test_tap_fires_on_release_not_press(void) {
  ButtonLogic b;
  TEST_ASSERT_EQUAL(BtnEvent::None, b.update(true, 0));
  TEST_ASSERT_EQUAL(BtnEvent::None, b.update(true, 100));
  TEST_ASSERT_EQUAL(BtnEvent::Tap, b.update(false, 200));
}

void test_long_press_fires_at_threshold_and_suppresses_tap(void) {
  ButtonLogic b;
  b.update(true, 0);
  TEST_ASSERT_EQUAL(BtnEvent::None, b.update(true, LONG_PRESS_MS - 1));
  TEST_ASSERT_EQUAL(BtnEvent::LongStart, b.update(true, LONG_PRESS_MS));
  // Releasing after a long press must NOT also emit a tap, or every
  // volume-hold would additionally skip a track.
  TEST_ASSERT_EQUAL(BtnEvent::LongEnd, b.update(false, LONG_PRESS_MS + 50));
}

void test_hold_repeats_at_interval(void) {
  ButtonLogic b;
  b.update(true, 0);
  TEST_ASSERT_EQUAL(BtnEvent::LongStart, b.update(true, LONG_PRESS_MS));
  TEST_ASSERT_EQUAL(BtnEvent::None, b.update(true, LONG_PRESS_MS + HOLD_REPEAT_MS - 1));
  TEST_ASSERT_EQUAL(BtnEvent::LongRepeat, b.update(true, LONG_PRESS_MS + HOLD_REPEAT_MS));
  TEST_ASSERT_EQUAL(BtnEvent::LongRepeat,
                    b.update(true, LONG_PRESS_MS + (HOLD_REPEAT_MS * 2)));
}

void test_button_is_reusable_after_release(void) {
  ButtonLogic b;
  b.update(true, 0);
  b.update(false, 100);  // tap
  b.update(true, 200);
  TEST_ASSERT_EQUAL(BtnEvent::Tap, b.update(false, 300));
}

// ---------------------------------------------------------------------------
// CommandQueue
// ---------------------------------------------------------------------------

void test_queue_is_fifo(void) {
  CommandQueue<8> q;
  q.push({CommandType::Next, 0});
  q.push({CommandType::Previous, 0});
  Command c;
  TEST_ASSERT_TRUE(q.pop(&c));
  TEST_ASSERT_EQUAL(CommandType::Next, c.type);
  TEST_ASSERT_TRUE(q.pop(&c));
  TEST_ASSERT_EQUAL(CommandType::Previous, c.type);
  TEST_ASSERT_FALSE(q.pop(&c));
}

void test_queue_drops_when_full_rather_than_blocking(void) {
  CommandQueue<4> q;  // capacity is CAPACITY-1 usable
  TEST_ASSERT_TRUE(q.push({CommandType::Next, 0}));
  TEST_ASSERT_TRUE(q.push({CommandType::Next, 0}));
  TEST_ASSERT_TRUE(q.push({CommandType::Next, 0}));
  TEST_ASSERT_FALSE(q.push({CommandType::Next, 0}));
}

void test_volume_commands_coalesce_to_the_latest_value(void) {
  // Holding volume must not enqueue one API call per 5% step.
  CommandQueue<8> q;
  q.pushCoalesced({CommandType::SetVolume, 40});
  q.pushCoalesced({CommandType::SetVolume, 45});
  q.pushCoalesced({CommandType::SetVolume, 50});

  Command c;
  TEST_ASSERT_TRUE(q.pop(&c));
  TEST_ASSERT_EQUAL(CommandType::SetVolume, c.type);
  TEST_ASSERT_EQUAL_INT(50, c.arg);
  TEST_ASSERT_FALSE(q.pop(&c));
}

void test_coalescing_does_not_disturb_other_commands(void) {
  CommandQueue<8> q;
  q.push({CommandType::Next, 0});
  q.pushCoalesced({CommandType::SetVolume, 30});
  q.pushCoalesced({CommandType::SetVolume, 35});

  Command c;
  TEST_ASSERT_TRUE(q.pop(&c));
  TEST_ASSERT_EQUAL(CommandType::Next, c.type);
  TEST_ASSERT_TRUE(q.pop(&c));
  TEST_ASSERT_EQUAL_INT(35, c.arg);
}

// ---------------------------------------------------------------------------
// ProgressClock  (regression: the 2s stutter)
// ---------------------------------------------------------------------------

void test_clock_resyncs_only_on_a_new_publish(void) {
  // The bug: the UI re-read the same published state every frame and copied
  // its progress back, so extrapolation was wiped and the clock only moved
  // when a poll landed.
  ProgressClock c;
  TEST_ASSERT_TRUE(c.sync(1, 10000));
  TEST_ASSERT_EQUAL_UINT32(10000, c.value());

  c.advance(500, true, 200000);
  TEST_ASSERT_EQUAL_UINT32(10500, c.value());

  // Same sequence: this is a re-read, not new data. Must not clobber.
  TEST_ASSERT_FALSE(c.sync(1, 10000));
  TEST_ASSERT_EQUAL_UINT32(10500, c.value());

  c.advance(500, true, 200000);
  TEST_ASSERT_EQUAL_UINT32(11000, c.value());

  // New sequence: authoritative, so resync.
  TEST_ASSERT_TRUE(c.sync(2, 12000));
  TEST_ASSERT_EQUAL_UINT32(12000, c.value());
}

void test_clock_does_not_advance_while_paused(void) {
  ProgressClock c;
  c.sync(1, 5000);
  c.advance(1000, false, 200000);
  TEST_ASSERT_EQUAL_UINT32(5000, c.value());
}

void test_clock_clamps_at_duration(void) {
  ProgressClock c;
  c.sync(1, 9500);
  c.advance(5000, true, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, c.value());
}

// ---------------------------------------------------------------------------
// TimeFormat
// ---------------------------------------------------------------------------

void test_elapsed_formatting(void) {
  char b[16];
  formatElapsed(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0:00", b);
  formatElapsed(49000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0:49", b);
  formatElapsed(157000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2:37", b);
}

void test_remaining_counts_down(void) {
  char b[16];
  formatRemaining(0, 157000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("-2:37", b);
  formatRemaining(49000, 157000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("-1:48", b);
}

void test_remaining_clamps_at_zero(void) {
  // Extrapolation can push progress past duration between polls; a negative
  // remaining would render as garbage.
  char b[16];
  formatRemaining(200000, 157000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("-0:00", b);
}

// ---------------------------------------------------------------------------
// MergePolicy
// ---------------------------------------------------------------------------

static AppState makeSource(const char *track, bool playing, int vol, bool liked,
                           bool liked_known) {
  AppState s;
  s.link = LinkStatus::Online;
  s.pb.has_track = true;
  setStr(s.pb.track_id, ID_LEN, track);
  setStr(s.pb.album_id, ID_LEN, "album");
  setStr(s.pb.title, TEXT_LEN, "Title");
  s.pb.is_playing = playing;
  s.pb.volume_pct = vol;
  s.pb.liked = liked;
  s.pb.liked_known = liked_known;
  s.pb.duration_ms = 200000;
  s.pb.progress_ms = 1000;
  return s;
}

void test_merge_ignores_playback_when_not_polled(void) {
  AppState dst;
  AppState src = makeSource("t1", true, 50, true, true);
  mergePlayback(&dst, src, /*polled=*/false, 0);
  TEST_ASSERT_FALSE(dst.pb.has_track);
  TEST_ASSERT_EQUAL_UINT32(0, dst.publish_seq);
  // Link status still propagates, so connection problems surface immediately.
  TEST_ASSERT_EQUAL(LinkStatus::Online, dst.link);
}

void test_settle_window_protects_an_optimistic_flip(void) {
  // The bug this prevents: you press pause, the icon flips, then a response
  // already in flight snaps it back and the device looks broken.
  AppState dst;
  dst.pb.is_playing = false;          // user just paused, optimistically
  dst.settle_playing_until_ms = 1500;

  AppState src = makeSource("t1", true, 50, false, true);  // remote still playing
  mergePlayback(&dst, src, true, /*now=*/500);
  TEST_ASSERT_FALSE(dst.pb.is_playing);

  // After the window closes the remote wins again.
  mergePlayback(&dst, src, true, /*now=*/1600);
  TEST_ASSERT_TRUE(dst.pb.is_playing);
}

void test_settle_windows_are_independent(void) {
  AppState dst;
  dst.pb.volume_pct = 80;
  dst.settle_volume_until_ms = 2000;
  dst.pb.is_playing = false;
  dst.settle_playing_until_ms = 0;  // not protected

  AppState src = makeSource("t1", true, 20, false, true);
  mergePlayback(&dst, src, true, 500);
  TEST_ASSERT_EQUAL_INT(80, dst.pb.volume_pct);  // protected
  TEST_ASSERT_TRUE(dst.pb.is_playing);           // not protected
}

void test_liked_known_is_not_downgraded_on_the_same_track(void) {
  // /me/library/contains can be unavailable, leaving saved-state unknowable.
  // A like the user performed this session is then the only truth we have and
  // must survive polls that report "unknown".
  AppState dst;
  setStr(dst.pb.track_id, ID_LEN, "t1");
  dst.pb.liked = true;
  dst.pb.liked_known = true;

  AppState src = makeSource("t1", true, 50, false, /*liked_known=*/false);
  mergePlayback(&dst, src, true, 99999);
  TEST_ASSERT_TRUE(dst.pb.liked_known);
}

void test_liked_known_resets_on_a_track_change(void) {
  AppState dst;
  setStr(dst.pb.track_id, ID_LEN, "t1");
  dst.pb.liked_known = true;

  AppState src = makeSource("t2", true, 50, false, /*liked_known=*/false);
  mergePlayback(&dst, src, true, 99999);
  TEST_ASSERT_FALSE(dst.pb.liked_known);
}

void test_newer_toast_wins_and_older_is_ignored(void) {
  AppState dst;
  dst.showToast("UI message", 1000);  // expires at 3000

  AppState src;
  src.showToast("stale", 0);  // expires at 2000 — older
  mergePlayback(&dst, src, false, 1000);
  TEST_ASSERT_EQUAL_STRING("UI message", dst.toast);

  AppState newer;
  newer.showToast("fresher", 2000);  // expires at 4000
  mergePlayback(&dst, newer, false, 2000);
  TEST_ASSERT_EQUAL_STRING("fresher", dst.toast);
}

void test_publish_seq_increments_only_on_a_real_poll(void) {
  AppState dst;
  AppState src = makeSource("t1", true, 50, false, true);
  mergePlayback(&dst, src, false, 0);
  TEST_ASSERT_EQUAL_UINT32(0, dst.publish_seq);
  mergePlayback(&dst, src, true, 0);
  TEST_ASSERT_EQUAL_UINT32(1, dst.publish_seq);
  mergePlayback(&dst, src, true, 0);
  TEST_ASSERT_EQUAL_UINT32(2, dst.publish_seq);
}

void test_unknown_volume_is_preserved_not_zeroed(void) {
  AppState dst;
  AppState src = makeSource("t1", true, -1, false, true);
  mergePlayback(&dst, src, true, 99999);
  TEST_ASSERT_EQUAL_INT(-1, dst.pb.volume_pct);
}

// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_tap_fires_on_release_not_press);
  RUN_TEST(test_long_press_fires_at_threshold_and_suppresses_tap);
  RUN_TEST(test_hold_repeats_at_interval);
  RUN_TEST(test_button_is_reusable_after_release);

  RUN_TEST(test_queue_is_fifo);
  RUN_TEST(test_queue_drops_when_full_rather_than_blocking);
  RUN_TEST(test_volume_commands_coalesce_to_the_latest_value);
  RUN_TEST(test_coalescing_does_not_disturb_other_commands);

  RUN_TEST(test_clock_resyncs_only_on_a_new_publish);
  RUN_TEST(test_clock_does_not_advance_while_paused);
  RUN_TEST(test_clock_clamps_at_duration);

  RUN_TEST(test_elapsed_formatting);
  RUN_TEST(test_remaining_counts_down);
  RUN_TEST(test_remaining_clamps_at_zero);

  RUN_TEST(test_merge_ignores_playback_when_not_polled);
  RUN_TEST(test_settle_window_protects_an_optimistic_flip);
  RUN_TEST(test_settle_windows_are_independent);
  RUN_TEST(test_liked_known_is_not_downgraded_on_the_same_track);
  RUN_TEST(test_liked_known_resets_on_a_track_change);
  RUN_TEST(test_newer_toast_wins_and_older_is_ignored);
  RUN_TEST(test_publish_seq_increments_only_on_a_real_poll);
  RUN_TEST(test_unknown_volume_is_preserved_not_zeroed);

  return UNITY_END();
}
