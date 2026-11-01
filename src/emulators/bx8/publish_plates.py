#!/usr/bin/env python3
"""Publish BX-8 plates onto the fpgaMSX book page.

The Verilator runner writes a PPM. This script turns that file into a small
indexed PNG under msx.btron.pro/emulators/fpgaMSX/frames/ and stamps the
matching src in index.html, so a reload shows the new frame. It does not
start a simulation.

A flat capture does not replace a plate that already has a picture. Aurel's
title stays when a later run is still the blank upload.
"""

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BOOK = REPO.parent / "msx.btron.pro" / "emulators" / "fpgaMSX"
FRAMES = BOOK / "frames"
INDEX = BOOK / "index.html"
PLATES = REPO / "build" / "bx8" / "plates"
PNG_DIR = REPO / "build" / "bx8" / "png"

# Basename of the ROM file, lower-case, to the frame file on the page.
ROM_TO_FRAME = {
    "msx_logo-clean-final.rom": "logo-clean.png",
    "msx_logo-v2.rom": "logo-v2.png",
    "msx_logo.rom": "logo.png",
    "yazzier.rom": "yazzier.png",
    "c0man.rom": "c0man.png",
    "xracing_v1.0.4.rom": "xracing.png",
    "bullrush.rom": "bullrush.png",
    "aurel_vault.rom": "aurel.png",
    "btron8.rom": "btron8.png",
    "night_raven_flight_demo.rom": "flight.png",
    "night_raven_geo3d.rom": "geo3d.png",
    "neon_revenant-hc-geo3d-256-v0.2.0.rom": "neon.png",
}


def frame_for_name(name):
    low = name.lower()
    if low in ROM_TO_FRAME:
        return ROM_TO_FRAME[low]
    if "bad apple" in low:
        return "badapple.png"
    if low.startswith("dynamite"):
        return "dynamite.png"
    if low.startswith("neon"):
        return "neon.png"
    stem = low[:-4] if low.endswith(".ppm") or low.endswith(".png") else low
    if stem.endswith(".rom"):
        return frame_for_name(stem)
    candidate = stem + ".png"
    if (FRAMES / candidate).exists() or candidate in ROM_TO_FRAME.values():
        return candidate
    return None


def chunk(tag, content):
    crc = zlib.crc32(tag + content) & 0xFFFFFFFF
    return struct.pack(">I", len(content)) + tag + content + struct.pack(">I", crc)


def write_png(path, width, height, rgb):
    """rgb is width*height*3 bytes. Indexed when the picture fits a palette."""
    colors = {}
    order = []
    for i in range(0, len(rgb), 3):
        px = rgb[i:i + 3]
        if px not in colors:
            if len(order) == 256:
                colors = None
                break
            colors[px] = len(order)
            order.append(px)
    if colors is None:
        raw = bytearray()
        row = width * 3
        for y in range(height):
            raw.append(0)
            raw += rgb[y * row:(y + 1) * row]
        ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
        parts = [b"\x89PNG\r\n\x1a\n", chunk(b"IHDR", ihdr),
                 chunk(b"IDAT", zlib.compress(bytes(raw), 6)), chunk(b"IEND", b"")]
    else:
        depth = 4 if len(order) <= 16 else 8
        raw = bytearray()
        if depth == 4:
            for y in range(height):
                raw.append(0)
                row = rgb[y * width * 3:(y + 1) * width * 3]
                for x in range(0, width, 2):
                    hi = colors[row[x * 3:x * 3 + 3]]
                    lo = colors[row[(x + 1) * 3:(x + 1) * 3 + 3]] if x + 1 < width else 0
                    raw.append((hi << 4) | lo)
        else:
            for y in range(height):
                raw.append(0)
                row = rgb[y * width * 3:(y + 1) * width * 3]
                for x in range(width):
                    raw.append(colors[row[x * 3:x * 3 + 3]])
        pal = b"".join(order)
        ihdr = struct.pack(">IIBBBBB", width, height, depth, 3, 0, 0, 0)
        parts = [b"\x89PNG\r\n\x1a\n", chunk(b"IHDR", ihdr), chunk(b"PLTE", pal),
                 chunk(b"IDAT", zlib.compress(bytes(raw), 9)), chunk(b"IEND", b"")]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(parts))
    return len(order) if colors is not None else 0


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def unfilter(raw, height, row_bytes, pixel_bytes):
    rows = []
    prev = bytearray(row_bytes)
    i = 0
    for _ in range(height):
        filt = raw[i]
        i += 1
        row = bytearray(raw[i:i + row_bytes])
        i += row_bytes
        if filt == 1:
            for x in range(len(row)):
                left = row[x - pixel_bytes] if x >= pixel_bytes else 0
                row[x] = (row[x] + left) & 255
        elif filt == 2:
            for x in range(len(row)):
                row[x] = (row[x] + prev[x]) & 255
        elif filt == 3:
            for x in range(len(row)):
                left = row[x - pixel_bytes] if x >= pixel_bytes else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif filt == 4:
            for x in range(len(row)):
                left = row[x - pixel_bytes] if x >= pixel_bytes else 0
                up_left = prev[x - pixel_bytes] if x >= pixel_bytes else 0
                row[x] = (row[x] + paeth(left, prev[x], up_left)) & 255
        prev = row
        rows.append(bytes(row))
    return rows


