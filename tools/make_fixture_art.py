#!/usr/bin/env python3
"""Generate fixture album covers for the emulator.

Writes 300x300 PNGs (the size Spotify's mid-resolution CDN image uses), which
the caller converts to JPEG with sips. Deliberately dependency-free: a minimal
PNG encoder over zlib beats requiring Pillow for five placeholder images.
"""

import math
import struct
import sys
import zlib
from pathlib import Path

SIZE = 300


def write_png(path: Path, w: int, h: int, rgb: bytearray) -> None:
    raw = b"".join(
        b"\x00" + bytes(rgb[y * w * 3 : (y + 1) * w * 3]) for y in range(h)
    )

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return (
            struct.pack(">I", len(data))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def blank() -> bytearray:
    return bytearray(SIZE * SIZE * 3)


def put(buf: bytearray, x: int, y: int, r: int, g: int, b: int) -> None:
    if 0 <= x < SIZE and 0 <= y < SIZE:
        i = (y * SIZE + x) * 3
        buf[i] = max(0, min(255, r))
        buf[i + 1] = max(0, min(255, g))
        buf[i + 2] = max(0, min(255, b))


def cover_gradient_circle() -> bytearray:
    buf = blank()
    for y in range(SIZE):
        t = y / SIZE
        for x in range(SIZE):
            put(buf, x, y, int(12 + 30 * t), int(20 + 90 * t), int(70 + 150 * t))
    cx, cy, rad = 195, 105, 62
    for y in range(cy - rad, cy + rad + 1):
        for x in range(cx - rad, cx + rad + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad:
                put(buf, x, y, 245, 240, 225)
    return buf


def cover_rings() -> bytearray:
    buf = blank()
    for y in range(SIZE):
        for x in range(SIZE):
            put(buf, x, y, 14, 12, 12)
    cx = cy = SIZE // 2
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - cx, y - cy)
            if int(d) % 34 < 13 and d < 140:
                f = 1.0 - d / 150.0
                put(buf, x, y, int(255 * f), int(120 * f), int(30 * f))
    return buf


def cover_triangle() -> bytearray:
    buf = blank()
    for y in range(SIZE):
        for x in range(SIZE):
            put(buf, x, y, 236, 230, 214)
    apex_x, apex_y, base_y = SIZE // 2, 48, 250
    for y in range(apex_y, base_y):
        half = int((y - apex_y) * 0.86)
        for x in range(apex_x - half, apex_x + half):
            put(buf, x, y, 198, 24, 108)
    return buf


def cover_stripes() -> bytearray:
    buf = blank()
    for y in range(SIZE):
        for x in range(SIZE):
            band = ((x + y) // 26) % 2
            if band:
                put(buf, x, y, 18, 74, 56)
            else:
                put(buf, x, y, 226, 214, 120)
    return buf


def cover_radial() -> bytearray:
    buf = blank()
    cx, cy = 120, 170
    for y in range(SIZE):
        for x in range(SIZE):
            d = min(1.0, math.hypot(x - cx, y - cy) / 260.0)
            put(
                buf,
                x,
                y,
                int(250 - 150 * d),
                int(60 + 30 * d),
                int(170 - 60 * d),
            )
    return buf


COVERS = {
    "aurora": cover_gradient_circle,
    "rings": cover_rings,
    "monolith": cover_triangle,
    "meridian": cover_stripes,
    "duskpop": cover_radial,
}


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/art")
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in COVERS.items():
        path = out / f"{name}.png"
        write_png(path, SIZE, SIZE, fn())
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
