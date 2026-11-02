#!/usr/bin/env python3
"""
Generate blueMSX config.ini machine descriptions for B-MSX (T-BIOS & T-DOS).
Encodes machine parameters in blueMSX INI format (avoiding XML).
"""

import os

MACHINES = [
    # MSX2 without FDD
    {
        "name": "MSX2 - T-BIOS",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": None, "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2 - T-BIOS - JP",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2_jp.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": None, "kanji": True, "sub_slot": 0
    },
    {
        "name": "MSX2 - T-BIOS - EU",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2_eu.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": None, "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2 - T-BIOS - BR",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2_br.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": None, "kanji": False, "sub_slot": 0
    },

    # MSX2 with FDD (T-DOS)
    {
        "name": "MSX2 - T-BIOS FDD",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": "tdos_disk.rom", "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2 - T-BIOS FDD - JP",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2_jp.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": "tdos_disk.rom", "kanji": True, "sub_slot": 0
    },
    {
        "name": "MSX2 - T-BIOS FDD - EU",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2_eu.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": "tdos_disk.rom", "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2 - T-BIOS FDD - BR",
        "video": "V9938", "board": "MSX-S1985", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2_br.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": "tdos_disk.rom", "kanji": False, "sub_slot": 0
    },

    # MSX2+ without FDD
    {
        "name": "MSX2+ - T-BIOS",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2+.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": None, "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2+ - T-BIOS - JP",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2+_jp.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": None, "kanji": True, "sub_slot": 0
    },
    {
        "name": "MSX2+ - T-BIOS - EU",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2+_eu.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": None, "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2+ - T-BIOS - BR",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 0, "r800": False,
        "main_rom": "tbios_main_msx2+_br.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": None, "kanji": False, "sub_slot": 0
    },

    # MSX2+ with FDD (T-DOS)
    {
        "name": "MSX2+ - T-BIOS FDD",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2+.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": "tdos_disk.rom", "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2+ - T-BIOS FDD - JP",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2+_jp.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": "tdos_disk.rom", "kanji": True, "sub_slot": 0
    },
    {
        "name": "MSX2+ - T-BIOS FDD - EU",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2+_eu.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": "tdos_disk.rom", "kanji": False, "sub_slot": 0
    },
    {
        "name": "MSX2+ - T-BIOS FDD - BR",
        "video": "V9958", "board": "MSX-T9769B", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_msx2+_br.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": "tdos_disk.rom", "kanji": False, "sub_slot": 0
    },

    # MSXturboR without & with FDD
    {
        "name": "MSXturboR - T-BIOS",
        "video": "V9958", "board": "MSX-T9769C", "subslot0": 1, "fdc": 0, "r800": True,
        "main_rom": "tbios_main_msxtr_jp.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": None, "kanji": True, "sub_slot": 1
    },
    {
        "name": "MSXturboR - T-BIOS FDD",
        "video": "V9958", "board": "MSX-T9769C", "subslot0": 1, "fdc": 1, "r800": True,
        "main_rom": "tbios_main_msxtr_jp.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": "tbios_music.rom", "disk_rom": "tdos_disk.rom", "kanji": True, "sub_slot": 1
    },

    # Hitachi HC-90 / HC-95 (HD64180 CPU)
    {
        "name": "Hitachi HC-95 - T-BIOS FDD",
        "video": "V9938", "board": "MSX-HD64180", "subslot0": 0, "fdc": 1, "r800": False,
        "main_rom": "tbios_main_hc95.rom", "logo_rom": "tbios_logo.rom", "sub_rom": "tbios_sub.rom",
        "music_rom": None, "disk_rom": "tdos_disk.rom", "kanji": True, "sub_slot": 0,
        "hd64180": True
    }
]

