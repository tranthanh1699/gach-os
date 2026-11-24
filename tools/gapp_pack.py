#!/usr/bin/env python3
"""Đóng gói thư mục app thành file .gapp (tar ustar không nén) cho gach-os.

Dùng (thường gọi qua appstore/gapp.sh):
  python3 tools/gapp_pack.py appstore/apps/counter               # -> appstore/dist/<id>.gapp
  python3 tools/gapp_pack.py appstore/apps/counter -o out.gapp
  python3 tools/gapp_pack.py appstore/apps/counter --check       # chỉ kiểm tra
  python3 tools/gapp_pack.py --index appstore/dist               # sinh index.json (danh mục kho)

Kiểm tra trước khi đóng gói: manifest.ini hợp lệ, có file entry, cú pháp Lua (nếu có `luac`).
Cài lên máy: chép .gapp vào thư mục /install trên thẻ SD rồi mở app "Apps" -> nút thẻ SD (hoặc khởi động lại).
Công cụ nhanh: appstore/gapp.sh (new | check | pack | install | list).
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import zlib
from pathlib import Path

SDK_HEADER = Path(__file__).resolve().parent.parent / "src/runtime/include/runtime/sdk_version.hpp"


def read_sdk_version(header: Path = SDK_HEADER) -> int:
    """Đọc runtime::SDK_VERSION từ header C++ (nguồn sự thật duy nhất, tránh lệch phiên bản)."""
    match = re.search(r"SDK_VERSION\s*=\s*(\d+)U?", header.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(f"cannot find SDK_VERSION in {header}")
    return int(match.group(1))


SDK_VERSION = read_sdk_version()
ID_RE = re.compile(r"^[a-z0-9_-][a-z0-9._-]{0,30}$")
MAX_PATH = 100  # Giới hạn tên trong header tar ustar (không dùng prefix cho đơn giản).
EXCLUDE = {".DS_Store", "Thumbs.db"}
EXCLUDE_DIRS = {"tools"}  # Công cụ phát triển của app (VD script vẽ đồ họa): không đóng vào gói.

# ---- PNG -> .gimg (ảnh nạp thẳng vào LVGL, board không cần giải PNG) ----
GIMG_MAGIC = b"GIMG"
GIMG_VERSION = 1
GIMG_RGB565, GIMG_RGB565A8 = 0, 1
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}  # color type -> số kênh


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def png_filter_rows(rows: list[bytes], bpp: int, ftype: int) -> list[bytes]:
    """Lọc tham chiếu (dùng trong test để dựng PNG có filter 1-4)."""
    out, prev = [], bytes(len(rows[0]))
    for row in rows:
        f = bytearray(len(row))
        for i, x in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            pred = {0: 0, 1: a, 2: b, 3: (a + b) // 2, 4: _paeth(a, b, c)}[ftype]
            f[i] = (x - pred) & 0xFF
        out.append(bytes(f))
        prev = row
    return out


def _unfilter(data: bytes, width: int, height: int, bpp: int, name: str) -> list[bytearray]:
    stride = width * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        ftype = data[pos]
        row = bytearray(data[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            if ftype == 1:
                row[i] = (row[i] + a) & 0xFF
            elif ftype == 2:
                row[i] = (row[i] + b) & 0xFF
            elif ftype == 3:
                row[i] = (row[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                row[i] = (row[i] + _paeth(a, b, prev[i - bpp] if i >= bpp else 0)) & 0xFF
            elif ftype != 0:
                raise SystemExit(f"error: {name}: bad PNG filter {ftype}")
        rows.append(row)
        prev = row
    return rows


def png_to_gimg(png: bytes, name: str) -> bytes:
    """PNG 8-bit (gray, RGB, palette, gray+alpha, RGBA; không interlace) -> .gimg RGB565 hoặc RGB565A8."""
    if png[:8] != PNG_SIGNATURE:
        raise SystemExit(f"error: {name}: not a PNG file")
    pos, idat, plte, trns, ihdr = 8, b"", b"", b"", None
    while pos + 8 <= len(png):
        length, kind = struct.unpack(">I4s", png[pos:pos + 8])
        body = png[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if ihdr is None:
        raise SystemExit(f"error: {name}: missing IHDR")
    width, height, depth, ctype, _comp, _filt, interlace = ihdr
    if interlace != 0:
        raise SystemExit(f"error: {name}: interlaced PNG is not supported (save without interlace)")
    if depth != 8 or ctype not in PNG_CHANNELS:
        raise SystemExit(f"error: {name}: only 8-bit PNG is supported (got {depth}-bit, color type {ctype})")
    if width > 0xFFFF // 2 or height > 0xFFFF:
        raise SystemExit(f"error: {name}: image too large ({width}x{height})")
    rows = _unfilter(zlib.decompress(idat), width, height, PNG_CHANNELS[ctype], name)
    color, alpha = bytearray(), bytearray()
    for row in rows:
        for x in range(width):
            if ctype == 0:
                r = g = b = row[x]
                a = 0 if trns and row[x] == struct.unpack(">H", trns[:2])[0] else 255
            elif ctype == 2:
                r, g, b = row[3 * x:3 * x + 3]
                a = 255
            elif ctype == 3:
                idx = row[x]
                r, g, b = plte[3 * idx:3 * idx + 3]
                a = trns[idx] if idx < len(trns) else 255
            elif ctype == 4:
                r = g = b = row[2 * x]
                a = row[2 * x + 1]
            else:
                r, g, b, a = row[4 * x:4 * x + 4]
            color += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            alpha.append(a)
    has_alpha = any(a != 255 for a in alpha)
    fmt = GIMG_RGB565A8 if has_alpha else GIMG_RGB565
    head = struct.pack("<4sBBHHHHH", GIMG_MAGIC, GIMG_VERSION, fmt, 0, width, height, width * 2, 0)
    return head + bytes(color) + (bytes(alpha) if has_alpha else b"")


def parse_manifest(path: Path) -> dict[str, str]:
    fields: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        fields[key.strip()] = value.strip()
    return fields


def validate(app_dir: Path) -> dict[str, str]:
    manifest_path = app_dir / "manifest.ini"
    if not manifest_path.is_file():
        raise SystemExit(f"error: {manifest_path} not found")
    m = parse_manifest(manifest_path)
    errors = []
    if not ID_RE.match(m.get("id", "")) or ".." in m.get("id", ""):
        errors.append("id must match [a-z0-9._-], <= 31 chars")
    for key in ("name", "version", "sdk"):
        if not m.get(key):
            errors.append(f"missing '{key}'")
    if len(m.get("name", "")) > 23:
        errors.append("name must be <= 23 chars")
    if m.get("sdk", "").isdigit() and int(m["sdk"]) > SDK_VERSION:
        errors.append(f"sdk={m['sdk']} > supported {SDK_VERSION}")
    entry = m.get("entry", "main.lua")
    if "/" in entry or ".." in entry or not (app_dir / entry).is_file():
        errors.append(f"entry '{entry}' missing or invalid")
    if errors:
        raise SystemExit("error: invalid manifest:\n  - " + "\n  - ".join(errors))

    luac = shutil.which("luac5.4") or shutil.which("luac")
    if luac:
        result = subprocess.run([luac, "-p", str(app_dir / entry)], capture_output=True, text=True)
        if result.returncode != 0:
            raise SystemExit(f"error: Lua syntax:\n{result.stderr.strip()}")
    return m


def pack(app_dir: Path, output: Path) -> None:
    files = sorted(p for p in app_dir.rglob("*") if p.is_file() and p.name not in EXCLUDE
                   and p.relative_to(app_dir).parts[0] not in EXCLUDE_DIRS)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w", format=tarfile.USTAR_FORMAT) as tar:
        for path in files:
            name = path.relative_to(app_dir).as_posix()
            data = path.read_bytes()
            if path.suffix.lower() == ".png":  # Ảnh: đổi sẵn sang định dạng board vẽ thẳng.
                data = png_to_gimg(data, name)
                name = name[:-len(path.suffix)] + ".gimg"
            if len(name) >= MAX_PATH:
                raise SystemExit(f"error: path too long for package: {name}")
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            info.mtime = 0  # Gói tái lập: cùng nội dung -> cùng byte.
            tar.addfile(info, io.BytesIO(data))


def write_index(dist_dir: Path) -> Path:
    """Danh mục mọi gói trong dist_dir: nền tảng cho kho ứng dụng tải qua mạng."""
    apps = []
    for package in sorted(dist_dir.glob("*.gapp")):
        with tarfile.open(package, "r") as tar:
            member = tar.extractfile("manifest.ini")
            if member is None:
                raise SystemExit(f"error: {package.name} has no manifest.ini")
            text = member.read().decode("utf-8")
        fields = {}
        for raw in text.splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                fields[key.strip()] = value.strip()
        data = package.read_bytes()
        apps.append({
            "id": fields.get("id"), "name": fields.get("name"), "version": fields.get("version"),
            "sdk": int(fields.get("sdk", "0")), "icon": fields.get("icon", ""), "color": fields.get("color", ""),
            "permissions": [p.strip() for p in fields.get("permissions", "").split(",") if p.strip()],
            "file": package.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        })
    index = dist_dir / "index.json"
    index.write_text(json.dumps({"format": 1, "apps": apps}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return index


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("app_dir", type=Path, nargs="?")
    parser.add_argument("-o", "--output", type=Path, default=None)
    parser.add_argument("--check", action="store_true", help="chỉ kiểm tra, không đóng gói")
    parser.add_argument("--index", type=Path, metavar="DIST_DIR", help="sinh index.json cho thư mục gói")
    args = parser.parse_args()

    if args.index is not None:
        index = write_index(args.index.resolve())
        print(f"[gapp] index -> {index}")
        return 0
    if args.app_dir is None:
        parser.error("app_dir is required")

    app_dir = args.app_dir.resolve()
    manifest = validate(app_dir)
    if args.check:
        print(f"[gapp] OK {manifest['id']} ({manifest['name']} v{manifest['version']}, sdk {manifest['sdk']})")
        return 0
    output = args.output or Path(__file__).resolve().parent.parent / "appstore" / "dist" / f"{manifest['id']}.gapp"
    pack(app_dir, output)
    print(f"[gapp] {manifest['name']} v{manifest['version']} -> {output} ({output.stat().st_size} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
