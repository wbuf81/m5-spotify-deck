#!/usr/bin/env python3
"""Visual regression tests against real emulator framebuffers.

The unit tests cover logic. These cover rendering, because every display bug
found so far — toast text ghosting under the timecodes, the play glyph
vanishing after a like, the clock stuttering — was invisible to logic tests and
obvious in a screenshot.

Each case runs the emulator headlessly with env hooks, dumps the framebuffer,
and asserts on pixels. No golden images: those rot every time a colour or a
font changes. These assert on properties that should hold regardless.

  python3 tools/visual_tests.py
"""

import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, ".pio", "build", "native", "program")

# Regions, matching src/ui/Theme.h
ART = (8, 8, 176, 176)
HEART = (185, 165, 24, 24)
ROW = (0, 206, 320, 34)
LEFT_TIME = (8, 208, 46, 18)
RIGHT_TIME = (250, 208, 62, 18)
GLYPH = (151, 209, 18, 16)
VIS = (192, 114, 120, 50)
TEXT_TOP = (192, 8, 120, 26)
# Beacon sprite on the status screen: 96x96, centred, pushed at y=18.
BEACON = (112, 18, 96, 96)


def read_bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    row = w * 3
    pad = (4 - row % 4) % 4
    px = [[None] * w for _ in range(h)]
    for ry in range(h):
        base = off + ry * (row + pad)
        y = h - 1 - ry
        for x in range(w):
            i = base + x * 3
            px[y][x] = (d[i + 2], d[i + 1], d[i])
    return px


# M5GFX's SDL backend has an unsynchronised startup race: Panel_sdl::init()
# does _list_monitor.push_back() on our thread inside M5.begin(), while the SDL
# thread is already iterating that same std::list in _update_proc(). No mutex
# guards it. It segfaults roughly 1 run in 80, always during startup.
#
# Upstream bug, emulator-only — the device build has no Panel_sdl. It appears
# more often when the suite spawns processes back to back, and two consecutive
# hits were observed in one run, so one retry was not enough.
#
# Retried rather than ignored: a genuinely broken build crashes on every
# attempt and still fails. Retries are counted and reported so the flakiness
# can never quietly become invisible.
ATTEMPTS = 3
STARTUP_RACE_RETRIES = 0


def run(env_extra, out_bmp, unpin_mode=False):
    global STARTUP_RACE_RETRIES
    env = dict(os.environ)
    env["EMU_DUMP"] = out_bmp
    env.setdefault("EMU_FAKE", "1")  # never hit the network from a test
    # Pin the classic view unless a test asks otherwise. Modes rotate per track,
    # so without this every layout assertion below would be testing whichever
    # mode the fixture happened to hash to.
    env.setdefault("EMU_MODE", "0")
    env.update(env_extra)
    if unpin_mode:
        env.pop("EMU_MODE", None)

    last = None
    for attempt in range(ATTEMPTS):
        r = subprocess.run([BIN], env=env, capture_output=True, timeout=60)
        if r.returncode == 0:
            if attempt:
                STARTUP_RACE_RETRIES += 1
            return read_bmp(out_bmp)
        last = r
    raise RuntimeError(
        f"emulator exited {last.returncode} on all {ATTEMPTS} attempts "
        f"(not the startup race): "
        f"{last.stderr[:300]}")


def region(px, box):
    x, y, w, h = box
    return [px[yy][xx] for yy in range(y, y + h) for xx in range(x, x + w)]


def is_amber(p):
    r, g, b = p
    return r > 120 and 50 < g < 200 and b < 80 and r > b + 60


def is_green(p):
    r, g, b = p
    return g > 90 and g > r + 40 and g > b + 40


def is_bright(p):
    return sum(p) > 300


def count(px, box, pred):
    return sum(1 for p in region(px, box) if pred(p))


def distinct(px, box):
    return len(set(region(px, box)))


# ---------------------------------------------------------------------------

CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def toast_leaves_no_residue(tmp):
    """A toast must not ghost under the timecodes after it expires.

    Regression: the row clear was a hardcoded 10px, sized for a 6x8 font. The
    taller JetBrains Mono face left its lower rows behind.
    """
    px = run({"EMU_TOAST": "Saved to Liked Songs", "EMU_EXIT_MS": "3200"},
             tmp("residue"))
    amber = count(px, ROW, is_amber)
    assert amber == 0, f"{amber} amber pixels remain in the info row after expiry"


@case
def glyph_returns_after_toast(tmp):
    """The play glyph must come back when a toast clears.

    Regression: only the timecodes were repainted, so the glyph stayed erased
    until the next play/pause and looked like the button had vanished.
    """
    px = run({"EMU_TOAST": "Saved to Liked Songs", "EMU_EXIT_MS": "3200"},
             tmp("glyph_back"))
    lit = count(px, GLYPH, is_bright)
    assert lit > 20, f"glyph missing after toast expiry (only {lit} lit pixels)"


