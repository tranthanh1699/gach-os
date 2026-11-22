#!/usr/bin/env python3
"""Đóng gói thư mục app thành file .gapp (tar ustar không nén) cho gach-os.

Dùng:
  python3 tools/gapp_pack.py examples/apps/counter              # -> build/gapp/<id>.gapp
  python3 tools/gapp_pack.py examples/apps/counter -o out.gapp

Kiểm tra trước khi đóng gói: manifest.ini hợp lệ, có file entry, cú pháp Lua (nếu có `luac`).
Cài lên máy: chép .gapp vào thư mục /install trên thẻ SD rồi mở app "Apps" -> nút thẻ SD (hoặc khởi động lại).
"""
from __future__ import annotations

import argparse
import io
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

SDK_VERSION = 1  # Phải khớp src/runtime/include/runtime/sdk_version.hpp
ID_RE = re.compile(r"^[a-z0-9_-][a-z0-9._-]{0,30}$")
MAX_PATH = 100  # Giới hạn tên trong header tar ustar (không dùng prefix cho đơn giản).
EXCLUDE = {".DS_Store", "Thumbs.db"}


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
    files = sorted(p for p in app_dir.rglob("*") if p.is_file() and p.name not in EXCLUDE)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w", format=tarfile.USTAR_FORMAT) as tar:
        for path in files:
            name = path.relative_to(app_dir).as_posix()
            if len(name) >= MAX_PATH:
                raise SystemExit(f"error: path too long for package: {name}")
            data = path.read_bytes()
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            info.mtime = 0  # Gói tái lập: cùng nội dung -> cùng byte.
            tar.addfile(info, io.BytesIO(data))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("app_dir", type=Path)
    parser.add_argument("-o", "--output", type=Path, default=None)
    args = parser.parse_args()

    app_dir = args.app_dir.resolve()
    manifest = validate(app_dir)
    output = args.output or Path(__file__).resolve().parent.parent / "build" / "gapp" / f"{manifest['id']}.gapp"
    pack(app_dir, output)
    print(f"[gapp] {manifest['name']} v{manifest['version']} -> {output} ({output.stat().st_size} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
