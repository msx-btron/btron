#!/usr/bin/env python3
"""
scripts/tool_mk_pi400_payload.py — Build preloaded RAM payload for Pi 400 / Pi 4B

Offsets matching physical RAM addresses when loaded at 0x03000000
(initramfs btron_payload.bin 0x03000000). Packed so the file is ~68 MiB
instead of 144 MiB of zeros through the old 0x0A000000 /MSX slot:

  0x00000000 -> 0x03000000: btron_sys.vol     (HFDS /SYS, 1 MB)
  0x00200000 -> 0x03200000: btron_anders.vol  (HFDS /ANDERS, 1 MB)
  0x01000000 -> 0x04000000: pak0.pak          (Quake PAK, ~18.6 MB)
  0x02400000 -> 0x05400000: btron_msx.vol     (HFDS /MSX, 32 MB)

The Quake hunk lives at 0x08000000 and is not part of this blob.
"""

import os
import struct
import sys

MSX_PAYLOAD_OFF = 0x02400000  # RAM 0x05400000 when initramfs loads at 0x03000000
HFDS_MAGIC = (b'\x42\xfe', b'\xfe\x42')


def build_mbr_sector(partitions):
    mbr = bytearray(512)
    mbr[0:2] = b'\xEB\xFE'
    for i, (status, ptype, start_lba, nsec) in enumerate(partitions[:4]):
        off = 446 + i * 16
        mbr[off:off + 16] = struct.pack(
            '<B3sB3sII', status, b'\x00\x02\x00', ptype, b'\xFF\xFF\xFF', start_lba, nsec
        )
    mbr[510] = 0x55
    mbr[511] = 0xAA
    return mbr


def must_read(path, label):
    if not os.path.exists(path):
        print(f"[PAYLOAD] ERROR: {label} not found: {path}", file=sys.stderr)
        sys.exit(1)
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 2 or data[:2] not in HFDS_MAGIC:
        print(f"[PAYLOAD] ERROR: {label} is not an HFDS volume (magic {data[:4]!r})",
              file=sys.stderr)
        sys.exit(1)
    return data


def write_optional(out, path, offset, label):
    if not os.path.exists(path):
        print(f"[PAYLOAD] {label} skipped (no {path})")
        return
    with open(path, 'rb') as f:
        data = f.read()
    out.seek(offset)
    out.write(data)
    print(f"[PAYLOAD] {label} written: {len(data)} bytes at offset 0x{offset:x}")


def build_payload(out_path, use_mbr=False):
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    sys_data = must_read('btron_sys.vol', '/SYS')
    msx_data = must_read('btron_msx.vol', '/MSX')
    with open(out_path, 'wb') as out:
        if use_mbr:
            # Partitioned RAM payload: keep pak0.pak undisturbed at 0x01000000 (RAM 0x04000000)
            sys_off = 0x00100000      # LBA 2048 (1 MiB)
            anders_off = 0x00D00000   # LBA 26624 (13 MiB, 3 MiB before pak0.pak)
            parts = [
                (0x80, 0x13, sys_off // 512, (len(sys_data) + 511) // 512),
            ]
            if os.path.exists('btron_anders.vol'):
                anders_sz = os.path.getsize('btron_anders.vol')
                parts.append((0x00, 0x13, anders_off // 512, (anders_sz + 511) // 512))
            parts.append((0x00, 0x13, MSX_PAYLOAD_OFF // 512, (len(msx_data) + 511) // 512))
            out.write(build_mbr_sector(parts))
            out.seek(sys_off)
            out.write(sys_data)
            print(f"[PAYLOAD] MBR + /SYS written: {len(sys_data)} bytes at offset 0x{sys_off:x}")
            write_optional(out, 'btron_anders.vol', anders_off, '/ANDERS')
        else:
            out.write(sys_data)
            print(f"[PAYLOAD] /SYS written: {len(sys_data)} bytes at offset 0x0")
            write_optional(out, 'btron_anders.vol', 0x00200000, '/ANDERS')

        write_optional(out, 'assets/quake/pak/pak0.pak', 0x01000000, 'pak0.pak')

        out.seek(MSX_PAYLOAD_OFF)
        out.write(msx_data)
        print(f"[PAYLOAD] /MSX written: {len(msx_data)} bytes at offset 0x{MSX_PAYLOAD_OFF:x}")

        total_sz = out.tell()
        print(f"[PAYLOAD] Total payload size: {total_sz} bytes ({total_sz // (1024*1024)} MiB)")

    if total_sz < MSX_PAYLOAD_OFF + len(msx_data):
        print("[PAYLOAD] ERROR: payload truncated before /MSX", file=sys.stderr)
        sys.exit(1)
    with open(out_path, 'rb') as chk:
        chk.seek(MSX_PAYLOAD_OFF)
        magic = chk.read(2)
    if magic not in HFDS_MAGIC:
        print(f"[PAYLOAD] ERROR: /MSX slot at 0x{MSX_PAYLOAD_OFF:x} has magic {magic!r}",
              file=sys.stderr)
        sys.exit(1)
    print(f"[PAYLOAD] /MSX HFDS magic ok at payload 0x{MSX_PAYLOAD_OFF:x} -> RAM 0x05400000")

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--mbr']
    use_mbr = '--mbr' in sys.argv[1:]
    out_file = args[0] if args else '.build/pi400/btron_payload.bin'
    build_payload(out_file, use_mbr=use_mbr)

