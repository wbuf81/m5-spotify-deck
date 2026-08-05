# M5Stack Spotify Desk Controller

A permanently-plugged-in desk appliance showing Spotify album art and now-playing
info, with three physical buttons for playback control.

Target hardware: **M5Stack Core Basic v2.7** (ESP32, 320×240, three tactile
buttons). Not yet purchased — the emulator below exists so the UI can be judged
first.

Design spec: [`docs/superpowers/specs/2026-08-05-m5stack-spotify-controller-design.md`](docs/superpowers/specs/2026-08-05-m5stack-spotify-controller-design.md)

## Running the emulator

The emulator is not a mockup. It is the **same firmware source** compiled for
your Mac, rendering through SDL at the real 320×240, so what you see is what the
panel will show.

```sh
brew install platformio sdl2 pkg-config     # one time
export HOMEBREW_PREFIX=/opt/homebrew        # Apple Silicon

pio run -e native && ./.pio/build/native/program
```

### Controls

| Key | Button | Tap | Hold |
|---|---|---|---|
| `←` or `A` | A | Previous track | Volume down (repeats) |
| `Space` or `S` | B | Play / pause | Save to Liked Songs |
| `→` or `D` | C | Next track | Volume up (repeats) |

The emulator plays through five fixture tracks with real JPEG cover art. It
deliberately reproduces the awkward parts of the real thing: commands take effect
after a simulated 250ms API round trip, state is only published every 2 seconds,
and the progress bar is extrapolated locally in between.

Leave it alone for 30 seconds to watch it dim; the sleep timer needs 3 minutes of
nothing playing.

### Environment hooks

| Variable | Effect |
|---|---|
| `EMU_FAKE=1` | Use offline fixtures instead of live Spotify |
| `EMU_TRACK=<n>` | Start on fixture *n* (0–4) |
| `EMU_EXIT_MS=<ms>` | Quit after wall-clock ms — use this when waiting on the network |
| `SPOTIFY_DEBUG=1` | Log HTTP status codes and API errors (never tokens) |
| `SPOTIFY_DIAG=1` | Probe several endpoints once and log their statuses |
| `EMU_TOAST=<text>` | Raise a toast at startup, to inspect the toast row without a keypress |
| `EMU_FIRE=<like\|unlike\|playpause>` | Fire a user action at a known time, to sample animations |
| `EMU_FIRE_MS=<ms>` | When `EMU_FIRE` triggers (default 500) |
| `EMU_LINK=<connecting\|offline\|autherror\|reauth\|notrack>` | Force a link state to inspect the status screen |
| `EMU_SCENE=<0-3>` | Pin one ambient scene instead of rotating |
| `EMU_DIM_AFTER_MS=<ms>` | Shorten the 30s dim timer, so dimming is testable |
| `EMU_DUMP=<path>` | Write the framebuffer as a 24-bit BMP |
| `EMU_EXIT_AFTER=<n>` | Quit after *n* frames. Note `loop()` runs as fast as SDL allows, so this is **not** a wall-clock proxy — prefer `EMU_EXIT_MS` when waiting on the network |

```sh
# Capture a specific case as a PNG
EMU_TRACK=2 EMU_DUMP=/tmp/f.bmp EMU_EXIT_AFTER=90 ./.pio/build/native/program
sips -s format png /tmp/f.bmp --out /tmp/f.png
```

## Screenshots

Captured from the emulator at native 320×240.

| | |
|---|---|
| ![now playing](docs/screenshots/now-playing.png) | ![truncation](docs/screenshots/long-title-truncation.png) |
| Normal playback | A long title truncating |

![offline](docs/screenshots/offline.png)

The status screen. `CONNECTING` is accent-coloured with a clean core; faults
are amber with a slash through it, so a normal wait and a real problem are
distinguishable without relying on colour alone.

![accents](docs/screenshots/accented-text.png)

Accented text, which is why the fonts are the Unicode `lgfxJapanGothicP` faces
rather than Adafruit's ASCII-only FreeSans.

## Known issue: emulator startup crash

M5GFX's SDL backend has an unsynchronised data race. `Panel_sdl::init()` does
`_list_monitor.push_back()` on the app thread inside `M5.begin()`, while the SDL
thread is already iterating that same `std::list` in `_update_proc()`. No mutex
guards it, so roughly 1 launch in 80 segfaults during startup. Relaunch it.

Upstream bug, and **emulator-only** — the device build contains no `Panel_sdl`.
The visual test harness retries a crashed run once and reports how often that
happened, so the flakiness stays visible; a crash that reproduces twice still
fails the suite.

## Tests

```sh
./tools/run_tests.sh
```

Two layers, because they catch different things:

- **`pio test -e test`** — 22 host unit tests over the display-free logic:
  button state machine, command coalescing, the progress clock, the merge
  policy's settle windows, time formatting. No SDL, no M5GFX, runs in seconds.
- **`tools/visual_tests.py`** — 11 checks that run the emulator headlessly and
  assert on real framebuffer pixels.

The visual layer is not optional. Every rendering bug this project has hit was
invisible to logic tests and obvious in a screenshot: toast text ghosting under
the timecodes, the play glyph vanishing after a like, the clock stuttering in
two-second steps. Each of those now has a test named after the symptom.

