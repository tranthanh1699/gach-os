#!/usr/bin/env python3
"""Sinh đồ họa nguyên bản cho Core Guard (hình học thuần, khử răng cưa 4x). Chạy lại sau khi sửa:

    python3 appstore/apps/coreguard/tools/make_art.py

Ra: img/units.png (5 robot x 2 khung), img/enemies.png (4 địch x 3 khung), img/fx.png (đạn, tế bào năng lượng,
nổ), img/core.png (nút lõi), img/icon_<robot>.png (thẻ chọn trên HUD), img/cell.png (biểu tượng năng lượng).
"""
from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "img"
SS = 4  # Siêu lấy mẫu.
UNIT = 96
FX = 48


# ----------------------------------------------------------------------------- canvas
class Canvas:
    """Ảnh RGBA vẽ ở độ phân giải SS lần, thu nhỏ khi lưu (khử răng cưa)."""

    def __init__(self, w: int, h: int):
        self.w, self.h = w * SS, h * SS
        self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(self.w * self.h)]

    def blend(self, x: int, y: int, color, alpha: float = 1.0) -> None:
        if not (0 <= x < self.w and 0 <= y < self.h):
            return
        r, g, b = color[:3]
        a = (color[3] / 255.0 if len(color) > 3 else 1.0) * alpha
        p = self.px[y * self.w + x]
        inv = 1.0 - a
        p[0] = r * a + p[0] * inv
        p[1] = g * a + p[1] * inv
        p[2] = b * a + p[2] * inv
        p[3] = a + p[3] * inv

    def shape(self, inside, color, bbox) -> None:
        """Tô mọi điểm (tọa độ ảnh gốc, số thực) mà inside(x, y) đúng, trong bbox (x0, y0, x1, y1)."""
        x0, y0, x1, y1 = (int(max(0, v * SS)) for v in bbox)
        x1, y1 = min(self.w, x1 + SS), min(self.h, y1 + SS)
        for y in range(y0, y1):
            fy = (y + 0.5) / SS
            for x in range(x0, x1):
                c = inside((x + 0.5) / SS, fy)
                if c:
                    self.blend(x, y, c if isinstance(c, tuple) else color)

    def ellipse(self, cx, cy, rx, ry, color, shade=None):
        def inside(x, y):
            d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if d > 1:
                return None
            return shade(x, y, d) if shade else color
        self.shape(inside, color, (cx - rx, cy - ry, cx + rx, cy + ry))

    def rrect(self, x, y, w, h, r, color, shade=None):
        def inside(px, py):
            qx = max(abs(px - (x + w / 2)) - (w / 2 - r), 0)
            qy = max(abs(py - (y + h / 2)) - (h / 2 - r), 0)
            if qx * qx + qy * qy > r * r:
                return None
            return shade(px, py) if shade else color
        self.shape(inside, color, (x, y, x + w, y + h))

    def poly(self, pts, color):
        def inside(px, py):
            n, ok = len(pts), False
            j = n - 1
            for i in range(n):
                xi, yi = pts[i]
                xj, yj = pts[j]
                if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                    ok = not ok
                j = i
            return color if ok else None
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.shape(inside, color, (min(xs), min(ys), max(xs), max(ys)))

    def line(self, x1, y1, x2, y2, width, color):
        def inside(px, py):
            dx, dy = x2 - x1, y2 - y1
            t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy or 1)))
            return color if math.hypot(px - (x1 + t * dx), py - (y1 + t * dy)) <= width / 2 else None
        pad = width
        self.shape(inside, color, (min(x1, x2) - pad, min(y1, y2) - pad, max(x1, x2) + pad, max(y1, y2) + pad))

    def pixels(self):
        """RGBA 8-bit ở kích thước gốc."""
        w, h = self.w // SS, self.h // SS
        out = bytearray()
        for y in range(h):
            for x in range(w):
                acc = [0.0] * 4
                for sy in range(SS):
                    row = (y * SS + sy) * self.w
                    for sx in range(SS):
                        p = self.px[row + x * SS + sx]
                        acc[0] += p[0] * p[3]
                        acc[1] += p[1] * p[3]
                        acc[2] += p[2] * p[3]
                        acc[3] += p[3]
                a = acc[3] / (SS * SS)
                if acc[3] > 0:
                    out += bytes((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(a * 255)))
                else:
                    out += b"\0\0\0\0"
        return w, h, bytes(out)


