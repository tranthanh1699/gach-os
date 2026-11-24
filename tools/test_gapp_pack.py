"""Test đổi PNG -> .gimg của tools/gapp_pack.py (PNG tự sinh bằng zlib, không cần file mẫu)."""
import struct
import sys
import tarfile
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gapp_pack  # noqa: E402


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def make_png(w, h, color_type, rows, depth=8, interlace=0, plte=None, trns=None, filter_type=0) -> bytes:
    """rows: list các bytes dữ liệu điểm ảnh của từng dòng (chưa có byte filter)."""
    raw = b"".join(bytes([filter_type]) + row for row in rows)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, color_type, 0, 0, interlace))
    if plte is not None:
        out += chunk(b"PLTE", plte)
    if trns is not None:
        out += chunk(b"tRNS", trns)
    return out + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def header(gimg: bytes):
    magic, version, fmt, _r1, w, h, stride, _r2 = struct.unpack("<4sBBHHHHH", gimg[:16])
    return magic, version, fmt, w, h, stride


class GimgTest(unittest.TestCase):
    def test_rgba_to_rgb565a8(self):
        png = make_png(2, 1, 6, [bytes([255, 0, 0, 255, 0, 0, 255, 128])])
        out = gapp_pack.png_to_gimg(png, "a.png")
        self.assertEqual(header(out), (b"GIMG", 1, 1, 2, 1, 4))
        self.assertEqual(out[16:20], bytes([0x00, 0xF8, 0x1F, 0x00]))
        self.assertEqual(out[20:22], bytes([0xFF, 0x80]))
        self.assertEqual(len(out), 22)

    def test_opaque_rgb_to_rgb565(self):
        out = gapp_pack.png_to_gimg(make_png(1, 1, 2, [bytes([255, 255, 255])]), "w.png")
        self.assertEqual(header(out), (b"GIMG", 1, 0, 1, 1, 2))
        self.assertEqual(out[16:], b"\xff\xff")
        self.assertEqual(len(out), 18)

    def test_palette_with_trns(self):
        png = make_png(2, 1, 3, [bytes([0, 1])], plte=bytes([0, 255, 0, 0, 0, 255]), trns=bytes([0]))
        out = gapp_pack.png_to_gimg(png, "p.png")
        self.assertEqual(header(out)[2], 1)
        self.assertEqual(out[16:20], bytes([0xE0, 0x07, 0x1F, 0x00]))
        self.assertEqual(out[20:22], bytes([0x00, 0xFF]))

    def test_gray_alpha(self):
        out = gapp_pack.png_to_gimg(make_png(1, 1, 4, [bytes([255, 64])]), "g.png")
        self.assertEqual(header(out)[2], 1)
        self.assertEqual(out[16:19], bytes([0xFF, 0xFF, 64]))

    def test_filters_decoded(self):
        rows = [bytes([10, 20, 30, 40, 50, 60])] * 2
        plain = gapp_pack.png_to_gimg(make_png(2, 2, 2, rows), "f.png")
        for ftype in (1, 2, 3, 4):
            # Dữ liệu đã lọc khác dữ liệu gốc: dựng bằng bộ lọc tham chiếu rồi so kết quả giải.
            filtered = gapp_pack.png_filter_rows(rows, 3, ftype)
            raw = b"".join(bytes([ftype]) + r for r in filtered)
            png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
                   + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
            self.assertEqual(gapp_pack.png_to_gimg(png, "f.png"), plain, f"filter {ftype}")

    def test_interlaced_rejected(self):
        with self.assertRaises(SystemExit) as ctx:
            gapp_pack.png_to_gimg(make_png(1, 1, 2, [bytes(3)], interlace=1), "i.png")
        self.assertIn("i.png", str(ctx.exception))

    def test_16bit_rejected(self):
        with self.assertRaises(SystemExit) as ctx:
            gapp_pack.png_to_gimg(make_png(1, 1, 2, [bytes(6)], depth=16), "d.png")
        self.assertIn("d.png", str(ctx.exception))

    def test_pack_replaces_png(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / "app"
            (app / "img").mkdir(parents=True)
            (app / "main.lua").write_text("return {}\n")
            (app / "manifest.ini").write_text("id=t\nname=T\nversion=1\nsdk=8\n")
            (app / "img" / "a.png").write_bytes(make_png(1, 1, 2, [bytes([0, 0, 0])]))
            (app / "tools").mkdir()
            (app / "tools" / "make_art.py").write_text("# dev only\n")
            out = Path(tmp) / "t.gapp"
            gapp_pack.pack(app, out)
            with tarfile.open(out) as tar:
                names = tar.getnames()
        self.assertIn("img/a.gimg", names)
        self.assertNotIn("img/a.png", names)
        self.assertNotIn("tools/make_art.py", names)

    def test_index_tsv(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / "app"
            app.mkdir()
            (app / "main.lua").write_text("return {}\n")
            (app / "manifest.ini").write_text("id=org.t.demo\nname=Demo App\nversion=1.2.0\nsdk=8\nicon=play\ncolor=#112233\n")
            dist = Path(tmp) / "dist"
            gapp_pack.pack(app, dist / "org.t.demo.gapp")
            gapp_pack.write_index(dist)
            rows = (dist / "index.tsv").read_text().splitlines()
            page = (dist / "index.html").read_text()
            page_ok = ("Demo App" in page) and ('href="org.t.demo.gapp"' in page)
            data = (dist / "org.t.demo.gapp").read_bytes()
        self.assertEqual(rows[0], "#gach-store\t1")
        self.assertTrue(page_ok)
        cols = rows[1].split("\t")
        self.assertEqual(cols, ["org.t.demo", "Demo App", "1.2.0", "8", str(len(data)),
                                __import__("hashlib").sha256(data).hexdigest(), "org.t.demo.gapp", "play", "#112233"])


if __name__ == "__main__":
    unittest.main()