No golden images — they rot whenever a colour or font changes. The checks
assert properties that should hold regardless ("no amber pixels remain in the
info row after a toast expires").

## Ambient scenes

Four scenes rotate under the song info, one per track. Which one you get is a
hash of the track ID, skipping whatever played last — so it spreads evenly, a
song always gets "its" scene, you never see the same one twice running, and the
tests can assert on it.

| Scene | What it is | What is real |
|---|---|---|
| `synthwave` | Mode-7 grid, Outrun sun, stars | Sun sinks as the track plays |
| `starfield` | Perspective hyperspace streaks | Speed follows real volume |
| `city` | Neon skyline, flickering windows, parallax | Album-tinted skyglow |
| `planet` | Banded planet with an orbiting moon | Moon does exactly one orbit per track |

All four are tinted from the album art and freeze when playback pauses.

These replaced a spectrum-bar visualiser, which was a mistake worth recording:
**bar shapes promise beat-sync no matter how they move.** Smooth motion did not
help, because the form itself was making the claim.

Beat-sync is not available at any price here. The device never sees the audio,
the Core Basic has no microphone, and Spotify's `/audio-features` and
`/audio-analysis` both return **403** for this app tier — verified, not assumed.
Atmosphere makes no such promise, so it can be honest.

The device never touches the audio stream, the Core Basic has no microphone,
and Spotify's `/audio-features` and `/audio-analysis` both return **403** for
this app tier — verified, not assumed. There is no tempo, energy or spectrum
available at any price, so nothing here can react to the music.

## Live Spotify

With `src/config/secrets.h` present the emulator uses the real Web API; without
it, offline fixtures. `EMU_FAKE=1` forces fixtures either way.

```sh
python3 tools/get_refresh_token.py    # in a REAL terminal, not an agent session
```

Networking runs on its own thread, mirroring the design's core-0 net task — a
Spotify call takes 200-500ms and must never block rendering.

**Development Mode apps must use the post-February-2026 library endpoints**
(`/me/library`, `/me/library/contains`, addressed by Spotify URI). The old
`/me/tracks` equivalents are deprecated and answer 403 with no explanation,
which is indistinguishable from a scope problem. See the spec for the full
diagnosis. Note also that the app owner needs active Premium or the app stops
working entirely.

## Building for hardware

```sh
pio run -e esp32 -t upload && pio device monitor
```

Requires `src/config/secrets.h` with WiFi credentials filled in (the OAuth
helper prompts for them).

**Untested on real hardware — no device yet.** It compiles, links and fits, but
nothing below has been observed running on a board.

```
RAM    15.8%   51,812 / 327,680 static
Flash  40.2%   1,263,409 / 3,145,728
```

The default partition table gives the app only 1.31MB, and WiFi + mbedTLS + the
root CA bundle alone fill 96% of that, so `board_build.partitions = huge_app.csv`
claims 3MB of the board's 16MB flash. OTA is not needed for a tethered device.

TLS validates certificates against the ESP-IDF root CA bundle. `setInsecure()`
would be one line shorter and would send the Spotify client secret and refresh
token over a connection anyone on the network could impersonate.

The net task is a real FreeRTOS task pinned to **core 0** with a 16KB stack —
`std::thread` cannot pin a core or size a stack, and an mbedTLS handshake
overflows the pthread default. Rendering stays on core 1 with the Arduino loop.

Storage needed no porting: Arduino's SD library mounts at `/sd` through the
ESP-IDF VFS, so the same `fopen`/`mkdir` code serves both platforms.

### What to expect on first flash

Unknown, honestly. The things most likely to bite, in order:

1. **JPEG decode speed and heap headroom.** Only measurable on the board. The
   boot banner and periodic `[heap]` lines over serial are there to answer it.
2. **SD card behaviour** at 25MHz — drop the clock in `Esp32Storage.cpp` if
   mounts are flaky.
3. **TLS handshake stack.** The net task has 16KB; if it panics with a stack
   overflow inside mbedTLS, raise `NET_TASK_STACK`.

The panel-readback risk is gone: `sampleArtTint` now decodes a thumbnail into
an off-screen sprite and samples that, so the ILI9342C is never read back.

Serial output on boot reports chip, flash, PSRAM, free heap, largest contiguous
block, SD status and display size, then free heap every 30s — and shouts once if
it drops below the level where TLS starts failing.

## Layout

Geometry lives in `src/ui/Theme.h`. `ART_SIZE` drives every other coordinate, so
the spec's 160/150 artwork fallbacks are a one-constant change.

```
+----------------------------------------+
|  +------------+    Song Title That     |   art:   (8, 8)   176x176
|  |            |    Wraps To Three      |   text:  (192, 8) 120x176
|  |  album art |    Lines Max           |
|  |  176 x 176 |                        |
|  |            |    Artist Name         |
|  +------------+    <3          70%     |
+----------------------------------------+
|  ==================--------            |   strip: (0, 192) 320x48
|  1:47                          3:52    |   opaque
+----------------------------------------+
```

## Regenerating fixture album art

```sh
python3 tools/make_fixture_art.py assets/art
for f in assets/art/*.png; do
  sips -s format jpeg -s formatOptions 82 "$f" --out "${f%.png}.jpg"
done
rm -f assets/art/*.png
```

Dependency-free by design: a minimal PNG encoder over `zlib`, then macOS `sips`
for JPEG, rather than requiring Pillow for five placeholder images.
