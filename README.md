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

### Review harness

```sh
./tools/harness.sh          # fixture tracks, offline, deterministic
./tools/harness.sh --live   # your real Spotify
```

Every state and animation on demand, rather than waiting for a track to end or
for the network to fail on its own. Click the window first or the keys go to
your terminal.

| Key | |
|---|---|
| `1`..`7` | Pin a view: classic, pixel, gameboy, cassette, scoreboard, cyberdeck, synthwave |
| `0` | Unpin — rotate per track again |
| `[` `]` | Previous / next fixture track |
| `,` `.` | Scrub ±10s | 
| `m` | Jump to near the end |
| `p` | Play / pause (fires the glyph morph) |
| `f` | Toggle like (fires the heart animation) |
| `v` `b` | Volume ∓5 |
| `t` | Fire a toast |
| `n` | Cycle link state: auto → connecting → offline → auth error → re-auth |
| `k` | Toggle "nothing playing" |
| `d` | Cycle brightness: auto → active → idle → off |
| `c` | Toggle CRT scanlines |
| `h` | Hide the status bar |
| `q` | Quit |

`Panel_sdl` binds plain `1`–`6` to window zoom and `r`/`l` to 90-degree
rotation, with no modifier required. Those collided with the harness keys, so
picking a view also resized the window. `setShortcutKeymod(KMOD_LCTRL)` moves
them behind Ctrl — `Ctrl`+`1`–`6` still zooms the window if you want it.

A bar across the top shows the active mode and last action — `~` means
rotating, `*` means pinned. It deliberately covers the top ten rows of whatever
is showing; knowing what is pinned matters more than those pixels.

Scrubbing is the useful part for anything progress-driven: the cassette reels,
the sinking synthwave sun, the scoreboard clock. Fixtures are the default
because reviewing states should not depend on what happens to be playing, and
scrubbing against a live player fights the next poll.

The harness is `EMU_HARNESS`-gated and entirely inside `#if defined(EMULATOR)`;
the device build is byte-identical without it.

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
| `EMU_MODE=<0-6>` | Pin one full-screen view (0 = classic) instead of rotating |
| `EMU_DIM_AFTER_MS=<ms>` | Shorten the 30s dim timer, so dimming is testable |
| `EMU_SLEEP_AFTER_MS=<ms>` | Shorten the sleep timer likewise |
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

## View modes

Seven full-screen presentations rotate, one per track, chosen the same way the
ambient scenes are: hashed from the track ID, skipping whatever played last.

| Mode | What it is | What is real |
|---|---|---|
| `classic` | Artwork, text column, ambient scene panel | everything below |
| `pixel` | Cover posterized to 8px cells in an RGB332 palette | the cover itself |
| `gameboy` | Four DMG greens, ordered-dithered | luminance of the cover |
| `cassette` | Tape deck whose reels transfer | reel radius **is** the progress |
| `scoreboard` | Stadium LED board, Gators or Jaguars palette | elapsed as a game clock |
| `cyberdeck` | Phosphor terminal with scanlines | most legible of the set |
| `synthwave` | Mode-7 horizon, cover masked into the sun | the sun sinks with progress |

Modes cannot composite off-screen — a 320×240 RGB565 buffer is 150KB and this
board has no PSRAM — so each draws straight to the panel and splits into
`enter()` (repaint on track change) and `tick()` (only the moving parts). A full
blit costs ~30ms over SPI, which would otherwise cap the frame rate and
saturate the bus.

Scoreboard uses team colours and a generic board rather than club marks: logos
at this resolution read as mush, and the palette carries the association anyway.

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

## First boot

### Before you plug it in

- **A MicroSD card, FAT32, 16GB or smaller.** Album art is cached on it, and
  artwork is most of the point. Without one the device runs fine and says
  `no sd card` in place of the cover. Insert it before powering on.
- **Your WiFi must have a 2.4GHz band.** The ESP32 cannot see 5GHz at all. If
  your router publishes one SSID for both bands this usually still works; if
  5GHz-only, it will sit on `CONNECTING` forever. This is the single most
  common first-boot failure.
- **A USB-C data cable.** Charge-only cables are common and will power the
  device while leaving no serial port to flash.
- `src/config/secrets.h` must have `WIFI_SSID` and `WIFI_PASSWORD` filled in,
  not just the Spotify values.

### Flash it

```sh
pio run -e esp32 -t upload && pio device monitor
```

If no port is found, check the USB-serial driver — M5Stack Core uses a CP210x
or CH9102. Recent macOS has both built in; a charge-only cable presents no port
at all, which looks the same.

### What you should see

```
=== m5 spotify ===
chip      : ESP32-D0WDQ6 rev3, 2 core(s) @ 240MHz
psram     : none (expected)
heap free : ~250000 bytes
sd card   : mounted
==================
[net] net task started
[net] wifi connecting to <ssid>
[net] wifi connected, ip=..., rssi=-xx
[net] token refresh: HTTP 200
[net] GET https://api.spotify.com/v1/me/player -> 200
```

On screen: the `CONNECTING` beacon, then the now-playing layout once something
is actually playing on Spotify.

### If it misbehaves

| Symptom | Likely cause |
|---|---|
| Stuck on `CONNECTING` | 5GHz-only network, or wrong password. Serial shows the retry backoff |
| `OFFLINE` with a slash | Association failed repeatedly — check `rssi`, the device may be too far |
| `RE-AUTH` | Refresh token revoked. Re-run `tools/get_refresh_token.py` |
| `NO DEVICE` | Nothing has an active Spotify session. Start playback anywhere |
| `no sd card` where art should be | Card absent or not FAT32 |
| Reboot loop | Watch `[heap]` lines before the reset. A stack overflow inside mbedTLS means raising `NET_TASK_STACK` in `NetWorker.cpp` |
| Art appears then stalls | JPEG decode too slow. Time it before assuming otherwise |
| SD mount fails intermittently | Drop the 25MHz clock in `Esp32Storage.cpp` |

### Still unknown until it runs

Everything above is reasoned from datasheets and host measurements. Three
things genuinely cannot be known until the board is on a desk: **JPEG decode
speed**, **heap headroom under a live TLS session**, and **SD reliability at
25MHz**. The boot banner and the 30-second `[heap]` lines exist to answer the
middle one quickly.

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
