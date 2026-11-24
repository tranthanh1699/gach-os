#!/usr/bin/env python3
"""Sinh ảnh mẫu cho trang Images của UI Kit (hình học thuần, nguyên bản): img/orb.png, img/badge.png."""
import math
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "img"


def write_png(path: Path, w: int, h: int, pixels) -> None:
    raw = b"".join(b"\x00" + bytes(c for x in range(w) for c in pixels(x, y)) for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def orb(x, y, size=96):
    """Quả cầu phát sáng: tâm sáng, viền mờ dần (alpha mềm)."""
    cx = cy = size / 2
    d = math.hypot(x + 0.5 - cx, y + 0.5 - cy) / (size / 2)
    if d > 1:
        return (0, 0, 0, 0)
    hl = max(0.0, 1 - math.hypot(x - size * 0.35, y - size * 0.32) / (size * 0.45))
    r = int(60 + 150 * hl)
    g = int(120 + 120 * hl)
    b = int(220 + 35 * hl)
    a = int(255 * min(1.0, (1 - d) * 6))
    return (min(r, 255), min(g, 255), min(b, 255), a)


def badge(x, y, size=96):
    """Huy hiệu lục giác cam với ngôi sao 5 cánh trắng."""
    cx = cy = size / 2
    px, py = x + 0.5 - cx, y + 0.5 - cy
    ang = math.atan2(py, px)
    rad = math.hypot(px, py)
    hexr = (size * 0.46) * math.cos(math.pi / 6) / math.cos(((ang % (math.pi / 3)) - math.pi / 6))
    if rad > hexr:
        return (0, 0, 0, 0)
    k = (ang + math.pi / 2) % (2 * math.pi / 5) - math.pi / 5
    star = size * 0.28 * (0.5 + 0.5 * math.cos(k * 2.5) ** 6)
    if rad < star + size * 0.06:
        return (255, 255, 255, 255)
    shade = 1 - rad / hexr * 0.35
    return (int(255 * shade), int(150 * shade), int(30 * shade), 255)


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    write_png(OUT / "orb.png", 96, 96, orb)
    write_png(OUT / "badge.png", 96, 96, badge)
    print("ok")
