"""Unit test cho tools/gach: COBS, CRC, đóng/mở khung (khớp src/runtime/src/frame_codec.hpp)."""
import importlib.machinery
import importlib.util
import random
import unittest
from pathlib import Path

_loader = importlib.machinery.SourceFileLoader("gach", str(Path(__file__).with_name("gach")))
_spec = importlib.util.spec_from_loader("gach", _loader)
gach = importlib.util.module_from_spec(_spec)
_loader.exec_module(gach)


class CodecTest(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(gach.crc16(b"123456789"), 0x29B1)

    def test_cobs_roundtrip(self):
        rng = random.Random(1)
        samples = [b"", b"\x00", b"\x00\x00", bytes(254 * [0x11]), bytes(255 * [0x11]), bytes(600 * [0x22])]
        samples += [bytes(rng.randrange(3) for _ in range(rng.randrange(600))) for _ in range(300)]
        samples += [bytes(rng.randrange(1, 256) for _ in range(rng.randrange(600))) for _ in range(300)]
        for data in samples:
            encoded = gach.cobs_encode(data)
            self.assertNotIn(0, encoded)
            self.assertEqual(gach.cobs_decode(encoded), data)

    def test_frame_roundtrip(self):
        frame = gach.encode_frame(gach.TYPE_CMD, 7, b"ls apps")
        self.assertEqual(frame[0], 0)
        self.assertEqual(frame[-1], 0)
        self.assertEqual(gach.decode_frame(frame[1:-1]), (gach.TYPE_CMD, 7, b"ls apps"))

    def test_corrupt_frame_rejected(self):
        frame = bytearray(gach.encode_frame(gach.TYPE_CMD, 7, b"info"))
        frame[3] ^= 0x01
        self.assertIsNone(gach.decode_frame(bytes(frame[1:-1])))

    def test_raw_log_is_not_a_frame(self):
        self.assertIsNone(gach.decode_frame(b"I (123) main: OS started\r\n"))


if __name__ == "__main__":
    unittest.main()
