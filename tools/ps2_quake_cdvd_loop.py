#!/usr/bin/env python3
"""PS2 Quake CDVD gate loop.

Paste a PCSX2 / hardware UART log.  The script names the stuck gate and
the next action.  Repeat until G6 PLAY (e1m1 in RAM, not the slipgate).

  python3 tools/ps2_quake_cdvd_loop.py uart.log
  python3 tools/ps2_quake_cdvd_loop.py --self-test

Boot path: `make PS2_MSX=1 PS2_QUAKE=1 run-ps2` loads btron-ps2.iso.
`ELF=1` is host: ELF with an empty tray; G1 DISC cannot pass.
"""
from __future__ import annotations

import argparse
import re
import sys

GATES = ("G0", "G1", "G2", "G3", "G4", "G5", "G6")

NEXT = {
    "G0": "Build and boot: make PS2_MSX=1 PS2_QUAKE=1 run-ps2",
    "G1": (
        "Boot btron-ps2.iso (make PS2_MSX=1 PS2_QUAKE=1 run-ps2). "
        "Do not load host:btron-ps2.elf (that is ELF=1, empty tray)."
    ),
    "G2": (
        "sceCdMmode returned 0 with a disc present. "
        "Match media: CD=1 (DiskType 0x12), DVD=2 (0x14). Recheck SCMD 34 recv."
    ),
    "G3": (
        "NCMD READ extra never DMA'd (xfer0=a5). "
        "Next: send_size 24 (Sony sceCdRead) or NCMD 13 ReadIOPMem."
    ),
    "G4": (
        "PVD landed but PAK did not hash. "
        "FILEIO cdrom0:\\PAK0.PAK;1 or ISO walk of QUAKE/PAK0.PAK."
    ),
    "G5": (
        "PAK hashed, BSP body missed. "
        "Range-read maps/e1m1.bsp from the open PAK fd / LBA."
    ),
    "G6": "Levels load. Replay Demo should World_LoadMap e1m1 and play drone1m1.",
}


def _disk_type(text: str) -> int | None:
    m = re.search(r"\[CDVD\] DiskType=(\d+)", text)
    if not m:
        return None
    return int(m.group(1))


def classify(text: str) -> dict:
    """Return {stuck, passed, evidence, next}."""
    passed: list[str] = []
    evidence: list[str] = []

    boot = bool(
        re.search(r"\[BOOT\] B-System", text)
        or re.search(r"ELF .* is executing", text)
        or re.search(r"\[FS\] PS2 disc PAK index starting", text)
    )
    if not boot:
        return {
            "stuck": "G0",
            "passed": [],
            "evidence": ["no [BOOT] / ELF executing line"],
            "next": NEXT["G0"],
        }
    passed.append("G0")

    dtype = _disk_type(text)
    host_elf = bool(
        re.search(r"loadelf:.*host:btron-ps2\.elf", text)
        or re.search(r"host:btron-ps2\.elf", text)
    )
    gate1_line = bool(re.search(r"GATE1 tray empty|GATE1 no disc", text))
    disc = dtype is not None and dtype >= 0x10
    if not disc:
        why = []
        if dtype is not None:
            why.append(f"DiskType={dtype}")
        else:
            why.append("no DiskType line")
        if host_elf:
            why.append("host:btron-ps2.elf")
        if gate1_line:
            why.append("GATE1")
        evidence.extend(why)
        return {
            "stuck": "G1",
            "passed": passed,
            "evidence": evidence,
            "next": NEXT["G1"],
        }
    passed.append("G1")
    evidence.append(f"DiskType={dtype}")

    mmode = re.search(r"\[CDVD\] sceCdMmode\((\d+)\)=(\d+)", text)
    if not mmode or int(mmode.group(2)) == 0:
        evidence.append(mmode.group(0) if mmode else "no sceCdMmode line")
        return {
            "stuck": "G2",
            "passed": passed,
            "evidence": evidence,
            "next": NEXT["G2"],
        }
    passed.append("G2")
    evidence.append(mmode.group(0))

    pvd_ok = bool(re.search(r"CDVDFSV PVD ok", text) or re.search(r"CD001", text))
    xfer_a5 = bool(re.search(r"xfer0=a5", text))
    pvd_miss = bool(re.search(r"PVD is not ISO9660|PVD miss", text))
    if not pvd_ok:
        if xfer_a5:
            evidence.append("xfer0=a5")
        if pvd_miss:
            evidence.append("PVD miss")
        return {
            "stuck": "G3",
            "passed": passed,
            "evidence": evidence,
            "next": NEXT["G3"],
        }
    passed.append("G3")

    pak = bool(
        re.search(r"e1m1=1", text)
        or re.search(r"PAK (fileio|disc|FILEIO|CDVDFSV).*indexed", text)
        or re.search(r"\[FS\] FILEIO PAK indexed", text)
        or re.search(r"\[FS\] CDVDFSV PAK indexed", text)
        or re.search(r"hashed=3[0-9]{2}", text)
    )
    if not pak:
        if re.search(r"FILEIO PAK missed|no PAK|PAK not indexed", text):
            evidence.append("PAK not indexed")
        return {
            "stuck": "G4",
            "passed": passed,
            "evidence": evidence,
            "next": NEXT["G4"],
        }
    passed.append("G4")

    bsp = bool(re.search(r"FS_CacheBSP: level maps/", text))
    bsp_miss = bool(re.search(r"FS_CacheBSP: maps/.*not found", text))
    if not bsp:
        if bsp_miss:
            evidence.append("FS_CacheBSP not found")
        return {
            "stuck": "G5",
            "passed": passed,
            "evidence": evidence,
            "next": NEXT["G5"],
        }
    passed.append("G5")

    load_fail = bool(re.search(r"World_LoadMap: Could not load", text))
    if load_fail and not re.search(
        r"World_LoadMap: Could not load.*\n(?:.*\n){0,5}FS_CacheBSP: level",
        text,
    ):
        evidence.append("World_LoadMap failed")
        return {
            "stuck": "G5",
            "passed": passed,
            "evidence": evidence,
            "next": NEXT["G5"],
        }
    passed.append("G6")
    return {
        "stuck": "G6",
        "passed": passed,
        "evidence": evidence + ["e1m1 in RAM"],
        "next": NEXT["G6"],
    }


