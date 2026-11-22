#!/usr/bin/env python3
"""Kiểm tra tuân thủ kiến trúc phân tầng của gach-os.

Đọc ma trận từ tools/layer_matrix.json rồi quét mọi file C/C++ trong src/ và platform/:
  1. Mỗi #include phải trỏ tới tầng được phép (layer matrix).
  2. File trong src/ không chứa mẫu cấm (cấp phát động, exception, RTTI, hàm chuỗi không an toàn).

Dùng:
  python3 tools/layer_check.py                 # kiểm tra, exit 1 nếu vi phạm
  python3 tools/layer_check.py --print-matrix  # in ma trận dạng bảng Markdown
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".cc"}
EXCLUDED_DIRS = {"build", "managed_components", ".git"}
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.MULTILINE)

STD_HEADERS = {
    # C++ standard library
    "algorithm", "array", "atomic", "bitset", "cassert", "cctype", "cerrno", "cfloat", "chrono",
    "cinttypes", "climits", "cmath", "cstdarg", "cstdbool", "cstddef", "cstdint", "cstdio", "cstdlib",
    "cstring", "ctime", "functional", "initializer_list", "iterator", "limits", "memory", "new",
    "numeric", "optional", "ratio", "tuple", "type_traits", "utility", "variant", "string_view",
    "span", "bit", "compare", "concepts",
    # C standard library
    "assert.h", "ctype.h", "errno.h", "float.h", "inttypes.h", "limits.h", "math.h", "stdarg.h",
    "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "string.h", "time.h",
}


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    rule: str
    detail: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.detail}"


def strip_code(text: str, keep_strings: bool) -> str:
    """Thay comment (và chuỗi nếu keep_strings=False) bằng khoảng trắng, giữ nguyên số dòng."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        ch, nxt = text[i], text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            end = text.find("\n", i)
            end = n if end == -1 else end
            out.append(" " * (end - i))
            i = end
        elif ch == "/" and nxt == "*":
            end = text.find("*/", i + 2)
            end = n if end == -1 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif ch in ('"', "'"):
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j] if keep_strings else ch + " " * max(0, j - i - 2) + ch)
            i = j
        else:
            out.append(ch)
            i += 1
    return "".join(out)


class LayerChecker:
    def __init__(self, root: Path, matrix: dict) -> None:
        self.root = root
        self.layers: dict = matrix["layers"]
        self.allow: dict = matrix["allow"]
        self.external: dict = matrix.get("external", {})
        self.banned = {k: re.compile(v) for k, v in matrix.get("banned_patterns", {}).items() if not k.startswith("$")}
        # Path dài nhất khớp trước -> tầng cụ thể (vd: osal_port) thắng tầng cha (osal).
        self.path_index = sorted(
            ((p, name) for name, spec in self.layers.items() for p in spec["paths"]),
            key=lambda item: len(item[0]),
            reverse=True,
        )
        self.prefix_index = {spec["include_prefix"]: name for name, spec in self.layers.items() if "include_prefix" in spec}

    def layer_of(self, rel_path: str) -> str | None:
        for prefix, name in self.path_index:
            if rel_path.startswith(prefix):
                return name
        return None

    def classify(self, src: Path, delim: str, target: str) -> str:
        for prefix, layer in self.prefix_index.items():
            if target.startswith(prefix):
                return layer
        for ext_name, patterns in self.external.items():
            if any(target == p or (p.endswith("/") and target.startswith(p)) for p in patterns):
                return ext_name
        if delim == "<" and target in STD_HEADERS:
            return "std"
        if delim == '"' and self._is_local(src, target):
            return "self"
        return "vendor"

    def _is_local(self, src: Path, target: str) -> bool:
        if (src.parent / target).is_file():
            return True
        rel = src.relative_to(self.root).parts
        if len(rel) >= 2 and rel[0] == "src":
            component = self.root / rel[0] / rel[1]
            return any((component / sub / target).is_file() for sub in ("src", "include"))
        return False

    def check_file(self, path: Path) -> list[Violation]:
        rel = path.relative_to(self.root).as_posix()
        layer = self.layer_of(rel)
        if layer is None:
            return []
        allowed = set(self.allow.get(layer, []))
        raw = path.read_text(encoding="utf-8", errors="replace")
        violations: list[Violation] = []

        if "*" not in allowed:
            no_comments = strip_code(raw, keep_strings=True)
            for match in INCLUDE_RE.finditer(no_comments):
                delim, target = match.group(1), match.group(2).strip()
                target_layer = self.classify(path, delim, target)
                if target_layer != "self" and target_layer != layer and target_layer not in allowed:
                    line = no_comments.count("\n", 0, match.start()) + 1
                    violations.append(Violation(rel, line, f"{layer} -> {target_layer}",
                                                f'#include {delim}{target} không được phép (xem tools/layer_matrix.json)'))

        if rel.startswith("src/"):
            code = strip_code(raw, keep_strings=False)
            for rule, regex in self.banned.items():
                for match in regex.finditer(code):
                    line = code.count("\n", 0, match.start()) + 1
                    violations.append(Violation(rel, line, f"banned:{rule}", f"'{match.group(0).strip()}'"))
        return violations

    def iter_sources(self):
        for top in ("src", "platform"):
            base = self.root / top
            if not base.is_dir():
                continue
            for path in sorted(base.rglob("*")):
                if path.suffix in SOURCE_SUFFIXES and path.is_file() and not EXCLUDED_DIRS.intersection(path.parts):
                    yield path

    def run(self) -> tuple[int, list[Violation]]:
        count, violations = 0, []
        for path in self.iter_sources():
            count += 1
            violations.extend(self.check_file(path))
        return count, violations


def print_matrix(matrix: dict) -> None:
    targets = ["std", "common", "mcal", "osal", "hal_api", "drivers", "framework", "runtime", "apps", "lvgl", "lua", "vendor"]
    print("| From \\ To | " + " | ".join(targets) + " |")
    print("| :--- |" + " :---: |" * len(targets))
    for layer, allowed in matrix["allow"].items():
        cells = ["✅" if ("*" in allowed or t in allowed or t == layer) else "❌" for t in targets]
        print(f"| **{layer}** | " + " | ".join(cells) + " |")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--matrix", type=Path, default=None)
    parser.add_argument("--print-matrix", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    matrix_path = args.matrix or root / "tools" / "layer_matrix.json"
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))

    if args.print_matrix:
        print_matrix(matrix)
        return 0

    count, violations = LayerChecker(root, matrix).run()
    for violation in violations:
        print(violation)
    status = "FAIL" if violations else "PASS"
    print(f"[layer-check] {status}: {count} files, {len(violations)} violation(s)")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