def save(path: Path, frames: list[list[Canvas]]) -> None:
    """Ghép lưới khung (hàng x cột) thành một PNG RGBA."""
    rows = [[f.pixels() for f in row] for row in frames]
    fw, fh = rows[0][0][0], rows[0][0][1]
    cols = max(len(r) for r in rows)
    W, H = fw * cols, fh * len(rows)
    img = bytearray(W * H * 4)
    for ri, row in enumerate(rows):
        for ci, (_w, _h, data) in enumerate(row):
            for y in range(fh):
                dst = ((ri * fh + y) * W + ci * fw) * 4
                img[dst:dst + fw * 4] = data[y * fw * 4:(y + 1) * fw * 4]
    raw = b"".join(b"\0" + bytes(img[y * W * 4:(y + 1) * W * 4]) for y in range(H))

    def chunk(k, d):
        return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    print(f"{path.name}: {W}x{H}")


# ----------------------------------------------------------------------------- màu
INK = (24, 26, 36)
STEEL = (150, 160, 178)
STEEL_DARK = (92, 100, 118)
TEAL = (40, 190, 170)
YELLOW = (255, 214, 70)
BLUE = (70, 150, 255)
ICE = (180, 230, 255)
RED = (240, 70, 70)
WHITE = (250, 250, 255)


def lit(base, k):
    return tuple(max(0, min(255, int(c * k))) for c in base)


def body_shade(base, cy, h):
    """Tô sáng phía trên, tối phía dưới."""
    return lambda x, y, *_: lit(base, 1.25 - 0.5 * (y - (cy - h / 2)) / h)


def shadow(c: Canvas, cx, w):
    c.ellipse(cx, 88, w, 5, (0, 0, 0, 90))


# ----------------------------------------------------------------------------- robot
def dynamo(f: int) -> Canvas:
    c = Canvas(UNIT, UNIT)
    shadow(c, 48, 30)
    c.rrect(20, 44, 56, 42, 8, INK)
    c.rrect(23, 47, 50, 36, 6, TEAL, body_shade(TEAL, 65, 36))
    c.rrect(30, 56, 36, 6, 3, lit(TEAL, 0.6))
    c.rrect(30, 66, 36, 6, 3, lit(TEAL, 0.6))
    c.rrect(40, 26, 16, 22, 4, INK)
    c.rrect(43, 28, 10, 20, 3, STEEL)
    glow = 1.0 if f else 0.55
    c.ellipse(48, 24, 15, 15, (255, 230, 120, int(90 * glow)))
    c.ellipse(48, 24, 10, 10, INK)
    c.ellipse(48, 24, 8, 8, lit(YELLOW, 0.7 + 0.4 * glow))
    for a in range(3):  # Tia điện.
        ang = a * 2.1 + f * 0.7
        c.line(48 + 9 * math.cos(ang), 24 + 9 * math.sin(ang), 48 + 17 * math.cos(ang), 24 + 17 * math.sin(ang), 2,
               (255, 240, 160, int(255 * glow)))
    return c


def spark(f: int) -> Canvas:
    c = Canvas(UNIT, UNIT)
    shadow(c, 44, 28)
    c.rrect(18, 66, 52, 20, 6, INK)
    c.rrect(21, 68, 46, 15, 5, STEEL_DARK)
    c.ellipse(44, 52, 24, 22, INK)
    c.ellipse(44, 52, 21, 19, STEEL, lambda x, y, d: lit(STEEL, 1.2 - 0.4 * d))
    c.rrect(56, 45, 32, 14, 5, INK)
    c.rrect(58, 47, 28, 10, 4, STEEL_DARK)
    c.ellipse(86, 52, 5, 6, BLUE)
    c.ellipse(38, 48, 7, 7, INK)
    c.ellipse(38, 48, 5, 5, lit(BLUE, 1.2))
    if f:
        c.ellipse(90, 52, 10, 10, (150, 200, 255, 200))
        c.ellipse(90, 52, 5, 5, WHITE)
    return c


