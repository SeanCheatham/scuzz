#!/usr/bin/env python3
"""Check pixel decoding, malformed image rejection, and command results."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

import png_max_delta as png


SIGNATURE = b"\x89PNG\r\n\x1a\n"
PIXELS = bytes([10, 20, 30, 40, 50, 60, 70, 80,
                80, 70, 60, 50, 40, 30, 20, 10])
ROWS = [
    ([10, 20, 30, 40, 50, 60, 70, 80], [80, 70, 60, 50, 40, 30, 20, 10]),
    ([10, 20, 30, 40, 40, 40, 40, 40], [80, 70, 60, 50, 216, 216, 216, 216]),
    ([10, 20, 30, 40, 50, 60, 70, 80], [70, 50, 30, 10, 246, 226, 206, 186]),
    ([10, 20, 30, 40, 45, 50, 55, 60], [75, 60, 45, 30, 231, 221, 211, 201]),
    ([10, 20, 30, 40, 40, 40, 40, 40], [70, 50, 30, 10, 216, 216, 206, 186]),
]


def chunk(kind, data=b""):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data)))


def header(width=2, height=2, depth=8, color=6, compression=0, filtering=0,
           interlace=0):
    return chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color,
                                     compression, filtering, interlace))


def image(raw, head=None):
    return SIGNATURE + (head or header()) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND")


class PngChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="scuzz-png-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write(self, data, name="image.png"):
        path = self.root / name
        path.write_bytes(data)
        return path

    def reject(self, data):
        with self.assertRaises((ValueError, zlib.error)):
            png.read_png(self.write(data))

    def command(self, tolerance, first, second):
        return subprocess.run([sys.executable, str(Path(png.__file__)),
                               str(tolerance), str(first), str(second)],
                              capture_output=True, text=True, timeout=10)

    def test_all_filters(self):
        for kind, rows in enumerate(ROWS):
            with self.subTest(filter=kind):
                raw = bytes([kind]) + bytes(rows[0]) + bytes([kind]) + bytes(rows[1])
                self.assertEqual(png.read_png(self.write(image(raw))), (2, 2, PIXELS))
        self.assertEqual(png.paeth(0, 3, 2), 0)
        self.assertEqual(png.paeth(3, 0, 2), 0)
        self.assertEqual(png.paeth(10, 20, 15), 15)

    def test_split_image_chunks_and_metadata(self):
        raw = b"\0" + PIXELS[:8] + b"\0" + PIXELS[8:]
        compressed = zlib.compress(raw)
        # Each byte can occupy its own image chunk. Trailing padding is valid.
        parts = [chunk(b"IDAT", bytes([byte])) for byte in compressed]
        parts.append(chunk(b"IDAT", b"\0\0"))
        data = (SIGNATURE + header() + chunk(b"PLTE", b"\0\0\0") +
                chunk(b"tEXt", b"Key\0value") + b"".join(parts) + chunk(b"IEND"))
        self.assertEqual(png.read_png(self.write(data)), (2, 2, PIXELS))

    def test_truncation_and_checksums(self):
        data = image(b"\0" + PIXELS[:8] + b"\0" + PIXELS[8:])
        for end in range(len(data)):
            with self.subTest(end=end):
                self.reject(data[:end])
        for byte in [32, len(data) - 1]:
            damaged = bytearray(data)
            damaged[byte] ^= 1
            self.reject(damaged)
        self.reject(data + b"extra")

    def test_image_data_size_and_filter(self):
        raw = b"\0" + PIXELS[:8] + b"extra"
        for length in range(len(raw) + 1):
            if length != 9:
                with self.subTest(length=length):
                    self.reject(image(raw[:length], header(height=1)))
        self.reject(image(b"\5" + PIXELS[:8], header(height=1)))
        raw = b"\0" + PIXELS[:8]
        self.reject(SIGNATURE + header(height=1) +
                    chunk(b"IDAT", zlib.compress(raw)[:-1]) + chunk(b"IEND"))

    def test_headers_and_chunk_order(self):
        for fields in [{"width": 0}, {"height": 0}, {"width": 0x80000000},
                       {"depth": 16}, {"color": 2}, {"compression": 1},
                       {"filtering": 1}, {"interlace": 1}]:
            with self.subTest(fields=fields):
                self.reject(image(b"\0" + PIXELS[:8] + b"\0" + PIXELS[8:],
                                  header(**fields)))
        body = chunk(b"IDAT", zlib.compress(b"\0" + PIXELS[:8] + b"\0" + PIXELS[8:]))
        for parts in [body + chunk(b"IEND"),
                      header() + header() + body + chunk(b"IEND"),
                      header() + chunk(b"IEND"),
                      header() + body + chunk(b"IEND", b"x"),
                      header() + body + chunk(b"tEXt", b"Key\0value") + body + chunk(b"IEND"),
                      header() + chunk(b"ABCD") + body + chunk(b"IEND"),
                      header() + chunk(b"acTL", struct.pack(">II", 1, 0)) + body + chunk(b"IEND"),
                      header() + chunk(b"PLTE", b"x") + body + chunk(b"IEND")]:
            with self.subTest(parts=parts[:30]):
                self.reject(SIGNATURE + parts)

    def test_command_results(self):
        raw = b"\0" + PIXELS[:8] + b"\0" + PIXELS[8:]
        first = self.write(image(raw), "first.png")
        changed = bytearray(raw)
        changed[-1] += 3
        second = self.write(image(changed), "second.png")
        for bound, code in [(2, 1), (3, 0), (300, 0)]:
            result = self.command(bound, first, second)
            self.assertEqual((result.returncode, result.stdout), (code, "3\n"))
        result = self.command(-1, first, second)
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("Traceback", result.stderr)

    def test_equal_incomplete_images(self):
        # Two equally incomplete images must not pass as equal.
        short = self.write(image(b"\0" + PIXELS[:4], header(height=1)), "short.png")
        result = self.command(0, short, short)
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
