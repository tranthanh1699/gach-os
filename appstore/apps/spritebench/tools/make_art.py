#!/usr/bin/env python3
"""Sinh img/orbs.png: sprite sheet 4 khung 32x32 (quả cầu nhịp sáng), hình học thuần."""
import math
import struct
import zlib
from pathlib import Path

FRAME, FRAMES = 32, 4


def write_png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(c for x in range(w) for c in px(x, y)) for y in range(h))

    def chunk(k, d):
        return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def pixel(x, y):
    f = x // FRAME
    lx, ly = x % FRAME + 0.5, y + 0.5
    d = math.hypot(lx - FRAME / 2, ly - FRAME / 2) / (FRAME / 2 - 1)
    if d > 1:
        return (0, 0, 0, 0)
    glow = 0.6 + 0.4 * math.sin(f * math.pi / 2)
    hl = max(0.0, 1 - math.hypot(lx - 11, ly - 10) / 14)
    return (int(min(255, 255 * glow * 0.9 + 60 * hl)), int(min(255, 120 * glow + 120 * hl)),
            int(min(255, 40 + 160 * hl)), int(255 * min(1.0, (1 - d) * 5)))


if __name__ == "__main__":
    out = Path(__file__).resolve().parent.parent / "img" / "orbs.png"
    write_png(out, FRAME * FRAMES, FRAME, pixel)
    print(out)
