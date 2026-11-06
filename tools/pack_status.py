#!/usr/bin/env python3
"""Builds status.pack, the 13 BUSY status animations for the ESP32-C6 display, from vendor/.

Format (little endian):

    header   "BSP1", u32 status count
    entries  char name[24], u16 fps, u16 frames, u32 offset, u32 size (offset from file start)
    data     for each status, its frames one after another

The first frame of a status is coded against a black screen, every other frame against the frame
before it, as tokens that together cover the 72x16 = 1152 pixels in reading order. A token's top
two bits give its type and the low six its length minus one:

    00  skip n pixels (unchanged)
    01  n literal pixels follow
    10  one pixel follows, repeated n times

Pixels are RGB565, little endian. Decoding mirrors main/anim.c; every run checks the pack by
decoding it again and comparing it with the source PNGs.

Usage: tools/pack_status.py [output, default pack/status.pack]
"""
import io
import json
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "vendor" / "apps_assets"
OUT = Path(__file__).resolve().parents[1] / "pack" / "status.pack"
W, H = 72, 16
PIXELS = W * H
# The flash partition for the pack; see partitions.csv.
LIMIT = 0x6C0000
NAME_LEN = 24


def read_png(data: bytes) -> list[int]:
    """Decodes an 8-bit, non-interlaced PNG to W*H RGB565 values, over black."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, palette, alpha = 8, b"", None, None
    width = height = depth = ctype = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and interlace == 0, f"unsupported PNG: depth {depth}, interlace {interlace}"
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            alpha = body
        elif kind == b"IDAT":
            idat += body
    assert (width, height) == (W, H), f"frame is {width}x{height}"
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = W * channels
    prev = bytearray(stride)
    out = []
    for y in range(H):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(W):
            px = line[x * channels:(x + 1) * channels]
            if ctype == 0:
                r, g, b_, al = px[0], px[0], px[0], 255
            elif ctype == 2:
                r, g, b_, al = px[0], px[1], px[2], 255
            elif ctype == 3:
                i = px[0]
                r, g, b_ = palette[i * 3:i * 3 + 3]
                al = alpha[i] if alpha and i < len(alpha) else 255
            elif ctype == 4:
                r, g, b_, al = px[0], px[0], px[0], px[1]
            else:
                r, g, b_, al = px
            r, g, b_ = r * al // 255, g * al // 255, b_ * al // 255
            out.append((r >> 3) << 11 | (g >> 2) << 5 | b_ >> 3)
    return out


def encode(frame: list[int], prev: list[int]) -> bytes:
    out = bytearray()
    i = 0
    while i < PIXELS:
        if frame[i] == prev[i]:
            n = 1
            while i + n < PIXELS and n < 64 and frame[i + n] == prev[i + n]:
                n += 1
            out.append(n - 1)
            i += n
            continue
        run = 1
        while i + run < PIXELS and run < 64 and frame[i + run] == frame[i]:
            run += 1
        if run >= 3:
            out.append(0x80 | (run - 1))
            out += struct.pack("<H", frame[i])
            i += run
            continue
        # Literals, until something cheaper starts.
        n = 1
        while i + n < PIXELS and n < 64 and frame[i + n] != prev[i + n]:
            if i + n + 2 < PIXELS and frame[i + n] == frame[i + n + 1] == frame[i + n + 2]:
                break
            n += 1
        out.append(0x40 | (n - 1))
        for p in frame[i:i + n]:
            out += struct.pack("<H", p)
        i += n
    return bytes(out)


def decode(data: bytes, pos: int, buf: list[int]) -> int:
    i = 0
    while i < PIXELS:
        token = data[pos]
        pos += 1
        n = (token & 0x3F) + 1
        kind = token >> 6
        if kind == 1:
            for k in range(n):
                buf[i + k] = struct.unpack_from("<H", data, pos)[0]
                pos += 2
        elif kind == 2:
            value = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            buf[i:i + n] = [value] * n
        else:
            assert kind == 0
        i += n
    assert i == PIXELS
    return pos


def frames_of(path: Path) -> tuple[int, list[list[int]]]:
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if n.endswith(".png") and "__MACOSX" not in n]
        names.sort(key=lambda n: int(re.findall(r"(\d+)\.png$", n)[0]))
        meta = next(n for n in z.namelist() if n.endswith("meta.json") and "__MACOSX" not in n)
        fps = json.load(io.BytesIO(z.read(meta)))["fps"]
        return fps, [read_png(z.read(n)) for n in names]


def statuses() -> list[tuple[str, Path]]:
    found = []
    for d in (ASSETS / "busy" / "themes").iterdir():
        if d.is_dir():
            found.append((json.loads((d / "theme.json").read_text())["order"], d.name))
    themes = [(n, ASSETS / "shared" / "animations" / f"{n}_72x16.zip") for _, n in sorted(found)]
    return [("busy", ASSETS / "busy" / "animations" / "indicator_busy_72x16.zip")] + themes


def pack_one(frames: list[list[int]]) -> bytes:
    out = bytearray()
    prev = [0] * PIXELS
    for f in frames:
        out += encode(f, prev)
        prev = f
    return bytes(out)


def main() -> None:
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else OUT
    loaded = []
    for name, path in statuses():
        fps, frames = frames_of(path)
        loaded.append([name, fps, frames, pack_one(frames)])

    header = 8 + len(loaded) * (NAME_LEN + 12)
    total = lambda: header + sum(len(s[3]) for s in loaded)
    # Too big for the partition: halve the frame rate of the biggest until it fits.
    while total() > LIMIT:
        s = max((s for s in loaded if s[1] > 15), key=lambda s: len(s[3]))
        s[1] //= 2
        s[2] = s[2][::2]
        s[3] = pack_one(s[2])
        print(f"too big: {s[0]} drops to {s[1]} fps")

    data = bytearray(b"BSP1" + struct.pack("<I", len(loaded)))
    offset = header
    for name, fps, frames, blob in loaded:
        data += name.encode().ljust(NAME_LEN, b"\0") + struct.pack("<HHII", fps, len(frames), offset, len(blob))
        offset += len(blob)
    for s in loaded:
        data += s[3]

    # Check: decode everything again and compare with the sources.
    for i, (name, fps, frames, blob) in enumerate(loaded):
        off = struct.unpack_from("<I", data, 8 + i * (NAME_LEN + 12) + NAME_LEN + 4)[0]
        buf, pos = [0] * PIXELS, off
        for n, f in enumerate(frames):
            pos = decode(data, pos, buf)
            assert buf == f, f"{name}: frame {n} differs"
        assert pos == off + len(blob), name
        print(f"{name:20} {fps:3} fps {len(frames):5} frames {len(blob) / 1024:8.0f} KB")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)
    print(f"{out_path}: {len(data) / 1024 / 1024:.2f} MB of {LIMIT / 1024 / 1024:.2f} MB, verified")


if __name__ == "__main__":
    main()
