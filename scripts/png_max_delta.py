#!/usr/bin/env python3
"""Print the max per-channel delta between two RGBA PNGs.

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
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a png: " + path)
    pos = 8
    width = 0
    height = 0
    idat = b""
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height = struct.unpack(">II", chunk[:8])
            if chunk[8] != 8 or chunk[9] != 6:
                raise ValueError("png is not 8-bit rgba: " + path)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = width * 4
    rows = []
    index = 0
    prev = bytearray(stride)
    for _y in range(height):
        filt = raw[index]
        index += 1
        row = bytearray(raw[index : index + stride])
        index += stride
        if filt == 1:
            for x in range(stride):
                left = row[x - 4] if x >= 4 else 0
                row[x] = (row[x] + left) & 255
        elif filt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif filt == 3:
            for x in range(stride):
                left = row[x - 4] if x >= 4 else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif filt == 4:
            for x in range(stride):
                left = row[x - 4] if x >= 4 else 0
                up = prev[x]
                ul = prev[x - 4] if x >= 4 else 0
                row[x] = (row[x] + paeth(left, up, ul)) & 255
        elif filt != 0:
            raise ValueError("bad png filter")
        rows.append(bytes(row))
        prev = row
    return width, height, b"".join(rows)


def delta(path_a, path_b):
    wa, ha, pa = read_png(path_a)
    wb, hb, pb = read_png(path_b)
    if wa != wb or ha != hb or len(pa) != len(pb):
        raise ValueError("png size differs")
    worst = 0
    for a, b in zip(pa, pb):
        d = abs(a - b)
        if d > worst:
            worst = d
    return worst


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: png_max_delta.py TOLERANCE reference.png other.png\n")
        return 2
    try:
        bound = int(sys.argv[1])
        worst = delta(sys.argv[2], sys.argv[3])
    except (OSError, ValueError, zlib.error) as err:
        sys.stderr.write(str(err) + "\n")
        return 2
    sys.stdout.write(str(worst) + "\n")
    return 0 if worst <= bound else 1


if __name__ == "__main__":
    sys.exit(main())