@case
def toast_replaces_the_timecodes(tmp):
    """While a toast shows it owns the row: no timecodes underneath it."""
    px = run({"EMU_TOAST": "Saved to Liked Songs", "EMU_EXIT_MS": "700"},
             tmp("toast_on"))
    assert count(px, ROW, is_amber) > 30, "toast not visible while active"
    left = count(px, LEFT_TIME, is_bright)
    assert left == 0, f"timecode still drawn under the toast ({left} pixels)"


@case
def elapsed_time_advances_every_second(tmp):
    """Regression: the clock only moved when a poll landed, so it jumped in 2s
    steps. Sampling one second apart must show different pixels."""
    a = run({"EMU_EXIT_MS": "1400"}, tmp("t_a"))
    b = run({"EMU_EXIT_MS": "2400"}, tmp("t_b"))
    assert region(a, LEFT_TIME) != region(b, LEFT_TIME), \
        "elapsed time identical one second apart"


@case
def remaining_time_counts_down(tmp):
    """The right-hand figure is remaining time, so it must change too.

    It previously showed total duration, which never moves and reads as frozen.
    """
    a = run({"EMU_EXIT_MS": "1400"}, tmp("r_a"))
    b = run({"EMU_EXIT_MS": "2400"}, tmp("r_b"))
    assert region(a, RIGHT_TIME) != region(b, RIGHT_TIME), \
        "remaining time did not change over one second"


@case
def artwork_renders_and_stays_in_its_box(tmp):
    """Album art must decode, and must not bleed into the text column."""
    px = run({"EMU_EXIT_MS": "900"}, tmp("art"))
    assert distinct(px, ART) > 200, "artwork region looks flat; JPEG decode failed"
    gutter = (185, 20, 6, 140)
    assert distinct(px, gutter) <= 2, "artwork bled past its region into the gutter"


@case
def liking_turns_the_heart_green(tmp):
    """Fixture 1 starts unliked, so this is a real transition."""
    px = run({"EMU_TRACK": "1", "EMU_FIRE": "like",
              "EMU_FIRE_MS": "400", "EMU_EXIT_MS": "1200"}, tmp("liked"))
    g = count(px, HEART, is_green)
    assert g > 25, f"heart not filled after like ({g} green pixels)"


@case
def unliking_drains_the_heart(tmp):
    """Fixture 0 starts liked, so this is a real transition."""
    px = run({"EMU_TRACK": "0", "EMU_FIRE": "unlike",
              "EMU_FIRE_MS": "400", "EMU_EXIT_MS": "1200"}, tmp("unliked"))
    g = count(px, HEART, is_green)
    assert g < 10, f"heart still green after unlike ({g} green pixels)"


@case
def scene_animates_while_playing(tmp):
    """Ambient, but it must actually move."""
    a = run({"EMU_EXIT_MS": "1500"}, tmp("vis_a"))
    b = run({"EMU_EXIT_MS": "2100"}, tmp("vis_b"))
    assert region(a, VIS) != region(b, VIS), "scene is static while playing"


@case
def all_four_scenes_render_and_differ(tmp):
    """Each scene must draw something, and none may look like another."""
    frames = {}
    for n in range(4):
        px = run({"EMU_SCENE": str(n), "EMU_EXIT_MS": "2600"}, tmp(f"scene{n}"))
        lit = sum(1 for p in region(px, VIS) if sum(p) > 60)
        assert lit > 40, f"scene {n} rendered almost nothing ({lit} lit pixels)"
        frames[n] = region(px, VIS)

    for a in range(4):
        for b in range(a + 1, 4):
            assert frames[a] != frames[b], f"scenes {a} and {b} render identically"


@case
def scene_rotates_between_tracks(tmp):
    """A new song should bring a new scene."""
    a = run({"EMU_TRACK": "0", "EMU_EXIT_MS": "2600"}, tmp("rot0"))
    b = run({"EMU_TRACK": "1", "EMU_EXIT_MS": "2600"}, tmp("rot1"))
    assert region(a, VIS) != region(b, VIS), "same scene content across tracks"


@case
def scene_settles_when_paused(tmp):
    """Paused playback must calm the display rather than keep dancing.

    Two samples taken well after the pause should be identical, since the bars
    have decayed to their resting baseline.
    """
    common = {"EMU_FIRE": "playpause", "EMU_FIRE_MS": "300"}
    a = run({**common, "EMU_EXIT_MS": "2600"}, tmp("pause_a"))
    b = run({**common, "EMU_EXIT_MS": "3200"}, tmp("pause_b"))
    assert region(a, VIS) == region(b, VIS), "scene still moving while paused"


