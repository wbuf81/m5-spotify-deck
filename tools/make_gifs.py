#!/usr/bin/env python3
"""Build the README's animated GIFs from emulator frame sequences.

Each view runs ONCE with EMU_DUMP_EVERY_MS, so its animation clocks are
coherent across frames; the frames are then assembled with Pillow. Needs
Pillow and a built native binary.

  python3 tools/make_gifs.py [outdir]   (default docs/screenshots)
"""

import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, ".pio", "build", "native", "program")

# name -> (env, every_ms, count, trim_leading, frame_ms)
#
# trim_leading drops enter()-blank frames; frame_ms is the GIF's own timing,
# independent of capture spacing so slow sweeps can play back tighter.
SHOTS = {
    "snes": ({"EMU_MODE": "6", "EMU_TRACK": "3"}, 120, 26, 3, 100),
    "gameboy": ({"EMU_MODE": "2"}, 120, 18, 0, 120),
    "daisy": ({"EMU_MODE": "5"}, 120, 22, 1, 110),
    "nes": ({"EMU_MODE": "7"}, 150, 20, 2, 130),
    "synthwave": ({"EMU_MODE": "4"}, 120, 22, 1, 110),
    "classic-like": (
        {"EMU_MODE": "0", "EMU_TRACK": "1", "EMU_FIRE": "like",
         "EMU_FIRE_MS": "600"}, 120, 24, 2, 110),
    "pixel": ({"EMU_MODE": "1"}, 250, 22, 2, 140),
    "cyberdeck": (
        {"EMU_MODE": "3", "EMU_FIRE": "playpause", "EMU_FIRE_MS": "1100"},
        250, 18, 1, 180),
}


def capture(name, env_extra, every, count, tmpdir):
    prefix = os.path.join(tmpdir, name + "_")
    env = dict(os.environ)
    env.update({"EMU_FAKE": "1", "EMU_DUMP": prefix,
                "EMU_DUMP_EVERY_MS": str(every),
                "EMU_DUMP_COUNT": str(count)})
    env.update(env_extra)
    # Three attempts around the known Panel_sdl startup race.
    for _ in range(3):
        r = subprocess.run([BIN], env=env, capture_output=True, timeout=120)
        frames = sorted(
            f for f in os.listdir(tmpdir) if f.startswith(name + "_"))
        if r.returncode == 0 and len(frames) == count:
            return [os.path.join(tmpdir, f) for f in frames]
        for f in frames:
            os.remove(os.path.join(tmpdir, f))
    raise RuntimeError(f"{name}: capture failed")


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "docs", "screenshots")
    os.makedirs(outdir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        for name, (env, every, count, trim, frame_ms) in SHOTS.items():
            paths = capture(name, env, every, count, tmpdir)[trim:]
            frames = [Image.open(p).convert("RGB").quantize(
                colors=128, dither=Image.Dither.NONE) for p in paths]
            out = os.path.join(outdir, f"{name}.gif")
            frames[0].save(out, save_all=True, append_images=frames[1:],
                           duration=frame_ms, loop=0, optimize=True)
            kb = os.path.getsize(out) // 1024
            print(f"{name:14s} {len(frames)} frames  {kb}KB  -> {out}")


if __name__ == "__main__":
    main()