def generate_ini(m):
    lines = []
    lines.append("; ==============================================================================")
    lines.append(f"; B-MSX Machine Configuration: {m['name']}")
    lines.append("; blueMSX INI Specification (Clean-Room Parameters)")
    lines.append("; ==============================================================================")
    lines.append("")
    lines.append("[Video]")
    lines.append(f"version={m['video']}")
    lines.append("vram size=128kB")
    lines.append("")
    lines.append("[CMOS]")
    lines.append("Enable CMOS=1")
    lines.append("Battery Backed=1")
    lines.append("")
    lines.append("[Subslotted Slots]")
    lines.append(f"slot 0={m['subslot0']}")
    lines.append("slot 1=0")
    lines.append("slot 2=0")
    lines.append("slot 3=1")
    lines.append("")
    lines.append("[External Slots]")
    lines.append("slot A=1 0")
    lines.append("slot B=2 0")
    lines.append("")
    lines.append("[FDC]")
    lines.append(f"Count={m['fdc']}")
    lines.append("")
    lines.append("[CPU]")
    if m.get("hd64180"):
        lines.append("HD64180 Frequency=6144000Hz")
    else:
        lines.append("Z80 Frequency=3579545Hz")
    if m.get("r800"):
        lines.append("R800 Frequency=7159090Hz")
    lines.append("")
    lines.append("[Board]")
    lines.append(f"type={m['board']}")
    lines.append("")
    lines.append("[Slots]")

    # System devices
    lines.append("0 0 0 0 34 \"\" \"\"")   # RTC RP5C01
    lines.append("0 0 0 0 84 \"\" \"\"")   # PSG AY-3-8910
    if not m.get("r800"):
        lines.append("0 0 0 0 32 \"\" \"\"") # PPI 8255
    if m.get("kanji"):
        lines.append("0 0 0 0 24 \"Machines/Shared Roms/KANJI.rom\" \"\"")

    # turbo R specific devices
    if m.get("r800"):
        lines.append("0 0 0 0 58 \"\" \"\"") # S1990 bus controller
        lines.append("0 0 0 0 59 \"\" \"\"") # PCM unit
        lines.append("0 0 0 0 60 \"\" \"\"") # MSX-MIDI
        lines.append("0 0 0 0 90 \"\" \"\"") # turboR system timer
        lines.append("0 0 0 0 88 \"\" \"\"") # turboR memory manager
        lines.append("0 0 0 0 26 \"\" \"\"") # internal SRAM

    # Main ROM
    main_type = 141 if m.get("r800") else 20
    name_dir = f"Machines/{m['name']}"
    lines.append(f"0 0 0 4 {main_type} \"{name_dir}/{m['main_rom']}\" \"\"")

    # Logo ROM
    lines.append(f"0 0 4 2 20 \"{name_dir}/{m['logo_rom']}\" \"\"")

    # MSX-MUSIC
    if m.get("music_rom"):
        if m.get("r800"):
            lines.append(f"0 2 2 2 78 \"{name_dir}/{m['music_rom']}\" \"\"")
        else:
            lines.append(f"3 1 2 2 78 \"{name_dir}/{m['music_rom']}\" \"\"")

    # Sub-ROM
    sub_slot = m.get("sub_slot", 0)
    lines.append(f"3 {sub_slot} 0 2 20 \"{name_dir}/{m['sub_rom']}\" \"\"")

    # Mapper RAM (512 KiB / 1024 KiB)
    ram_subslot = 0 if m.get("r800") else 2
    lines.append(f"3 {ram_subslot} 0 64 22 \"\" \"\"")

    # Disk ROM (T-DOS / TC8566AF)
    if m.get("disk_rom"):
        lines.append(f"3 3 2 4 143 \"{name_dir}/{m['disk_rom']}\" \"\"")

    lines.append("")
    return "\n".join(lines)

def main():
    dest_dirs = [
        "third_party/cbios-nextor_FDD-and-turboR/configs/blueMSX/Machines",
        "src/emulators/msx/configs/blueMSX/Machines"
    ]
    reduced_dir = "src/emulators/msx/configs"
    os.makedirs(reduced_dir, exist_ok=True)
    count = 0

    # 1. Generate reduced path: ./src/emulators/msx/configs/*.ini
    for m in MACHINES:
        ini_path = os.path.join(reduced_dir, f"{m['name']}.ini")
        content = generate_ini(m)
        with open(ini_path, "w", encoding="utf-8") as f:
            f.write(content)
        count += 1

    # 2. Generate blueMSX subdirectory structure
    for root in dest_dirs:
        os.makedirs(root, exist_ok=True)
        for m in MACHINES:
            mach_dir = os.path.join(root, m["name"])
            os.makedirs(mach_dir, exist_ok=True)
            ini_path = os.path.join(mach_dir, "config.ini")
            content = generate_ini(m)
            with open(ini_path, "w", encoding="utf-8") as f:
                f.write(content)
            count += 1
    print(f"Generated {count} B-MSX INI config files.")

if __name__ == "__main__":
    main()