def frost(f: int) -> Canvas:
    c = Canvas(UNIT, UNIT)
    shadow(c, 48, 28)
    hexa = [(48 + 26 * math.cos(math.pi / 6 + k * math.pi / 3), 64 + 22 * math.sin(math.pi / 6 + k * math.pi / 3))
            for k in range(6)]
    c.poly(hexa, INK)
    hexb = [(48 + 22 * math.cos(math.pi / 6 + k * math.pi / 3), 64 + 18 * math.sin(math.pi / 6 + k * math.pi / 3))
            for k in range(6)]
    c.poly(hexb, (210, 232, 250))
    c.rrect(36, 58, 24, 9, 4, lit(BLUE, 0.9))
    c.poly([(48, 10), (58, 32), (48, 44), (38, 32)], INK)
    c.poly([(48, 14), (55, 32), (48, 41), (41, 32)], ICE if not f else WHITE)
    if f:
        c.ellipse(48, 28, 20, 20, (170, 225, 255, 80))
    return c


def bulwark(f: int) -> Canvas:
    c = Canvas(UNIT, UNIT)
    shadow(c, 48, 36)
    c.rrect(12, 18, 72, 70, 12, INK)
    c.rrect(15, 21, 66, 64, 10, STEEL, body_shade(STEEL, 53, 64))
    c.rrect(24, 36, 48, 12, 6, INK)
    c.ellipse(36, 42, 4, 4, (255, 170, 60))
    c.ellipse(60, 42, 4, 4, (255, 170, 60))
    for x, y in ((22, 28), (74, 28), (22, 78), (74, 78)):
        c.ellipse(x, y, 3, 3, STEEL_DARK)
    if f:  # Bị hư: vết nứt.
        c.line(30, 54, 42, 66, 3, INK)
        c.line(42, 66, 38, 78, 3, INK)
        c.line(62, 22, 56, 34, 3, INK)
    return c


def mine(f: int) -> Canvas:
    c = Canvas(UNIT, UNIT)
    shadow(c, 48, 26)
    c.ellipse(48, 74, 28, 12, INK)
    c.ellipse(48, 72, 25, 10, STEEL_DARK)
    c.ellipse(48, 69, 19, 7, STEEL)
    c.ellipse(48, 66, 7, 6, INK)
    c.ellipse(48, 66, 5, 4, RED if f else (90, 40, 40))
    if f:
        c.ellipse(48, 66, 14, 12, (255, 80, 80, 70))
    return c


ROBOTS = [("dynamo", dynamo), ("spark", spark), ("frost", frost), ("bulwark", bulwark), ("mine", mine)]


# ----------------------------------------------------------------------------- địch (nhìn sang trái)
def slime(c: Canvas, base, f: int, r: float = 30.0, cy: float = 60.0):
    squash = 1.0 + (0.08 if f == 1 else 0.0)
    rx, ry = r * squash, r / squash * 0.9
    shadow(c, 48, rx)
    c.ellipse(48, cy + 4, rx + 3, ry + 3, INK)
    c.ellipse(48, cy + 4, rx, ry, base, lambda x, y, d: lit(base, 1.3 - 0.55 * d - 0.2 * (y - cy) / ry))
    c.ellipse(40, cy - ry * 0.45, rx * 0.25, ry * 0.15, (255, 255, 255, 120))
    for ex in (34, 50):  # Mắt nhìn sang trái.
        c.ellipse(ex, cy - 2, 6, 7, WHITE)
        c.ellipse(ex - 2, cy - 1, 3, 4, INK)


def blob(f):
    c = Canvas(UNIT, UNIT)
    slime(c, (110, 205, 90), f)
    return c


def shell_blob(f):
    c = Canvas(UNIT, UNIT)
    slime(c, (160, 100, 210), f)
    c.ellipse(48, 40, 28, 14, INK)
    c.ellipse(48, 39, 25, 11, STEEL, lambda x, y, d: lit(STEEL, 1.2 - 0.4 * d))
    c.rrect(30, 42, 36, 5, 2, STEEL_DARK)
    return c


