# M5Stack Spotify Desk Controller — Design

**Date:** 2026-08-05
**Status:** Approved design, ready for implementation planning

## Purpose

A permanently-plugged-in desk appliance that displays the album art and track
info for whatever is currently playing on Spotify, and controls playback with
three physical buttons. It is a standalone device: it talks to Spotify directly
over WiFi and works whether playback originates from the Mac, a phone, or any
other Spotify Connect device.

## Hardware

**M5Stack Core Basic v2.7** (Amazon B0CFXX1KG4). Specifications verified against
`docs.m5stack.com/en/core/basic_v2.7`:

| Property | Value |
|---|---|
| SoC | ESP32-D0WDQ6-V3, dual-core Xtensa LX6 @ 240MHz |
| SRAM | 520KB |
| PSRAM | **none** |
| Flash | 16MB |
| Display | 2.0" 320×240 ILI9342C IPS, max 853 nits |
| Buttons | 3 physical tactile (A, B, C) |
| Storage | TF/MicroSD slot, up to 16GB |
| Battery | 110mAh @ 3.7V |
| Power in | USB-C, 5V @ 500mA |
| Radio | 2.4GHz WiFi |

### Why this board

The three tactile buttons are the deciding factor. Core2 and CoreS3 offer 8MB of
PSRAM but replace the physical buttons with capacitive touch zones, which is the
wrong trade for a device whose purpose is eyes-free playback control. M5Dial has a
rotary encoder (genuinely attractive for volume) but a 1.28" *round* 240×240 panel,
which forces album art to be cropped or letterboxed, and it has no PSRAM either
(ESP32-S3FN8). The Core Basic's 320×240 rectangle is the correct aspect for
square art plus an information column.

### Consequences of no PSRAM

Usable heap once the WiFi and TLS stacks are up is roughly 150–200KB. Two hard
constraints follow, and both are designed around rather than fought:

1. **No full-frame buffering.** A decoded 320×240 RGB565 framebuffer is ~150KB and
   is not available. Album art is stream-decoded in MCU blocks directly to the
   panel, and any region that redraws frequently must be opaque so it never needs
   to composite against retained artwork.
2. **No naive JSON parsing.** See "The `available_markets` trap" below.

The MicroSD slot is the pressure-release valve: artwork is spooled to disk and
decoded from disk, never held whole in RAM, and the resulting on-disk cache makes
revisited albums render instantly with no network access.

### Operational notes

- Runs warm with the backlight on continuously. Use a real 5V charger, not a
  weak or shared hub port.
- The 110mAh battery living permanently at full charge is a slow wear item over
  a span of years. It does not affect the design; it is noted so it is not a
  surprise later.

## Scope

**v1 ships Spotify only.** Mac system vitals, clock/calendar, and dev/build status
were considered and deliberately deferred, but the architecture reserves a clean
path for them (see "Extensibility").

**Requires Spotify Premium** — confirmed present. The playback-control endpoints
(`play`, `pause`, `next`, `previous`, `volume`) return 403 on free accounts.

## Platform decisions

| Decision | Choice | Rationale |
|---|---|---|
| Build system | PlatformIO | Dependency pinning, a `native` test environment, CLI build/flash |
| Framework | Arduino-ESP32 | Required by M5Unified |
| Board library | M5Unified + M5GFX | Current generation; legacy `M5Stack.h` is deprecated |
| JPEG decode | M5GFX built-in (TJpgDec) | No separate decoder dependency needed |
| JSON | ArduinoJson with filtering | Filtering is mandatory, not optional — see below |
| Language | C++ | MicroPython/UIFlow would struggle with HTTPS + JPEG decode without PSRAM |

## Architecture

Standalone. The device holds a Spotify refresh token, mints access tokens itself,
polls the Spotify Web API, and pulls artwork from Spotify's CDN. Nothing runs on
the Mac. The device keeps working when the Mac is asleep or off.

The alternative considered and rejected was a Mac-side Python proxy with the
device as a thin display. It is meaningfully easier to build (no TLS or OAuth on
device, Mac pre-scales artwork, fast Python iteration, reliable system-volume
control) but makes the device dead whenever the Mac is, which defeats the point of
a desk object. It is retained as a documented fallback for the art pipeline only
(see "Risk").

### Module layout

