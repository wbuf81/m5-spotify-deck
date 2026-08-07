# Adding a view

A view is a full-screen presentation of the current track. Eight ship with
the firmware; this is the contract for a ninth.

## The interface

Subclass `ViewMode` (`src/ui/modes/ViewMode.h`):

```cpp
class MyMode : public ViewMode {
 public:
  const char *name() const override { return "mymode"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;  // full repaint
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;
  void release() override {}  // hand back any sprite memory
};
```

- `enter()` repaints everything that only changes with the track. It runs on
  a mode switch, a track change, a palette change, and when late-arriving
  album art lands. Budget: it blocks the UI, so stay under ~300ms.
- `tick()` runs every frame. Touch only what moved.
- `ctx.tint` is the album's sampled colour; `ctx.art_path` a local JPEG path
  (may be empty — always have a fallback).

## The contracts (each learned the hard way)

1. **The bottom 48px are not yours.** `y >= theme::STRIP_Y` (192) belongs to
   the shared StatusStrip: progress, timecodes, play glyph, heart, battery,
   volume, toasts. Don't draw there; don't duplicate its elements.
2. **There is no framebuffer.** No PSRAM; a 320×240 buffer does not exist.
   Draw directly, repaint only what changed, and never clear a region you are
   about to overwrite — panels present mid-draw, so clear-then-redraw blinks.
3. **Decode art into a small sprite, never per frame.** `drawArtInto()` a
   64–90px canvas in `enter()`, then sample it. A JPEG decode is ~200ms.
4. **Never touch SD inside `startWrite()`.** The LCD and SD share one SPI
   bus; this deadlocks the UI thread on hardware (invisible in the emulator).
5. **Sprite buffers store RGB565 byte-swapped.** Raw copies between buffers
   are consistent; any *arithmetic* on texels (fog, blending, averaging,
   quantising) must unswap first and re-swap after, or you paint rainbows.
6. **Drive animation from a play-gated clock**, not wall time:
   `if (st.pb.is_playing) clock_ += dt;` — then everything freezes on pause
   for free, which is the house style.
7. **Respect `theme::dimFactor()`** in your colours so the view dims with the
   screen.

## Registration checklist

1. `src/ui/modes/MyMode.{h,cpp}` — the mode.
2. `src/ui/ViewManager.h` — include + member.
3. `src/ui/ViewManager.cpp` — one line in `selectFor()`'s registration block;
   bump `MODE_COUNT` in the header.
4. `src/platform/native/Harness.cpp` — add the name to `MODE_NAMES` and
   extend the number-key array.
5. `src/platform/esp32/Esp32Portal.cpp` — add the name to `VIEW_NAMES` so the
   portal's enable/disable checkbox exists (order must match ViewManager).
6. `tools/visual_tests.py` — append to `MODE_NAMES`. The per-view transport
   matrix and the distinctness test then cover your mode automatically.

## Verifying

```sh
pio run -e native && EMU_FAKE=1 EMU_MODE=<n> ./.pio/build/native/program
python3 tools/visual_tests.py
pio run -e esp32          # it must also fit and compile for the device
```

Watch the serial `view enter <name>: NNNms` line on hardware for your
enter() budget, and the `[heap]` lines for anything you leaked.