def format_report(result: dict) -> str:
    lines = [
        f"stuck: {result['stuck']}  ({'PLAY' if result['stuck'] == 'G6' else 'open'})",
        f"pass:  {' '.join(result['passed']) or '(none)'}",
        f"why:   {', '.join(result['evidence']) or '-'}",
        f"next:  {result['next']}",
    ]
    return "\n".join(lines) + "\n"


SELF_TESTS = [
    (
        "g1-host-elf",
        "\n".join(
            [
                "loadelf: fname host:btron-ps2.elf secname all",
                "ELF host:btron-ps2.elf with entry point at 0x00100000 is executing.",
                "[BOOT] B-System / BTRON3 3.20  Emotion Engine R5900",
                "[FS] PS2 disc PAK index starting",
                "[CDVD] sceCdInit result=1 cdvdfsv=0 cdvdman=0",
                "[CDVD] SCMD 0x80000593 bound",
                "[CDVD] sceCdMmode(1)=0",
                "[CDVD] DiskType=0",
                "[CDVD] DiskReady=2",
                "[CDVD] READ lba=16 n=1 ret=0 pos=0 xfer0=a5",
                "[CDVD] CDVDFSV PVD is not ISO9660",
                "[CDVD] PVD miss; NCMD READ never landed CD001",
                "[FS] FILEIO PAK missed, trying CDVDFSV sectors",
            ]
        ),
        "G1",
    ),
    (
        "g3-pvd-a5",
        "\n".join(
            [
                "[BOOT] B-System / BTRON3 3.20",
                "[CDVD] sceCdMmode(1)=1",
                "[CDVD] DiskType=18",
                "[CDVD] DiskReady=2",
                "[CDVD] READ lba=16 n=1 ret=0 pos=0 xfer0=a5",
                "[CDVD] CDVDFSV PVD is not ISO9660",
            ]
        ),
        "G3",
    ),
    (
        "g4-pvd-ok-no-pak",
        "\n".join(
            [
                "[BOOT] B-System / BTRON3 3.20",
                "[CDVD] sceCdMmode(1)=1",
                "[CDVD] DiskType=18",
                "[CDVD] CDVDFSV PVD ok (RPC 0x80000595)",
                "[FS] FILEIO PAK missed, trying CDVDFSV sectors",
                "[FS] no PAK (FILEIO cdrom0:QUAKE/PAK0.PAK)",
            ]
        ),
        "G4",
    ),
    (
        "g6-play",
        "\n".join(
            [
                "[BOOT] B-System / BTRON3 3.20",
                "[CDVD] sceCdMmode(1)=1",
                "[CDVD] DiskType=18",
                "[CDVD] CDVDFSV PVD ok (RPC 0x80000595)",
                "[FS] FILEIO PAK indexed",
                "[FS] PAK fileio fd=1 size=18689235 dir=339 hashed=339 e1m1=1 first='sound/ambience.wav'",
                "FS_CacheBSP: level maps/e1m1.bsp (1361 KB)",
                "Demo_Play: started playback of 'drone1m1' (515 packets, 17.1s)",
            ]
        ),
        "G6",
    ),
]


def self_test() -> int:
    failed = 0
    for name, text, want in SELF_TESTS:
        got = classify(text)["stuck"]
        ok = got == want
        print(f"{'ok' if ok else 'FAIL'}  {name}: stuck={got} want={want}")
        if not ok:
            failed += 1
    print(f"{len(SELF_TESTS) - failed}/{len(SELF_TESTS)} gates")
    return 1 if failed else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", nargs="?", help="UART log file (default stdin)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.log:
        text = open(args.log, encoding="utf-8", errors="replace").read()
    else:
        text = sys.stdin.read()
    if not text.strip():
        print("stuck: G0\nnext:  paste a UART log (stdin or file)\n")
        return 1
    result = classify(text)
    sys.stdout.write(format_report(result))
    return 0 if result["stuck"] == "G6" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