```
src/
  main.cpp                 wiring only, no logic
  core/AppState.h          single source of truth, mutex-guarded
  core/CommandQueue.h      FreeRTOS queue, UI task -> net task
  core/Source.h            interface: poll(AppState&)
  config/secrets.h         gitignored
  config/secrets.h.example committed template
  net/WifiLink             connect, reconnect with backoff
  spotify/SpotifyAuth      refresh_token -> access_token, expiry tracking
  spotify/SpotifyClient    player GET, play/pause, next/prev, volume, like
  spotify/PlaybackState    plain struct, no behavior
  spotify/SpotifySource    implements Source
  art/ArtCache             SD: albumId -> /art/<id>.jpg, fetch-if-missing
  art/ArtRenderer          stream-decode JPEG from SD -> panel
  input/Buttons            tap / long-press / hold-repeat state machine
  ui/Screen.h              interface: draw(state), onButton(evt)
  ui/NowPlayingScreen      the primary screen
  ui/StatusScreen          boot, wifi, error, nothing-playing
  ui/ScreenManager         active screen, redraw, brightness and sleep policy
tools/
  get_refresh_token.py     one-time OAuth helper, run on the Mac
test/
  fixtures/                captured real Spotify JSON responses
  test_native/             host-side unit tests
```

### Extensibility

Two interfaces carry the entire extensibility story, and they are the one
structural decision that cannot be cheaply retrofitted:

- **`Source`** writes facts into `AppState`. v1 has one: `SpotifySource`.
- **`Screen`** renders `AppState` and handles button events. v1 has two:
  `NowPlayingScreen` and `StatusScreen`.

Adding Mac vitals later means writing one new `Source`, one new `Screen`, and
registering both. No Spotify code is touched. Screen cycling is not bound to a
button in v1 because there is only one content screen; `ScreenManager` supports it
so that binding it later is a one-line change.

### Task split across both cores

Networking and UI run as separate FreeRTOS tasks pinned to different cores. This
is not premature optimization: a TLS handshake to Spotify can block for over a
second, and if rendering shares that task the device feels broken every time it
refreshes a token — you press pause and nothing happens for a beat.

- **Core 0 — net task:** token refresh, player polling, artwork download,
  executing queued commands.
- **Core 1 — UI task:** button sampling, rendering, progress-bar ticks.

They communicate through a mutex-guarded `AppState` and a `CommandQueue`. **This
split is built in step 1, not added later** — retrofitting threading across
established module boundaries is the expensive refactor this design exists to
avoid.

## Data flow

### Token lifecycle

Spotify refresh tokens do not expire unless revoked. At boot the device POSTs
`grant_type=refresh_token` to `accounts.spotify.com/api/token` with HTTP Basic
auth built from the client ID and secret, receiving an access token and
`expires_in` (3600s).

Refresh is **proactive**, triggered 60 seconds before expiry rather than
reactively on a 401. A 401 is still handled by forcing one refresh and retrying
the request once, because clock drift happens.

### Polling

`GET /v1/me/player` — chosen over `/me/player/currently-playing` because it also
returns the active device and its volume percentage, both of which the UI needs.

Adaptive interval:

| State | Interval |
|---|---|
| Playing | 2s |
| Paused | 5s |
| Screen asleep | 10s |

At 2s this is 30 requests/minute, comfortably within Spotify's limits.

Fields extracted: `is_playing`, `progress_ms`, `item.id`, `item.name`,
`item.artists[0].name`, `item.duration_ms`, `item.album.id`,
`item.album.images[]`, `device.id`, `device.name`, `device.volume_percent`.

Any of these may be absent or null in a valid response — notably
`device.volume_percent` on devices that do not report volume, and the entire
`item` object when the player is in a transitional state. Absent fields render as
`--` (volume) or fall back to the `Nothing playing` state (`item`); they are never
treated as zero.

### The `available_markets` trap

**This is the single detail most likely to sink the build.** The `/me/player`
response embeds `available_markets` arrays on the track and album objects
containing hundreds of ISO country codes. Parsing the response naively will
exhaust the heap and hard-reset the device.

Mitigation is mandatory: ArduinoJson's `DeserializationOption::Filter` with a
filter document naming only the fields listed above. The parser then skips
everything else without ever allocating it. A host-side test asserts extraction
against a committed real-world fixture so a silently-dropped field is caught by
CI rather than by staring at the device.

### Liked state

`GET /me/tracks/contains?ids=<trackId>` determines whether the heart renders
filled or hollow. Issued on **track change only**, never per poll.

### Progress extrapolation

Between polls the UI task advances the progress bar locally from elapsed
`millis()`, resyncing to `progress_ms` on each poll, and only while `is_playing`.
The bar ticks every second even though the network is consulted every two.

### Album-scoped artwork refresh