@case
def scene_tint_follows_the_album(tmp):
    """Colour is sampled from the artwork, so two covers must differ.

    Both runs pin the same scene, so this isolates the tint rather than
    accidentally passing because two different scenes were drawn.
    """
    blue = run({"EMU_TRACK": "0", "EMU_SCENE": "0", "EMU_EXIT_MS": "1500"},
               tmp("tint0"))
    green = run({"EMU_TRACK": "3", "EMU_SCENE": "0", "EMU_EXIT_MS": "1500"},
                tmp("tint3"))

    def dominant(px):
        pixels = [p for p in region(px, VIS) if sum(p) > 120]
        if not pixels:
            return (0, 0, 0)
        n = len(pixels)
        return tuple(sum(c[i] for c in pixels) // n for i in range(3))

    a, b = dominant(blue), dominant(green)
    dist = sum(abs(a[i] - b[i]) for i in range(3))
    assert dist > 60, f"visualiser tint barely differs between albums ({a} vs {b})"


@case
def song_title_starts_at_the_top_of_the_column(tmp):
    """Text is top-aligned to leave room for the visualiser below it."""
    px = run({"EMU_EXIT_MS": "900"}, tmp("toptext"))
    lit = count(px, TEXT_TOP, is_bright)
    assert lit > 40, f"title not at the top of the column ({lit} lit pixels)"


def _mean(px, box):
    vals = [sum(p) / 3 for p in region(px, box)]
    return sum(vals) / len(vals)


@case
def dimming_darkens_artwork_as_well_as_text(tmp):
    """The backlight dims every pixel, so the emulator must too.

    It previously scaled only the palette, leaving album art at full brightness
    while the text dimmed around it — which misrepresents the device at exactly
    the moment you are judging how it looks on a desk at night.
    """
    bright = run({"EMU_EXIT_MS": "1200"}, tmp("bright"))
    dim = run({"EMU_DIM_AFTER_MS": "800", "EMU_EXIT_MS": "2400"}, tmp("dim"))

    art_ratio = _mean(dim, ART) / max(1.0, _mean(bright, ART))
    text_ratio = _mean(dim, (192, 8, 120, 60)) / max(1.0, _mean(bright, (192, 8, 120, 60)))

    assert art_ratio < 0.6, f"artwork did not dim (ratio {art_ratio:.2f})"
    assert text_ratio < 0.6, f"text did not dim (ratio {text_ratio:.2f})"
    # They must dim together; art staying bright while text dims was the bug.
    assert abs(art_ratio - text_ratio) < 0.25, (
        f"artwork and text dim by different amounts "
        f"({art_ratio:.2f} vs {text_ratio:.2f})")


MODE_NAMES = ["classic", "pixel", "gameboy", "cassette", "scoreboard",
              "cyberdeck", "synthwave"]


@case
def every_view_mode_renders_something_distinct(tmp):
    """All seven modes must draw, and none may be mistakable for another."""
    frames = {}
    for n, name in enumerate(MODE_NAMES):
        px = run({"EMU_MODE": str(n), "EMU_EXIT_MS": "2600"}, tmp(f"mode{n}"))
        lit = sum(1 for p in region(px, (0, 0, 320, 240)) if sum(p) > 90)
        assert lit > 2000, f"mode {name} rendered almost nothing ({lit} lit px)"
        frames[name] = region(px, (0, 0, 320, 240))

    for i, a in enumerate(MODE_NAMES):
        for b in MODE_NAMES[i + 1:]:
            assert frames[a] != frames[b], f"{a} and {b} render identically"


@case
def view_mode_rotates_between_tracks(tmp):
    """A new song should bring a new view."""
    seen = set()
    for track in range(5):
        px = run({"EMU_TRACK": str(track), "EMU_EXIT_MS": "2000"},
                 tmp(f"rot{track}"), unpin_mode=True)
        seen.add(tuple(region(px, (0, 0, 320, 120))[::37]))
    assert len(seen) >= 3, f"only {len(seen)} distinct views across 5 tracks"


@case
def gameboy_mode_draws_the_handheld_in_colour(tmp):
    """The mode is now the device itself with a colourised cover in its LCD.

    It used to be four DMG greens over the whole screen, and this test asserted
    exactly that — so it correctly failed when the design changed. Rewritten
    against what the mode is for: a recognisable body, and album colour inside
    the screen rather than a green wash.
    """
    px = run({"EMU_MODE": "2", "EMU_EXIT_MS": "2000"}, tmp("gb"))

    # The plastic body: a large flat region of one light, desaturated colour.
    body = region(px, (20, 150, 100, 40))
    from collections import Counter
    dominant, count = Counter(body).most_common(1)[0]
    r, g, b = dominant
    assert count > len(body) * 0.5, "no solid body colour where the shell should be"
    assert min(r, g, b) > 100, f"body is too dark to read as plastic: {dominant}"
    assert max(r, g, b) - min(r, g, b) < 60, f"body is too saturated: {dominant}"

    # The LCD must carry the album's colour, not a monochrome wash.
    lcd = region(px, (30, 30, 100, 84))
    assert len(set(lcd)) > 20, "LCD looks flat; artwork did not render"
    coloured = sum(1 for (r, g, b) in lcd if max(r, g, b) - min(r, g, b) > 40)
    assert coloured > 200, f"LCD is monochrome ({coloured} saturated pixels)"


@case
def mode_keys_do_not_resize_the_window(tmp):
    """Panel_sdl binds plain 1-6 to window zoom and r/l to 90-degree rotation,
    both with no modifier. The harness put view modes on 1-7, so picking a view
    also resized the window.

    No pixel assertion can catch this: the framebuffer stays 320x240 whatever
    the window does. So the emulator injects a keypress and reports the real
    SDL window size either side of it.
    """
    env = dict(os.environ)
    env.update({"EMU_HARNESS": "1", "EMU_FAKE": "1", "EMU_WINCHECK": "1",
                "EMU_EXIT_MS": "9000"})
    r = subprocess.run([BIN], env=env, capture_output=True, timeout=60)
    out = r.stderr.decode(errors="replace")
    assert "[wincheck]" in out, f"window check did not run: {out[-300:]}"
    assert "UNCHANGED (ok)" in out, f"a plain keypress resized the window:\n{out}"


@case
def the_status_screen_eventually_sleeps(tmp):
    """A lit CONNECTING beacon must not burn all night.

    Regression: the idle timer was reset by pb.is_playing, which keeps its last
    value when the link drops. If music had been playing, the flag stayed true
    and the timer reset every frame — so the device could never sleep while
    disconnected, which is precisely when it should.
    """
    lit = run({"EMU_LINK": "offline", "EMU_EXIT_MS": "1200"}, tmp("st_lit"))
    assert count(lit, (0, 0, 320, 240), is_amber) > 100, "status screen not shown"

    slept = run({"EMU_LINK": "offline", "EMU_DIM_AFTER_MS": "300",
                 "EMU_SLEEP_AFTER_MS": "800", "EMU_EXIT_MS": "2500"},
                tmp("st_slept"))
    lit_px = sum(1 for p in region(slept, (0, 0, 320, 240)) if sum(p) > 60)
    assert lit_px < 400, f"screen still lit after the sleep timeout ({lit_px} px)"


@case
def offline_shows_the_status_screen(tmp):
    """Losing the link must show a real screen, not a corner dot."""
    px = run({"EMU_LINK": "offline", "EMU_EXIT_MS": "900"}, tmp("offline"))
    assert count(px, (0, 0, 320, 200), is_amber) > 200, \
        "offline state is not clearly signalled"
    assert distinct(px, ART) < 40, "now-playing artwork still drawn while offline"


@case
def connecting_differs_from_offline(tmp):
    """A normal wait must not look like a fault."""
    off = run({"EMU_LINK": "offline", "EMU_EXIT_MS": "900"}, tmp("c_off"))
    con = run({"EMU_LINK": "connecting", "EMU_EXIT_MS": "900"}, tmp("c_con"))
    assert count(con, BEACON, is_green) > 40, \
        "connecting state should read as accent, not alarm"
    assert count(off, BEACON, is_amber) > 40, \
        "offline state should read as alarm"
    assert region(off, (0, 120, 320, 60)) != region(con, (0, 120, 320, 60)), \
        "offline and connecting render identically"


@case
def nothing_playing_shows_status_not_a_blank_screen(tmp):
    px = run({"EMU_LINK": "notrack", "EMU_EXIT_MS": "900"}, tmp("notrack"))
    assert count(px, (0, 100, 320, 100), is_bright) > 60, \
        "no headline drawn when nothing is playing"


# ---------------------------------------------------------------------------


def main():
    if not os.path.exists(BIN):
        sys.exit(f"build first: pio run -e native   (missing {BIN})")

    with tempfile.TemporaryDirectory() as td:
        def tmp(name):
            return os.path.join(td, f"{name}.bmp")

        failures = []
        for fn in CASES:
            name = fn.__name__
            try:
                fn(tmp)
                print(f"  PASS  {name}")
            except AssertionError as e:
                print(f"  FAIL  {name}\n          {e}")
                failures.append(name)
            except Exception as e:  # noqa: BLE001
                print(f"  ERROR {name}\n          {type(e).__name__}: {e}")
                failures.append(name)

    print()
    if STARTUP_RACE_RETRIES:
        print(f"note: {STARTUP_RACE_RETRIES} run(s) retried after the known "
              f"M5GFX Panel_sdl startup race (emulator-only, upstream)")
    print(f"{len(CASES) - len(failures)}/{len(CASES)} visual checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
