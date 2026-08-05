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


def run(env_extra, out_bmp):
    env = dict(os.environ)
    env["EMU_DUMP"] = out_bmp
    env.setdefault("EMU_FAKE", "1")  # never hit the network from a test
    env.update(env_extra)
    r = subprocess.run([BIN], env=env, capture_output=True, timeout=60)
    if r.returncode != 0:
        raise RuntimeError(f"emulator exited {r.returncode}: {r.stderr[:300]}")
    return read_bmp(out_bmp)


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
def visualiser_animates_while_playing(tmp):
    """It is decorative, but it must actually move."""
    a = run({"EMU_EXIT_MS": "1500"}, tmp("vis_a"))
    b = run({"EMU_EXIT_MS": "2100"}, tmp("vis_b"))
    assert region(a, VIS) != region(b, VIS), "visualiser is static while playing"


@case
def visualiser_settles_when_paused(tmp):
    """Paused playback must calm the display rather than keep dancing.

    Two samples taken well after the pause should be identical, since the bars
    have decayed to their resting baseline.
    """
    common = {"EMU_FIRE": "playpause", "EMU_FIRE_MS": "300"}
    a = run({**common, "EMU_EXIT_MS": "2600"}, tmp("pause_a"))
    b = run({**common, "EMU_EXIT_MS": "3200"}, tmp("pause_b"))
    assert region(a, VIS) == region(b, VIS), "visualiser still moving while paused"


@case
def visualiser_tint_follows_the_album(tmp):
    """The colour is sampled from the artwork, so two covers must differ."""
    blue = run({"EMU_TRACK": "0", "EMU_EXIT_MS": "1500"}, tmp("tint0"))
    green = run({"EMU_TRACK": "3", "EMU_EXIT_MS": "1500"}, tmp("tint3"))

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
    print(f"{len(CASES) - len(failures)}/{len(CASES)} visual checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