Artwork changes are keyed on **album** ID, not track ID. Playing straight through
an album never refetches artwork.

### Artwork pipeline

All network work on the net task; no image is ever held whole in RAM.

```
album ID changed
      |
      v
  /art/<albumId>.jpg on SD?
      |
   yes|-------------------> signal UI: decode from SD -> panel
      |
    no
      v
  stream JPEG from i.scdn.co --> write directly to SD file --> signal UI
```

Cache eviction is deliberately **not implemented in v1**. At roughly 25KB per
album, filling a 16GB card requires north of half a million distinct albums.

### Optimistic UI

A button tap mutates `AppState` immediately and pushes a command onto the queue;
the screen updates before the network is consulted. This hides the 200–500ms API
round trip. Two mechanisms are required for it to behave:

- **Settle window (1.5s).** After an optimistic write, polls may not overwrite
  that specific field. Without this, a response already in flight snaps the pause
  icon back and the device looks broken.
- **Volume coalescing.** Holding a volume button steps the on-screen value every
  5%, but only one `PUT /me/player/volume` fires, 300ms after release. Per-step
  calls would spam the API and risk 429s.

On command failure the optimistic write is reverted and a toast is shown.

## Controls

Buttons A, B, C sit physically left, center, right, which maps directly onto
previous / play-pause / next. Long-press yields six actions from three buttons.

| Button | Tap | Long-press |
|---|---|---|
| A (left) | Previous track | Volume down, repeats while held |
| B (center) | Play / pause | Save to Liked Songs |
| C (right) | Next track | Volume up, repeats while held |

Long-press threshold 500ms; hold-repeat every 150ms after that.

`Save to Liked Songs` on the center button was chosen over a mute toggle because
it is the action that is genuinely annoying to perform with a mouse and is wanted
within seconds of hearing a track. Mute is reachable as volume-down-to-zero.

**The like action is a toggle**, not add-only: `PUT /me/tracks` when the current
track is not saved, `DELETE /me/tracks` when it already is. The heart glyph shows
which state a long-press will produce, so an accidental unlike is visible
immediately and reversible with a second long-press.

## Screen layout

320×240, all coordinates exact. A single compile-time `ART_SIZE` constant drives
the layout arithmetic so the artwork fallbacks below are a constant change rather
than a redesign.

```
+----------------------------------------+
|  +------------+    Song Title That     |   art:  (8, 8)   176x176
|  |            |    Wraps To Three      |   text: (192, 8) 120x176
|  |  album art |    Lines Max           |
|  |  176 x 176 |                        |
|  |            |    Artist Name         |
|  +------------+    <3          70%     |
+----------------------------------------+
|  ==================--------            |   strip: (0, 192) 320x48
|  1:47                          3:52    |   opaque
+----------------------------------------+
```

- **Artwork region** `(8, 8)` 176×176. Full square cover, never cropped.
- **Text column** `(192, 8)` 120×176. Title up to 3 lines then ellipsis, artist
  below in a dimmer tone, heart glyph and volume percentage at the column foot.
- **Bottom strip** `(0, 192)` 320×48, **opaque**. 3px progress bar at y=200
  spanning x=8..312; elapsed and total times at y=212. Toast messages replace the
  time row for 2 seconds. WiFi/status glyph at the strip's right edge.

The strip is opaque specifically because it redraws every second and cannot
composite against retained artwork on a board with no PSRAM.

## Brightness and sleep

The ESP32 itself never sleeps — it stays plugged in with WiFi up and polling.
Only the backlight sleeps.

| Condition | Brightness | Poll |
|---|---|---|
| Interacting, or track just changed | 180/255 | 2s |
| 30s without interaction, still playing | 60/255 | 2s |
| 3 minutes of nothing playing | off (0) | 10s |
| Any button press | 180/255 immediately | 2s |

Default brightness is 180 rather than 255 because 853 nits at desk distance is
unpleasant. There is no burn-in risk — the panel is IPS LCD, not OLED — so a
static image is safe; dimming is purely about not lighting the room.

**The button press that wakes the screen is swallowed and does not trigger its
action**, matching phone behavior. Otherwise tapping to check what's playing
accidentally skips the track.

## Error handling

Every failure degrades to something readable. No blank screens, no reboot loops.

