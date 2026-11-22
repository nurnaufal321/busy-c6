#!/usr/bin/env python3
"""Draws the web app's icons (../../icons/) from the BUSY status: the word and its red panel as LED
dots on black. Black reaches the edges so the icons also work masked into circles and squircles.

Usage: tools/make_icons.py
"""
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from pack_status import read_png  # noqa: E402

OUT = ROOT.parent / "icons"
# The part of the 72x16 picture around the word, in LEDs.
CROP_X, CROP_W = 15, 42


def frame() -> list[tuple[int, int, int]]:
    z = zipfile.ZipFile(ROOT / "assets" / "animations" / "indicator_busy_72x16.zip")
    names = sorted((n for n in z.namelist() if n.endswith(".png") and "__MACOSX" not in n),
                   key=lambda s: int(re.findall(r"\d+", s)[-1]))
    px = read_png(z.read(names[len(names) // 2]))
    return [((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px]


def write_png(path: Path, size: int, rgb: list[tuple[int, int, int]]) -> None:
    raw = b"".join(b"\0" + bytes(c for p in rgb[y * size:(y + 1) * size] for c in p) for y in range(size))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def icon(size: int, leds: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    pitch = size * 0.78 / CROP_W
    left, top = (size - pitch * CROP_W) / 2, (size - pitch * 16) / 2
    dot = pitch * 0.8
    out = [(0, 0, 0)] * (size * size)
    for y in range(size):
        ly, fy = divmod(y - top, pitch)
        if not 0 <= ly < 16 or fy >= dot:
            continue
        for x in range(size):
            lx, fx = divmod(x - left, pitch)
            if 0 <= lx < CROP_W and fx < dot:
                out[y * size + x] = leds[int(ly) * 72 + CROP_X + int(lx)]
    return out


def main() -> None:
    OUT.mkdir(exist_ok=True)
    leds = frame()
    for size in (192, 512):
        write_png(OUT / f"icon-{size}.png", size, icon(size, leds))
    print(f"{OUT}: icon-192.png, icon-512.png")


if __name__ == "__main__":
    main()