def read_png(path):
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("not a png: %s" % path)
    pos = 8
    idat = b""
    plte = None
    width = height = depth = color = 0
    while pos + 8 <= len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
        elif tag == b"PLTE":
            plte = body
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    if color == 2 and depth == 8:
        rows = unfilter(raw, height, width * 3, 3)
        return b"".join(rows)
    if color == 3 and depth in (1, 2, 4, 8):
        pal = [plte[i:i + 3] for i in range(0, len(plte), 3)]
        row_bytes = (width * depth + 7) // 8
        rows = unfilter(raw, height, row_bytes, 1)
        out = bytearray()
        if depth == 8:
            for row in rows:
                for idx in row[:width]:
                    out += pal[idx]
        else:
            mask = (1 << depth) - 1
            for row in rows:
                for x in range(width):
                    shift = (8 - depth) - (x % (8 // depth)) * depth
                    idx = (row[x * depth // 8] >> shift) & mask
                    out += pal[idx]
        return bytes(out)
    if color == 0 and depth == 8:
        rows = unfilter(raw, height, width, 1)
        out = bytearray()
        for row in rows:
            for y8 in row[:width]:
                out += bytes((y8, y8, y8))
        return bytes(out)
    if color == 0 and depth == 1:
        row_bytes = (width + 7) // 8
        rows = unfilter(raw, height, row_bytes, 1)
        out = bytearray()
        for row in rows:
            for x in range(width):
                bit = (row[x // 8] >> (7 - (x % 8))) & 1
                v = 255 if bit else 0
                out += bytes((v, v, v))
        return bytes(out)
    raise ValueError("unsupported png %s color=%s depth=%s" % (path, color, depth))


def read_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("not a P6 ppm: %s" % path)
    pos = 2
    nums = []
    while len(nums) < 3:
        while pos < len(data) and data[pos] in b" \t\r\n":
            pos += 1
        if pos < len(data) and data[pos] == ord("#"):
            while pos < len(data) and data[pos] != ord("\n"):
                pos += 1
            continue
        start = pos
        while pos < len(data) and data[pos] not in b" \t\r\n":
            pos += 1
        nums.append(int(data[start:pos]))
    while pos < len(data) and data[pos] in b" \t\r\n":
        pos += 1
    width, height, _maxv = nums
    rgb = data[pos:pos + width * height * 3]
    if len(rgb) != width * height * 3:
        raise ValueError("short ppm %s" % path)
    return width, height, rgb


def load_rgb(path):
    if path.suffix.lower() == ".ppm":
        return read_ppm(path)
    rgb = read_png(path)
    # Indexed plates on this page are the MSX frame, 256 by 212.
    width = 256
    height = len(rgb) // (width * 3)
    return width, height, rgb


def unique_count(rgb):
    return len({rgb[i:i + 3] for i in range(0, len(rgb), 3)})


def stamp_index(frame_name, mtime):
    if not INDEX.exists():
        return False
    text = INDEX.read_text(encoding="utf-8")
    pat = re.compile(r"(frames/" + re.escape(frame_name) + r")(?:\?v=\d+)?")
    new, n = pat.subn(r"\1?v=%d" % mtime, text)
    if n and new != text:
        INDEX.write_text(new, encoding="utf-8")
        return True
    return False


def consider(path):
    frame = frame_for_name(path.name)
    if frame is None:
        return None
    try:
        mtime = path.stat().st_mtime
    except OSError:
        return None
    return frame, path, mtime


def collect(extra_ppm, extra_rom):
    found = {}
    forced = set()

    def add(item):
        if item is None:
            return
        frame, path, mtime = item
        prev = found.get(frame)
        if prev is None or mtime > prev[1]:
            found[frame] = (path, mtime)

    if PNG_DIR.is_dir():
        for path in PNG_DIR.glob("*.png"):
            add(consider(path))
    if PLATES.is_dir():
        for path in list(PLATES.glob("*.ppm")) + list(PLATES.glob("*.png")):
            add(consider(path))
    if extra_ppm:
        path = Path(extra_ppm)
        frame = frame_for_name(Path(extra_rom).name) if extra_rom else frame_for_name(path.name)
        if frame and path.exists():
            found[frame] = (path, path.stat().st_mtime + 1)
            forced.add(frame)
    return found, forced


def publish(found, forced, dry_run):
    wrote = 0
    for frame, (src, _mtime) in sorted(found.items()):
        dest = FRAMES / frame
        if frame not in forced and dest.exists() and src.stat().st_mtime <= dest.stat().st_mtime:
            continue
        try:
            width, height, rgb = load_rgb(src)
        except (ValueError, zlib.error, OSError) as exc:
            print("skip  %s  (%s)" % (frame, exc))
            continue
        src_n = unique_count(rgb)
        if dest.exists():
            try:
                old = read_png(dest)
            except ValueError as exc:
                print("plate %s unreadable (%s)" % (frame, exc))
                continue
            if old == rgb:
                continue
            if unique_count(old) > 2 and src_n <= 2:
                print("kept  %s  (new capture is flat, page already has the picture)" % frame)
                continue
        record = PNG_DIR / frame
        if dry_run:
            print("would %s  <- %s  (%d colors)" % (frame, src, src_n))
            wrote += 1
            continue
        n = write_png(dest, width, height, rgb)
        write_png(record, width, height, rgb)
        stamp_index(frame, int(dest.stat().st_mtime))
        print("plate %s  <- %s  (%d colors, %d bytes)" %
              (frame, src, n or src_n, dest.stat().st_size))
        wrote += 1
    if wrote == 0:
        print("plates unchanged")
    return wrote


def main():
    parser = argparse.ArgumentParser(description="Publish BX-8 plates to the fpgaMSX book")
    parser.add_argument("--ppm", help="PPM or PNG just written by the runner")
    parser.add_argument("--rom", help="Cartridge path, used to name --ppm")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not FRAMES.is_dir() or not INDEX.is_file():
        print("book frames not found at %s" % FRAMES, file=sys.stderr)
        return 1
    found, forced = collect(args.ppm, args.rom)
    publish(found, forced, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
