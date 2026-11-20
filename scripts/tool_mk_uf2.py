#!/usr/bin/env python3
"""
scripts/tool_mk_uf2.py — Zero-Dependency UF2 Packager & Verifier for PicoCalc

Supports:
  - RP2350 (Pico 2, ARM-S): family ID 0xE48BFF59, verifies PICOBIN IMAGE_DEF
  - RP2040 (Pico 1):        family ID 0xE48BFF56, patches 256-byte .boot2 MPEG-2 CRC32
  - Verification mode (`--verify`): checks UF2 magic, block numbering, family ID,
    RP2350 PICOBIN markers, or RP2040 boot2 CRC32.
"""

import argparse
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000

FAMILY_IDS = {
    "rp2350": 0xE48BFF59,  # RP2350_ARM_S
    "rp2040": 0xE48BFF56,  # RP2040
}

PICOBIN_START = 0xFFFFDED3
PICOBIN_END   = 0xAB123579


def crc32_mpeg2(data: bytes) -> int:
    """RP2040 Datasheet Section 2.8.1.3.1: CRC-32/MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF)."""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= (b << 24)
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def build_uf2(bin_path: str, uf2_path: str, board: str, base_addr: int, patch_boot2: bool):
    with open(bin_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 256:
        sys.exit(f"[ERROR] Binary too small ({len(data)} bytes): {bin_path}")

    if board == "rp2040" and patch_boot2:
        crc = crc32_mpeg2(bytes(data[:252]))
        struct.pack_into("<I", data, 252, crc)
        # Also write patched boot2 CRC32 back to .bin if needed
        with open(bin_path, "wb") as f:
            f.write(data)
        print(f"  [BOOT2] Patched RP2040 MPEG-2 CRC32 = 0x{crc:08X}")

    if board == "rp2350":
        head = bytes(data[:4096])
        if struct.pack("<I", PICOBIN_START) not in head or struct.pack("<I", PICOBIN_END) not in head:
            sys.exit(f"[ERROR] RP2350 PICOBIN IMAGE_DEF markers missing from first 4 KiB of {bin_path}")
        print("  [PICOBIN] Verified RP2350 ARM-S IMAGE_DEF in first 4 KiB")

    family_id = FAMILY_IDS[board]
    payload_sz = 256
    num_blocks = (len(data) + payload_sz - 1) // payload_sz

    out = bytearray()
    for blk in range(num_blocks):
        chunk = data[blk * payload_sz:(blk + 1) * payload_sz]
        if len(chunk) < payload_sz:
            chunk = chunk + bytes(payload_sz - len(chunk))
        hdr = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY,
            base_addr + blk * payload_sz,
            payload_sz,
            blk,
            num_blocks,
            family_id,
        )
        pad = bytes(476 - payload_sz)
        ftr = struct.pack("<I", UF2_MAGIC_END)
        out.extend(hdr)
        out.extend(chunk)
        out.extend(pad)
        out.extend(ftr)

    with open(uf2_path, "wb") as f:
        f.write(out)

    print(f"  [UF2] Wrote {uf2_path} ({num_blocks} blocks, {len(out)} bytes, family 0x{family_id:08X})")


def verify_uf2(uf2_path: str, board: str, expected_base: int):
    with open(uf2_path, "rb") as f:
        raw = f.read()

    if len(raw) == 0 or (len(raw) % 512) != 0:
        sys.exit(f"[FAIL] {uf2_path}: size {len(raw)} not a multiple of 512")

    nblk = len(raw) // 512
    expected_fam = FAMILY_IDS[board]
    reconstructed = bytearray()

    for i in range(nblk):
        blk = raw[i * 512:(i + 1) * 512]
        m0, m1, flags, addr, psz, bno, total, fam = struct.unpack_from("<IIIIIIII", blk, 0)
        mend, = struct.unpack_from("<I", blk, 508)
        if m0 != UF2_MAGIC_START0 or m1 != UF2_MAGIC_START1 or mend != UF2_MAGIC_END:
            sys.exit(f"[FAIL] {uf2_path}: bad UF2 magic at block {i}")
        if bno != i or total != nblk:
            sys.exit(f"[FAIL] {uf2_path}: bad block sequence {bno}/{total} at {i}")
        if fam != expected_fam or not (flags & UF2_FLAG_FAMILY):
            sys.exit(f"[FAIL] {uf2_path}: family 0x{fam:08X} != 0x{expected_fam:08X}")
        if i == 0 and addr != expected_base:
            sys.exit(f"[FAIL] {uf2_path}: base addr 0x{addr:08X} != 0x{expected_base:08X}")
        reconstructed.extend(blk[32:32 + psz])

    if board == "rp2040":
        calc = crc32_mpeg2(bytes(reconstructed[:252]))
        stored, = struct.unpack_from("<I", reconstructed, 252)
        if calc != stored:
            sys.exit(f"[FAIL] {uf2_path}: RP2040 boot2 CRC32 mismatch (0x{stored:08X} != 0x{calc:08X})")
        print(f"  [PASS] {uf2_path}: {nblk} blocks, family=0x{expected_fam:08X}, boot2 CRC32=0x{calc:08X}")
    else:
        head = bytes(reconstructed[:4096])
        if struct.pack("<I", PICOBIN_START) not in head or struct.pack("<I", PICOBIN_END) not in head:
            sys.exit(f"[FAIL] {uf2_path}: RP2350 PICOBIN markers missing")
        print(f"  [PASS] {uf2_path}: {nblk} blocks, family=0x{expected_fam:08X}, PICOBIN ARM-S OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", choices=["rp2350", "rp2040"], required=True)
    ap.add_argument("--base", type=lambda x: int(x, 0), default=0x10000000)
    ap.add_argument("--patch-boot2", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("input")
    ap.add_argument("output", nargs="?")
    args = ap.parse_args()

    if args.verify:
        verify_uf2(args.input, args.board, args.base)
    else:
        if not args.output:
            sys.exit("Usage: tool_mk_uf2.py --board <rp2350|rp2040> <in.bin> <out.uf2>")
        build_uf2(args.input, args.output, args.board, args.base, args.patch_boot2)


if __name__ == "__main__":
    main()
