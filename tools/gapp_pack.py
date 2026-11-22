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
import subprocess
import sys
import tarfile
from pathlib import Path

SDK_VERSION = 2  # Phải khớp src/runtime/include/runtime/sdk_version.hpp
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
