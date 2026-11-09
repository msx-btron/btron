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
import sys

MSX_PAYLOAD_OFF = 0x02400000  # RAM 0x05400000 when initramfs loads at 0x03000000
HFDS_MAGIC = (b'\x42\xfe', b'\xfe\x42')


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


def build_payload(out_path):
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    sys_data = must_read('btron_sys.vol', '/SYS')
    msx_data = must_read('btron_msx.vol', '/MSX')
    with open(out_path, 'wb') as out:
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
    out_file = sys.argv[1] if len(sys.argv) > 1 else '.build/pi400/btron_payload.bin'
    build_payload(out_file)

