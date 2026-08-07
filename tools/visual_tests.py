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
# Heart lives in the shared StatusStrip, right of the centre glyph.
HEART = (191, 204, 24, 24)
# Speaker + volume figure, alone on the strip's left half.
VOLUME = (74, 206, 60, 18)
# Progress bar row in the strip.
BARROW = (8, 200, 304, 3)
ROW = (0, 206, 320, 34)
LEFT_TIME = (8, 208, 46, 18)
RIGHT_TIME = (250, 208, 62, 18)
GLYPH = (151, 209, 18, 16)
VIS = (192, 114, 120, 50)
TEXT_TOP = (192, 8, 120, 26)
# Beacon sprite on the status screen: 96x96, centred, pushed at y=18.
BEACON = (112, 18, 96, 96)
# Battery glyph in the StatusStrip, between the play glyph and the volume.
BATTERY = (234, 212, 22, 14)


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
def loading_artwork_says_fetching_not_missing(tmp):
    """A cover in flight must not be reported as absent."""
    px = run({"EMU_ARTLOADING": "1", "EMU_EXIT_MS": "900"}, tmp("art_loading"))
    # The vinyl's label is drawn in the tint, which falls back to accent green
    # when no cover exists yet. The failure block contains no green at all, so
    # the label is the discriminator.
    green = count(px, ART, is_green)
    assert green > 300, f"vinyl label missing ({green} green px) — failure block?"
    lit = count(px, ART, lambda p: sum(p) > 90)
    assert lit > 800, f"vinyl placeholder not drawn ({lit} lit px)"


@case
def liking_sends_daisy_across_the_strip(tmp):
    """A like triggers Daisy's victory lap along the bottom strip."""
    def whiteish(p):
        r, g, b = p
        return r > 200 and g > 200 and b > 170
    # Mid-run: fired at 400ms, sampled ~750ms in — she is around x=110.
    mid = run({"EMU_TRACK": "1", "EMU_FIRE": "like", "EMU_FIRE_MS": "400",
               "EMU_EXIT_MS": "900"}, tmp("lap_mid"))
    lap = count(mid, (40, 194, 55, 44), whiteish)
    assert lap > 15, f"no Daisy in the strip mid-run ({lap} white px)"


@case
def liking_runs_daisy_on_mode_views_too(tmp):
    """The celebration must follow the user to whatever view is up."""
    def whiteish(p):
        r, g, b = p
        return r > 200 and g > 200 and b > 170
    # Synthwave pinned; its lower third is dark grid lines, so her white chest
    # stands out. Fired at 400ms, sampled ~750ms in.
    mid = run({"EMU_MODE": "4", "EMU_TRACK": "1", "EMU_FIRE": "like",
               "EMU_FIRE_MS": "400", "EMU_EXIT_MS": "900"}, tmp("lap_mode"))
    lap = count(mid, (40, 194, 55, 44), whiteish)
    assert lap > 15, f"no Daisy on the mode view mid-run ({lap} white px)"


@case
def daisy_lap_leaves_no_residue(tmp):
    """After the lap the strip must be rebuilt: bar, times, glyph, no dog."""
    def whiteish(p):
        r, g, b = p
        return r > 200 and g > 200 and b > 170
    px = run({"EMU_TRACK": "1", "EMU_FIRE": "like", "EMU_FIRE_MS": "400",
              "EMU_EXIT_MS": "2900"}, tmp("lap_done"))
    # Left of the glyph there is nothing bright in a healthy row; any white
    # here is a piece of dog.
    residue = count(px, (0, 228, 140, 12), whiteish)
    assert residue == 0, f"{residue} white pixels left behind after the lap"
    assert count(px, GLYPH, is_bright) > 20, "play glyph missing after the lap"
    assert region(px, LEFT_TIME) != [], "sanity"


@case
def liking_turns_the_heart_green(tmp):
    """Fixture 1 starts unliked, so this is a real transition."""
    # The like also launches Daisy's lap, which tramples the heart while she
    # crosses it; sample after the lap ends and the strip has rebuilt.
    px = run({"EMU_TRACK": "1", "EMU_FIRE": "like",
              "EMU_FIRE_MS": "400", "EMU_EXIT_MS": "2900"}, tmp("liked"))
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
def all_scenes_render_and_differ(tmp):
    """Each scene must draw something, and none may look like another."""
    frames = {}
    for n in range(3):
        px = run({"EMU_SCENE": str(n), "EMU_EXIT_MS": "2600"}, tmp(f"scene{n}"))
        lit = sum(1 for p in region(px, VIS) if sum(p) > 60)
        assert lit > 40, f"scene {n} rendered almost nothing ({lit} lit pixels)"
        frames[n] = region(px, VIS)

    for a in range(3):
        for b in range(a + 1, 3):
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


MODE_NAMES = ["classic", "pixel", "gameboy", "cyberdeck", "synthwave", "daisy", "snes", "nes"]


