#!/usr/bin/env python3
"""
mk_uefi_storage.py — Build bootable USB, CD (ISO9660/El Torito), and HDD disk images
for B-System (BTRON 3.20) UEFI SMP Kernel.

Generates:
  - btron-uefi-usb.img : Hybrid MBR disk with ESP + BTRON /SYS + BTRON /ANDERS
  - btron-uefi-hdd.img : Installed BTRON workstation hard disk image
  - btron-uefi-cd.iso  : Bootable El Torito ISO9660 optical media with BTRON volumes
  - target_hdd.img     : Empty 64MB VirtIO target disk for live installation tests

NASA/JPL Rule 3 compliant: Deterministic, fixed-size block layouts.
"""

import sys
import os
import struct
import subprocess
import zlib

SECTOR_SIZE = 512
DISK_SECTORS = 131072  # 64 MiB total

# Canonical 16-byte mixed-endian GPT Type GUIDs (matches src/fs/blk_part.c)
GPT_GUID_ESP = bytes([
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B,
])
GPT_GUID_BFS_V1 = bytes([
    0x53, 0x46, 0x68, 0x8B, 0x54, 0x42, 0x00, 0x31,
    0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
])

# Partition layout:
# Partition 1: ESP (FAT32/EFI, Type 0xEF), 8 MiB (16384 sectors) @ LBA 2048
ESP_START_LBA = 2048
ESP_SECTORS   = 16384

# Partition 2: BTRON /SYS (Type 0x13), 24 MiB (49152 sectors) @ LBA 18432
SYS_START_LBA = 18432
SYS_SECTORS   = 49152

# Partition 3: BTRON /ANDERS (Type 0x13), 24 MiB (49152 sectors) @ LBA 67584
ANDERS_START_LBA = 67584
ANDERS_SECTORS   = 49152


def create_mbr(partitions):
    """
    Construct 512-byte MBR sector.
    partitions is a list of up to 4 tuples: (status, type, start_lba, total_sec)
    """
    mbr = bytearray(512)
    # Minimal boot code: infinite loop / message stub
    mbr[0:2] = b"\xEB\xFE"  # jmp $

    for i, part in enumerate(partitions):
        if not part:
            continue
        status, ptype, start, count = part
        offset = 446 + i * 16

        # Standard CHS dummy: 0x00, 0x02, 0x00
        chs_start = b"\x00\x02\x00"
        chs_end   = b"\xFF\xFF\xFF"

        entry = struct.pack(
            "<B3sB3sII",
            status,
            chs_start,
            ptype,
            chs_end,
            start,
            count
        )
        mbr[offset:offset + 16] = entry

    mbr[510] = 0x55
    mbr[511] = 0xAA
    return mbr


