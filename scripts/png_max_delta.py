#!/usr/bin/env python3
"""Print the maximum channel difference between two static RGBA PNGs.

Exit 1 when the delta exceeds the bound. Exit 2 on a bad argument or a bad PNG.
"""

import struct
import sys
import zlib


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    with open(path, "rb") as source:
        data = source.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + str(path))
    pos = 8
    header = None
    idat = []
    idat_closed = False
    palette = False
    ended = False
    while pos < len(data):
        if pos + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        length, kind = struct.unpack(">I4s", data[pos : pos + 8])
        end = pos + 12 + length
        if length > 0x7FFFFFFF or end > len(data):
            raise ValueError("invalid PNG chunk length")
        chunk = data[pos + 8 : end - 4]
        crc = struct.unpack(">I", data[end - 4 : end])[0]
        if not kind.isalpha() or kind[2] & 32:
            raise ValueError("invalid PNG chunk type")
        if zlib.crc32(kind + chunk) != crc:
            raise ValueError("invalid PNG chunk checksum")
        pos = end
        if header is None and kind != b"IHDR":
            raise ValueError("PNG header must be first")
        if kind == b"IHDR":
            if header is not None or length != 13:
                raise ValueError("invalid PNG header")
            header = struct.unpack(">IIBBBBB", chunk)
            width, height, depth, color, compression, filtering, interlace = header
            if not (0 < width <= 0x7FFFFFFF and 0 < height <= 0x7FFFFFFF):
                raise ValueError("invalid PNG dimensions")
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("PNG must use 8-bit RGBA without interlacing")
        elif kind == b"IDAT":
            if idat_closed:
                raise ValueError("PNG image chunks must be consecutive")
            idat.append(chunk)
        elif kind == b"IEND":
            if length or not idat:
                raise ValueError("invalid PNG end chunk")
            ended = True
            break
        elif kind == b"PLTE":
            if palette or idat or not length or length > 768 or length % 3:
                raise ValueError("invalid PNG palette")
            palette = True
        elif kind in (b"acTL", b"fcTL", b"fdAT", b"tRNS") or not kind[0] & 32:
            raise ValueError("unsupported PNG chunk: " + kind.decode("ascii"))
        if kind != b"IDAT" and idat:
            idat_closed = True
    if not ended or pos != len(data):
        raise ValueError("missing PNG end or data after the end")
    inflater = zlib.decompressobj()
    raw = inflater.decompress(b"".join(idat))
    stride = width * 4
    if not inflater.eof or len(raw) != height * (stride + 1):
        raise ValueError("invalid PNG image data length")
    # PNG permits unused bytes after the complete zlib stream.
    rows = []
    prev = bytearray(stride)
    for index in range(0, len(raw), stride + 1):
        filt = raw[index]
        if filt > 4:
            raise ValueError("invalid PNG filter")
        row = bytearray(raw[index + 1 : index + 1 + stride])
        if filt:
            for x in range(stride):
                left = row[x - 4] if x >= 4 else 0
                if filt == 1:
                    predictor = left
                elif filt == 2:
                    predictor = prev[x]
                elif filt == 3:
                    predictor = (left + prev[x]) // 2
                else:
                    upper_left = prev[x - 4] if x >= 4 else 0
                    predictor = paeth(left, prev[x], upper_left)
                row[x] = (row[x] + predictor) & 255
        rows.append(bytes(row))
        prev = row
    return width, height, b"".join(rows)


def delta(path_a, path_b):
    wa, ha, pa = read_png(path_a)
    wb, hb, pb = read_png(path_b)
    if wa != wb or ha != hb or len(pa) != len(pb):
        raise ValueError("png size differs")
    return max(abs(a - b) for a, b in zip(pa, pb))


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: png_max_delta.py TOLERANCE reference.png other.png\n")
        return 2
    try:
        bound = int(sys.argv[1])
        if bound < 0:
            raise ValueError("tolerance cannot be negative")
        worst = delta(sys.argv[2], sys.argv[3])
    except (OSError, ValueError, zlib.error) as err:
        sys.stderr.write(str(err) + "\n")
        return 2
    sys.stdout.write(str(worst) + "\n")
    return 0 if worst <= bound else 1


if __name__ == "__main__":
    sys.exit(main())