| Failure | Behavior |
|---|---|
| WiFi down at boot | `Connecting…`; exponential backoff capped at 30s; UI never blocks |
| WiFi drops mid-run | Last known state retained and greyed, wifi-off glyph, auto-reconnect |
| Token refresh fails (network) | Retry with backoff; `Auth error` after 3 consecutive failures |
| Token refresh returns `invalid_grant` | **Terminal.** Access was revoked. `Re-auth needed`; retry only every 5 minutes |
| `204 No Content` from `/me/player` | `Nothing playing`; drop to slow poll |
| `404 NO_ACTIVE_DEVICE` on a command | Toast `No active device`. The most common real-world failure |
| `403` on volume | Toast `Volume not supported` |
| `429 Too Many Requests` | Honor the `Retry-After` response header |
| No SD card, or SD write fails | Text-only mode with flat color block in the art region; never bricks |
| JPEG decode fails | Delete the cached file, show flat block, retry on next album change |
| Free heap below threshold | Skip artwork fetch and run text-only rather than crash |

## Risk: fractional artwork scaling

Spotify serves cover art at 640, 300, and 64px. The target is 176×176, and
300→176 is not a power-of-two downscale, so it requires fractional scaling during
JPEG decode on a board with no PSRAM. M5GFX is expected to support this, but the
design must not rest on that assumption.

**Step 0 of implementation is a throwaway spike** that does nothing but fetch one
hardcoded album art URL and render it at 176×176. It either proves or kills this
question in the first hour rather than the last.

Ordered fallbacks, all of which preserve the rest of the design because layout is
parameterized by `ART_SIZE`:

1. **640px source at exactly 1/4** → 160×160. Integer downscale, guaranteed by
   TJpgDec. Larger download (~100KB) but it streams to SD, so RAM is unaffected.
2. **300px source at exactly 1/2** → 150×150. Integer downscale, guaranteed.
3. **Mac pre-scales.** Abandons standalone operation and is a last resort, taken
   only if on-device decode fails entirely.

## Testing

The bug-prone parts of this system are pure logic and are tested on the Mac with
no hardware involved, via PlatformIO's `native` environment:

- **JSON extraction** against committed fixtures of real Spotify responses.
  Captured once with curl; the access token is never committed. This is where the
  filter either works or silently drops a field.
- **Button state machine** — tap vs. long-press vs. hold-repeat, driven by
  synthetic time and pin-state sequences.
- **Settle window and optimistic state transitions** — the flicker bug class,
  proven by test rather than by squinting at the device.
- **Poll interval selection** and **progress extrapolation** arithmetic.

Hardware verification is a short manual smoke checklist plus a serial `selftest`
command that exercises the artwork pipeline against a fixed URL.

## Secrets

A gitignored `src/config/secrets.h`, with `secrets.h.example` committed as a
template. Contains WiFi SSID and password, Spotify client ID and secret, and the
refresh token. Changing networks means a reflash, which on a stationary desk
device is expected to happen approximately never.

The device firmware image therefore contains the refresh token. This is
acceptable for a personal desk device and is noted so the trade is explicit.

### One-time setup, performed by hand

1. Register an application at `developer.spotify.com`.
2. Set the redirect URI to `http://127.0.0.1:8888/callback`. Spotify tightened
   its redirect-URI rules and no longer accepts `http://localhost`; the explicit
   loopback IP is required. **Verify against current Spotify documentation at
   implementation time rather than trusting this note.**
3. Run `tools/get_refresh_token.py`, which performs the authorization-code
   exchange and prints the refresh token to paste into `secrets.h`.

Scopes required: `user-read-playback-state`, `user-modify-playback-state`,
`user-read-currently-playing`, `user-library-read`, `user-library-modify`.

## Build order

0. **Spike** — one hardcoded artwork URL on screen at 176×176. Throwaway code.
   Resolves the scaling risk above.
1. **Skeleton** — PlatformIO project, M5Unified, WiFi connect, `StatusScreen`,
   and the two-core task split with `AppState` and `CommandQueue`.
2. **Auth** — refresh token to access token, verified over serial.
3. **Poll and parse** — filtered JSON, host tests against fixtures, text-only
   now-playing render.
4. **Artwork pipeline** — real `ArtCache` and `ArtRenderer` with SD caching.
5. **Input** — button state machine with host tests, optimistic UI, command queue.
6. **Commands** — play/pause, next, previous, volume, like.
7. **Brightness and sleep policy.**

## Out of scope for v1

Recorded so these are decisions rather than omissions:

- Mac system vitals, clock/calendar, dev and build status screens
- Screen cycling bound to a button
- Artwork cache eviction
- On-device WiFi setup portal or on-device OAuth
- Shuffle and repeat controls
- Playlist or queue browsing
- Speaker output of any kind