@case
def every_view_mode_renders_something_distinct(tmp):
    """All modes must draw, and none may be mistakable for another."""
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
def gameboy_mode_is_four_shades_of_green(tmp):
    """The whole view above the strip renders as the DMG LCD.

    This test has tracked the mode through three designs: a green wash, then
    a drawn handheld with a colour cover (which asserted body plastic and LCD
    colour), and now the full-screen LCD — so it asserts the filter: every
    pixel above the strip is green-dominant, and the dithered cover has real
    tonal range.
    """
    px = run({"EMU_MODE": "2", "EMU_EXIT_MS": "2400"}, tmp("gb"))

    # The green filter: nothing above the strip may be red- or blue-dominant.
    screen = region(px, (0, 0, 320, 190))
    offenders = sum(1 for (r, g, b) in screen if r > g + 12 or b > g + 12)
    assert offenders < 50, f"{offenders} non-green pixels on the DMG screen"

    # The dither carries the artwork: the art region must span the shade range.
    art = region(px, (8, 8, 176, 176))
    assert len(set(art)) >= 3, "cover flat; dither did not render"
    dark = sum(1 for (r, g, b) in art if g < 90)
    light = sum(1 for (r, g, b) in art if g > 150)
    assert dark > 500 and light > 500, \
        f"dither lacks tonal range (dark={dark}, light={light})"


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


@case
def battery_glyph_shows_the_level(tmp):
    """The strip battery must be present, and must read differently when low."""
    full = run({"EMU_BATTERY": "100", "EMU_EXIT_MS": "1200"}, tmp("bat_full"))
    low = run({"EMU_BATTERY": "25", "EMU_EXIT_MS": "1200"}, tmp("bat_low"))

    assert count(full, BATTERY, is_green) > 8, \
        "a full pack should read as green"
    assert count(low, BATTERY, is_amber) > 8, \
        "a low pack should read as a warning colour"
    assert region(full, BATTERY) != region(low, BATTERY), \
        "100% and 25% render identically"


@case
def battery_glyph_hidden_without_a_reading(tmp):
    """-1 means unknown, and a fake 0% would read as a flat battery."""
    none = run({"EMU_BATTERY": "-1", "EMU_EXIT_MS": "1200"}, tmp("bat_none"))
    some = run({"EMU_BATTERY": "75", "EMU_EXIT_MS": "1200"}, tmp("bat_some"))
    assert region(none, BATTERY) != region(some, BATTERY), \
        "an absent reading should not draw the same glyph as a real one"


@case
def every_view_shows_the_full_transport(tmp):
    """The point of the shared strip: play state, heart, bar, volume and
    battery must read identically on every view."""
    for n, name in enumerate(MODE_NAMES):
        a = run({"EMU_MODE": str(n), "EMU_TRACK": "0", "EMU_BATTERY": "50", "EMU_EXIT_MS": "1400"},
                tmp(f"tp_{name}_a"))
        b = run({"EMU_MODE": str(n), "EMU_TRACK": "0", "EMU_BATTERY": "50", "EMU_EXIT_MS": "2900"},
                tmp(f"tp_{name}_b"))

        assert count(a, GLYPH, is_bright) > 20, \
            f"{name}: play glyph missing"
        assert count(a, HEART, is_green) > 12, \
            f"{name}: liked track shows no green heart"
        vol_px = count(a, VOLUME, lambda p: sum(p) > 150)
        assert vol_px > 15, f"{name}: volume label missing ({vol_px} px)"
        assert count(a, BATTERY, is_amber) + count(a, BATTERY, is_green) > 6, \
            f"{name}: battery badge missing"
        assert region(a, BARROW) != region(b, BARROW), \
            f"{name}: progress bar frozen across 1.5s of playback"
        assert region(a, LEFT_TIME) != region(b, LEFT_TIME), \
            f"{name}: elapsed time frozen"


@case
def unliked_track_shows_hollow_heart_everywhere(tmp):
    """Fixture t2 is unliked: the heart must not read green on any view."""
    for n, name in enumerate(MODE_NAMES):
        px = run({"EMU_MODE": str(n), "EMU_TRACK": "1", "EMU_EXIT_MS": "1200"},
                 tmp(f"uh_{name}"))
        g = count(px, HEART, is_green)
        assert g == 0, f"{name}: {g} green px on an unliked track"


@case
def setup_portal_screen_renders(tmp):
    """EMU_PORTAL previews the captive-portal instructions and join QR."""
    px = run({"EMU_PORTAL": "1", "EMU_EXIT_MS": "900"}, tmp("portal"))
    # The QR sits on a white plate top-right; a valid one is roughly half
    # dark. The library's broken accessor drew ~5% — this bound catches that.
    box = (203, 38, 100, 100)
    white = count(px, box, lambda p: sum(p) > 600)
    dark = count(px, box, lambda p: sum(p) < 150)
    assert white > 2000, f"QR quiet plate missing ({white} white px)"
    assert dark > 1500, f"QR not drawn or sparse ({dark} dark px)"
    # Headline present
    assert count(px, (0, 0, 200, 36), is_green) > 40, "SETUP MODE headline missing"


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
