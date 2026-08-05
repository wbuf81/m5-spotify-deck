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
| `EMU_TRACK=<n>` | Start on fixture *n* (0–4) |
| `EMU_DUMP=<path>` | Write the framebuffer as a 24-bit BMP |
| `EMU_EXIT_AFTER=<n>` | Quit after *n* frames — makes captures deterministic |

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

![accents](docs/screenshots/accented-text.png)

Accented text, which is why the fonts are the Unicode `lgfxJapanGothicP` faces
rather than Adafruit's ASCII-only FreeSans.

## Building for hardware

```sh
pio run -e esp32 -t upload
```

Not yet functional: the device build still needs `SpotifySource` (real Web API),
`WifiLink`, `SpotifyAuth`, the SD artwork cache, and `src/config/secrets.h`.
See the spec's build order.

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