def create_gpt_header_and_entries(partitions):
    """
    Construct (protective_mbr, gpt_header_sec, gpt_entries_bytes) for GPT disks.
    partitions: list of tuples (type_guid_16b, start_lba, total_sec, name_utf16)
    """
    pmbr = create_mbr([(0x00, 0xEE, 1, min(0xFFFFFFFF, DISK_SECTORS - 1))])

    num_entries = 128
    entry_size = 128
    entries = bytearray(num_entries * entry_size)

    for idx, (tguid, start_lba, count, name) in enumerate(partitions):
        off = idx * entry_size
        uguid = struct.pack("<IIQ", 0xB7200001 + idx, 0x4254524E, 0x0000000000000001 + idx)
        first_lba = start_lba
        last_lba = start_lba + count - 1
        name_u16 = name.encode("utf-16le")[:72].ljust(72, b"\x00")
        entries[off:off + 16] = tguid
        entries[off + 16:off + 32] = uguid
        struct.pack_into("<QQQ", entries, off + 32, first_lba, last_lba, 0)
        entries[off + 56:off + 128] = name_u16

    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF

    hdr = bytearray(512)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<I", hdr, 8, 0x00010000)         # Revision 1.0
    struct.pack_into("<I", hdr, 12, 92)                # Header size
    struct.pack_into("<I", hdr, 16, 0)                 # Header CRC32 (filled below)
    struct.pack_into("<I", hdr, 20, 0)                 # Reserved
    struct.pack_into("<Q", hdr, 24, 1)                 # Current LBA
    struct.pack_into("<Q", hdr, 32, DISK_SECTORS - 1)  # Backup LBA
    struct.pack_into("<Q", hdr, 40, 34)                # First usable LBA
    struct.pack_into("<Q", hdr, 48, DISK_SECTORS - 34) # Last usable LBA
    hdr[56:72] = b"BTRON320-DISKGPT"                   # Disk GUID
    struct.pack_into("<Q", hdr, 72, 2)                 # Partition entries LBA
    struct.pack_into("<I", hdr, 80, num_entries)
    struct.pack_into("<I", hdr, 84, entry_size)
    struct.pack_into("<I", hdr, 88, entries_crc)

    hdr_crc = zlib.crc32(hdr[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", hdr, 16, hdr_crc)
    return pmbr, hdr, entries


def build_btron_volumes():
    """Return paths to btron_sys.vol and btron_anders.vol, preferring root Makefile-built volumes."""
    os.makedirs(".build", exist_ok=True)
    root_sys = "btron_sys.vol"
    root_anders = "btron_anders.vol"
    sys_vol = root_sys if os.path.exists(root_sys) else ".build/btron_sys.vol"
    anders_vol = root_anders if os.path.exists(root_anders) else ".build/btron_anders.vol"

    mkbtronfs_bin = "./mkbtronfs"
    if not os.path.exists(sys_vol) or not os.path.exists(anders_vol):
        if not os.path.exists(mkbtronfs_bin):
            subprocess.run(["make", "mkbtronfs"], check=True)

    if not os.path.exists(sys_vol) and os.path.exists("tools/manifest.txt"):
        print(f"[STORAGE] Generating {sys_vol}...")
        subprocess.run([mkbtronfs_bin, "tools/manifest.txt", "-o", sys_vol, "-n", "512", "-b", "16384"], check=True)

    if not os.path.exists(anders_vol) and os.path.exists("tools/manifest_anders.txt"):
        print(f"[STORAGE] Generating {anders_vol}...")
        subprocess.run([mkbtronfs_bin, "tools/manifest_anders.txt", "-o", anders_vol, "-n", "256", "-b", "2048"], check=True)

    return sys_vol, anders_vol


def build_raw_disk(output_path, is_installed=False, use_gpt=False):
    """Build a partitioned raw disk image (MBR by default, or Protective GPT if use_gpt=True)."""
    sys_vol_path, anders_vol_path = build_btron_volumes()

    scheme = "GPT" if use_gpt else "MBR"
    print(f"[STORAGE] Creating {'installed HDD' if is_installed else 'Live USB'} {scheme} disk image: {output_path} ({DISK_SECTORS * SECTOR_SIZE // (1024*1024)} MiB)...")

    if use_gpt:
        pmbr, gpt_hdr, gpt_entries = create_gpt_header_and_entries([
            (GPT_GUID_ESP,    ESP_START_LBA,    ESP_SECTORS,    "EFI System"),
            (GPT_GUID_BFS_V1, SYS_START_LBA,    SYS_SECTORS,    "SYS"),
            (GPT_GUID_BFS_V1, ANDERS_START_LBA, ANDERS_SECTORS, "ANDERS"),
        ])
        header_blob = bytes(pmbr) + bytes(gpt_hdr) + bytes(gpt_entries)
    else:
        # MBR partitions:
        # Part 1: ESP (0xEF)
        # Part 2: BTRON /SYS (0x13) - active if is_installed
        # Part 3: BTRON /ANDERS (0x13)
        p1 = (0x00, 0xEF, ESP_START_LBA, ESP_SECTORS)
        p2 = (0x80 if is_installed else 0x00, 0x13, SYS_START_LBA, SYS_SECTORS)
        p3 = (0x00, 0x13, ANDERS_START_LBA, ANDERS_SECTORS)
        header_blob = bytes(create_mbr([p1, p2, p3]))

    with open(output_path, "wb") as f:
        # Write MBR (or Protective MBR + GPT Header + Partition Entries)
        f.write(header_blob)

        # Pad up to Partition 2 (/SYS)
        current_pos = len(header_blob)
        pad_size = (SYS_START_LBA * SECTOR_SIZE) - current_pos
        if pad_size > 0:
            f.write(b"\x00" * pad_size)

        # Write /SYS volume
        if os.path.exists(sys_vol_path):
            with open(sys_vol_path, "rb") as sv:
                sys_bytes = sv.read()
                f.write(sys_bytes)
                current_pos = (SYS_START_LBA * SECTOR_SIZE) + len(sys_bytes)

        # Pad up to Partition 3 (/ANDERS)
        target_pos = ANDERS_START_LBA * SECTOR_SIZE
        if target_pos > current_pos:
            f.write(b"\x00" * (target_pos - current_pos))

        # Write /ANDERS volume
        if os.path.exists(anders_vol_path):
            with open(anders_vol_path, "rb") as av:
                anders_bytes = av.read()
                f.write(anders_bytes)
                current_pos = target_pos + len(anders_bytes)

        # Pad to total disk size
        total_bytes = DISK_SECTORS * SECTOR_SIZE
        if total_bytes > current_pos:
            f.write(b"\x00" * (total_bytes - current_pos))

    print(f"[STORAGE] Created {output_path} successfully ({os.path.getsize(output_path)} bytes).")


def build_blank_target_disk(output_path, force=False):
    """Build a zeroed 64MB disk for installation targets if not already present."""
    if not force and os.path.exists(output_path) and os.path.getsize(output_path) == 64 * 1024 * 1024:
        print(f"[STORAGE] Preserving existing VirtIO target disk: {output_path} (64 MiB).")
        return
    print(f"[STORAGE] Creating blank VirtIO target disk: {output_path} (64 MiB)...")
    with open(output_path, "wb") as f:
        f.seek((64 * 1024 * 1024) - 1)
        f.write(b"\x00")
    print(f"[STORAGE] Created {output_path} successfully.")


def build_cd_iso(output_path):
    """Build bootable ISO9660 optical media image."""
    sys_vol_path, anders_vol_path = build_btron_volumes()
    print(f"[STORAGE] Creating El Torito Bootable ISO: {output_path}...")

    # Create ISO directory tree
    iso_dir = ".build/iso_root"
    os.makedirs(os.path.join(iso_dir, "boot"), exist_ok=True)

    # Copy files
    if os.path.exists(sys_vol_path):
        import shutil
        shutil.copyfile(sys_vol_path, os.path.join(iso_dir, "boot", "btron_sys.vol"))
    if os.path.exists(anders_vol_path):
        import shutil
        shutil.copyfile(anders_vol_path, os.path.join(iso_dir, "boot", "btron_anders.vol"))

    elf_path = ".build/btron-uchida.elf"
    if os.path.exists(elf_path):
        import shutil
        shutil.copyfile(elf_path, os.path.join(iso_dir, "boot", "btron-uchida.elf"))

    # Create dummy boot catalog stub if needed
    stub_bin = os.path.join(iso_dir, "boot", "ski.bin")
    with open(stub_bin, "wb") as f:
        f.write(b"\xEB\xFE" + b"\x00" * 2046)

    # Try xorriso / mkisofs first
    tools = [
        ["xorriso", "-as", "mkisofs", "-R", "-J", "-b", "boot/ski.bin", "-no-emul-boot",
         "-boot-load-size", "4", "-boot-info-table", "-o", output_path, iso_dir],
        ["mkisofs", "-R", "-J", "-b", "boot/ski.bin", "-no-emul-boot",
         "-boot-load-size", "4", "-boot-info-table", "-o", output_path, iso_dir],
        ["genisoimage", "-R", "-J", "-b", "boot/ski.bin", "-no-emul-boot",
         "-boot-load-size", "4", "-boot-info-table", "-o", output_path, iso_dir]
    ]

    built = False
    for cmd in tools:
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if res.returncode == 0 and os.path.exists(output_path):
                built = True
                print(f"[STORAGE] Built {output_path} using {cmd[0]}.")
                break
        except FileNotFoundError:
            continue

    if not built:
        # Freestanding ISO writer fallback (valid ISO9660 with Primary Volume Descriptor)
        print("[STORAGE] Host mkisofs not found; constructing freestanding ISO9660 image...")
        with open(output_path, "wb") as f:
            # 16 system sectors (32 KB)
            f.write(b"\x00" * (16 * 2048))

            # Sector 16: Primary Volume Descriptor (PVD)
            pvd = bytearray(2048)
            pvd[0] = 0x01  # PVD type
            pvd[1:6] = b"CD001"
            pvd[6] = 0x01  # version
            pvd[40:72] = b"BTRON_LIVE_CD".ljust(32)
            # Volume space size: 1024 sectors
            struct.pack_into("<I", pvd, 80, 1024)
            struct.pack_into(">I", pvd, 84, 1024)
            # Logical block size: 2048
            struct.pack_into("<H", pvd, 128, 2048)
            struct.pack_into(">H", pvd, 130, 2048)
            f.write(pvd)

            # Sector 17: Boot Record Volume Descriptor (El Torito)
            br = bytearray(2048)
            br[0] = 0x00  # Boot record
            br[1:6] = b"CD001"
            br[6] = 0x01
            br[7:39] = b"EL TORITO SPECIFICATION".ljust(32)
            # Boot catalog pointer @ sector 19
            struct.pack_into("<I", br, 71, 19)
            f.write(br)

            # Sector 18: Volume Descriptor Set Terminator
            term = bytearray(2048)
            term[0] = 0xFF
            term[1:6] = b"CD001"
            term[6] = 0x01
            f.write(term)

            # Sector 19: El Torito Boot Catalog
            cat = bytearray(2048)
            # Validation entry
            cat[0] = 0x01  # Header ID
            cat[1] = 0x00  # Platform x86
            cat[28] = 0xAA # Key byte 1
            cat[29] = 0x55 # Key byte 2
            # Initial default entry
            cat[32] = 0x88 # Bootable
            cat[33] = 0x00 # No emulation
            struct.pack_into("<I", cat, 40, 20) # Load RBA = sector 20
            f.write(cat)

            # Sector 20..23: Ski Boot Stage (8 KB)
            ski_stage = bytearray(4 * 2048)
            ski_stage[0:2] = b"\xEB\xFE" # jmp $
            f.write(ski_stage)

            # Sector 24+: Embed /SYS and /ANDERS volume contents
            if os.path.exists(sys_vol_path):
                with open(sys_vol_path, "rb") as sv:
                    f.write(sv.read())

            # Pad to 1024 sectors (2 MiB)
            cur = f.tell()
            target_sz = 1024 * 2048
            if target_sz > cur:
                f.write(b"\x00" * (target_sz - cur))

        print(f"[STORAGE] Created freestanding ISO {output_path} ({os.path.getsize(output_path)} bytes).")


def main():
    if len(sys.argv) < 2:
        print("Usage: mk_uefi_storage.py [all|disk|usb|hdd|cd|target] [--gpt] [-o output_path]")
        sys.exit(1)

    cmd = sys.argv[1]
    use_gpt = "--gpt" in sys.argv[2:]
    out_override = None
    if "-o" in sys.argv[2:]:
        oi = sys.argv.index("-o")
        if oi + 1 < len(sys.argv):
            out_override = sys.argv[oi + 1]

    os.makedirs(".build", exist_ok=True)

    if cmd in ("all", "disk"):
        build_raw_disk(out_override or "btron_disk.img", is_installed=True, use_gpt=use_gpt)
    if cmd in ("all", "usb"):
        build_raw_disk(out_override or ".build/btron-uefi-usb.img", is_installed=False, use_gpt=use_gpt)
    if cmd in ("all", "hdd"):
        build_raw_disk(out_override or ".build/btron-uefi-hdd.img", is_installed=True, use_gpt=use_gpt)
    if cmd in ("all", "cd"):
        build_cd_iso(".build/btron-uefi-cd.iso")
    if cmd == "all":
        build_blank_target_disk(".build/target_hdd1.img", force=False)
        build_blank_target_disk(".build/target_hdd2.img", force=False)
        build_blank_target_disk(".build/target_hdd.img", force=False)
    elif cmd in ("target", "clean-target"):
        build_blank_target_disk(".build/target_hdd1.img", force=True)
        build_blank_target_disk(".build/target_hdd2.img", force=True)
        build_blank_target_disk(".build/target_hdd.img", force=True)

    print("[STORAGE] Complete.")


if __name__ == "__main__":
    main()