def skitter(f):
    c = Canvas(UNIT, UNIT)
    shadow(c, 48, 24)
    for k in range(3):
        lx = 38 + k * 10
        dy = 4 if (k + f) % 2 else -2
        c.line(lx, 66, lx - 6, 80 + dy, 3, INK)
        c.line(lx, 66, lx + 4, 80 - dy, 3, INK)
    c.ellipse(52, 62, 22, 14, INK)
    c.ellipse(52, 61, 19, 11, (240, 140, 50), lambda x, y, d: lit((240, 140, 50), 1.25 - 0.5 * d))
    c.ellipse(30, 60, 11, 10, INK)
    c.ellipse(30, 59, 9, 8, (250, 170, 70))
    c.ellipse(26, 57, 3, 3, INK)
    c.line(24, 52, 14, 42, 2, INK)
    c.line(30, 51, 26, 40, 2, INK)
    return c


def brute(f):
    c = Canvas(UNIT, UNIT)
    slime(c, (190, 60, 60), f, r=40, cy=54)
    for sx in (30, 48, 66):  # Gai lưng.
        c.poly([(sx - 7, 24), (sx, 8), (sx + 7, 24)], INK)
        c.poly([(sx - 4, 23), (sx, 12), (sx + 4, 23)], (230, 210, 200))
    return c


ENEMIES = [blob, shell_blob, skitter, brute]


def hit_frame(make):
    """Khung bị trúng: bản sáng trắng của khung đi."""
    c = make(0)
    for p in c.px:
        if p[3] > 0:
            p[0] = p[0] * 0.4 + 255 * 0.6
            p[1] = p[1] * 0.4 + 255 * 0.6
            p[2] = p[2] * 0.4 + 255 * 0.6
    return c


# ----------------------------------------------------------------------------- hiệu ứng (48x48)
def fx_frames():
    out = []
    bolt = Canvas(FX, FX)  # 0: tia điện.
    bolt.ellipse(24, 24, 14, 7, (120, 180, 255, 120))
    bolt.ellipse(24, 24, 9, 4, (200, 230, 255))
    bolt.ellipse(28, 24, 4, 3, WHITE)
    out.append(bolt)
    ice = Canvas(FX, FX)  # 1: đạn băng.
    ice.poly([(10, 24), (24, 16), (40, 24), (24, 32)], (150, 220, 255, 200))
    ice.poly([(16, 24), (24, 19), (34, 24), (24, 29)], WHITE)
    out.append(ice)
    for f in range(2):  # 2, 3: tế bào năng lượng (nhịp sáng).
        cell = Canvas(FX, FX)
        cell.ellipse(24, 24, 20, 20, (255, 220, 90, 80 + 60 * f))
        cell.rrect(15, 10, 18, 28, 5, INK)
        cell.rrect(17, 12, 14, 24, 4, lit(YELLOW, 0.9 + 0.2 * f))
        cell.rrect(20, 6, 8, 5, 2, INK)
        cell.poly([(26, 14), (19, 25), (24, 25), (21, 34), (29, 22), (24, 22)], WHITE)
        out.append(cell)
    for f in range(2):  # 4, 5: nổ.
        boom = Canvas(FX, FX)
        r = 14 + f * 8
        boom.ellipse(24, 24, r, r, (255, 150, 50, 200 - f * 80))
        boom.ellipse(24, 24, r * 0.6, r * 0.6, (255, 230, 120, 230 - f * 100))
        out.append(boom)
    return out


def core_node():
    c = Canvas(64, UNIT)
    c.rrect(10, 8, 44, 80, 10, INK)
    c.rrect(13, 11, 38, 74, 8, STEEL_DARK)
    c.ellipse(32, 48, 15, 15, INK)
    c.ellipse(32, 48, 12, 12, (90, 220, 255), lambda x, y, d: lit((90, 220, 255), 1.3 - 0.5 * d))
    c.rrect(20, 18, 24, 5, 2, lit(TEAL, 1.1))
    c.rrect(20, 74, 24, 5, 2, lit(TEAL, 1.1))
    return c


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    save(OUT / "units.png", [[make(0), make(1)] for _name, make in ROBOTS])
    save(OUT / "enemies.png", [[make(0), make(1), hit_frame(make)] for make in ENEMIES])
    save(OUT / "fx.png", [fx_frames()])
    save(OUT / "core.png", [[core_node()]])
    for name, make in ROBOTS:
        save(OUT / f"icon_{name}.png", [[make(1 if name in ("dynamo", "mine", "frost") else 0)]])
    save(OUT / "cell.png", [[fx_frames()[3]]])
