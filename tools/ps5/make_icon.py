#!/usr/bin/env python3
"""Writes the PS5 homebrew's icon (sce_sys/icon0.png, 512x512), which websrv's homebrew list
shows: a plain picture drawn here, so the download carries nothing from the games.

    python3 tools/ps5/make_icon.py <icon0.png>
"""
import struct
import sys
import zlib

SIZE = 512


def pixel(x, y):
    # A dark blue square, lighter towards the bottom, with a white ring in the middle and a
    # second, smaller screen-shaped bar under it: two screens made one, as the program does.
    t = y / (SIZE - 1)
    bg = (int(20 + 30 * t), int(40 + 60 * t), int(90 + 80 * t))
    dx, dy = x - SIZE / 2, y - SIZE * 0.42
    r = (dx * dx + dy * dy) ** 0.5
    if 120 <= r <= 150:
        return (255, 255, 255)
    if 96 <= x < SIZE - 96 and SIZE * 0.78 <= y < SIZE * 0.84:
        return (255, 255, 255)
    return bg


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)
        for x in range(SIZE):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    with open(sys.argv[1], "wb") as f:
        f.write(png)


if __name__ == "__main__":
    main()
