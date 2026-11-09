#!/usr/bin/env python3
"""Append ISO9660 ';1' versions to file directory records.

hdiutil makehybrid writes names without a version.  Retail CDVDMAN
compares the first 12 bytes of the on-disc name against the SearchFile
query after it appends ';1', so PAK0.PAK (8 chars) never matches
PAK0.PAK;1.  Directory names and '.' / '..' stay untouched.
"""
from __future__ import print_function

import os
import struct
import sys

SECTOR = 2048


def rd_le32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def patch_dir_sector(sec):
    """Rewrite one directory sector. Returns (n_patched, ok, new_sector)."""
    records = []
    src_off = 0
    while src_off < SECTOR:
        rec_len = sec[src_off]
        if rec_len == 0:
            break
        if src_off + rec_len > SECTOR:
            return 0, False, bytes(sec)
        records.append(bytearray(sec[src_off : src_off + rec_len]))
        src_off += rec_len

    patched = 0
    new_recs = []
    for rec in records:
        nlen = rec[32]
        flags = rec[25]
        name = bytes(rec[33 : 33 + nlen])
        is_dir = bool(flags & 0x02)
        is_dot = nlen == 1 and name in (b"\x00", b"\x01")
        if (not is_dir) and (not is_dot) and (b";" not in name):
            tail = bytes(rec[33 + nlen :])
            body = bytearray(rec[: 33 + nlen])
            body[32] = nlen + 2
            body.extend(b";1")
            body.extend(tail)
            if len(body) & 1:
                body.append(0)
            body[0] = len(body)
            rec = body
            patched += 1
        new_recs.append(rec)

    out = bytearray(SECTOR)
    dst = 0
    for rec in new_recs:
        if dst + len(rec) > SECTOR:
            return patched, False, bytes(sec)
        out[dst : dst + len(rec)] = rec
        dst += len(rec)
    return patched, True, bytes(out)


def walk_and_patch(buf, dir_lba, dir_size, seen):
    total = 0
    nsec = (dir_size + SECTOR - 1) // SECTOR
    children = []

    for i in range(nsec):
        lba = dir_lba + i
        if lba in seen:
            continue
        seen.add(lba)
        off = lba * SECTOR
        if off + SECTOR > len(buf):
            continue
        sec = bytearray(buf[off : off + SECTOR])
        patched, ok, newsec = patch_dir_sector(sec)
        if not ok:
            print("ps2_iso_semicolons: sector %u would overflow" % lba,
                  file=sys.stderr)
            return -1
        if patched:
            buf[off : off + SECTOR] = newsec
            total += patched

        # Collect subdirectories from the original (pre-shift) names.
        src_off = 0
        while src_off < SECTOR:
            rec_len = sec[src_off]
            if rec_len == 0:
                break
            nlen = sec[src_off + 32]
            flags = sec[src_off + 25]
            name = bytes(sec[src_off + 33 : src_off + 33 + nlen])
            if (flags & 0x02) and not (nlen == 1 and name in (b"\x00", b"\x01")):
                clba = rd_le32(sec, src_off + 2)
                csz = rd_le32(sec, src_off + 10)
                children.append((clba, csz))
            src_off += rec_len

    for clba, csz in children:
        n = walk_and_patch(buf, clba, csz, seen)
        if n < 0:
            return -1
        total += n
    return total


def main():
    if len(sys.argv) != 2:
        print("usage: ps2_iso_semicolons.py <iso>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as f:
        buf = bytearray(f.read())
    pvd = buf[16 * SECTOR : 17 * SECTOR]
    if pvd[0] != 1 or pvd[1:6] != b"CD001":
        print("ps2_iso_semicolons: no ISO9660 PVD", file=sys.stderr)
        return 1
    root_lba = rd_le32(pvd, 156 + 2)
    root_sz = rd_le32(pvd, 156 + 10)
    n = walk_and_patch(buf, root_lba, root_sz, set())
    if n < 0:
        return 1
    with open(path, "wb") as f:
        f.write(buf)
    print("ps2_iso_semicolons: stamped ;1 on %d file records" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
