#!/usr/bin/env python3
"""
scripts/tool_mk_pi400_payload.py — Build preloaded RAM payload for Pi 400 / Pi 4B

Offsets matching physical RAM addresses when loaded at 0x03000000:
  0x00000000 -> 0x03000000: btron_sys.vol (HFDS /SYS Volume, 1 MB)
  0x00200000 -> 0x03200000: btron_anders.vol (HFDS /ANDERS Volume, 1 MB)
  0x01000000 -> 0x04000000: pak0.pak (Quake Game Data PAK, 18.6 MB)
"""

import os
import sys

def build_payload(out_path):
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, 'wb') as out:
        # 1. /SYS Volume at offset 0
        if os.path.exists('btron_sys.vol'):
            with open('btron_sys.vol', 'rb') as f:
                data = f.read()
                out.write(data)
                print(f"[PAYLOAD] /SYS written: {len(data)} bytes at offset 0x0")

        # 2. /ANDERS Volume at offset 0x00200000 (2 MB)
        if os.path.exists('btron_anders.vol'):
            out.seek(0x00200000)
            with open('btron_anders.vol', 'rb') as f:
                data = f.read()
                out.write(data)
                print(f"[PAYLOAD] /ANDERS written: {len(data)} bytes at offset 0x200000")

        # 3. Quake pak0.pak at offset 0x01000000 (16 MB) -> RAM 0x04000000
        pak_path = 'assets/quake/pak/pak0.pak'
        if os.path.exists(pak_path):
            out.seek(0x01000000)
            with open(pak_path, 'rb') as f:
                data = f.read()
                out.write(data)
                print(f"[PAYLOAD] pak0.pak written: {len(data)} bytes at offset 0x1000000")

        # 4. /MSX Volume at offset 0x07000000 (112 MB) -> RAM 0x0A000000
        if os.path.exists('btron_msx.vol'):
            out.seek(0x07000000)
            with open('btron_msx.vol', 'rb') as f:
                data = f.read()
                out.write(data)
                print(f"[PAYLOAD] /MSX written: {len(data)} bytes at offset 0x7000000")

        total_sz = out.tell()
        print(f"[PAYLOAD] Total payload size: {total_sz} bytes ({total_sz // (1024*1024)} MiB)")

if __name__ == '__main__':
    out_file = sys.argv[1] if len(sys.argv) > 1 else '.build/pi400/btron_payload.bin'
    build_payload(out_file)

