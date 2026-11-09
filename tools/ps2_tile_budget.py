#!/usr/bin/env python3
"""PS2 desktop RAM budget — same rule as a retail PS2 game.

The Emotion Engine has 32 MiB of RDRAM.  A DVD can be 4 GiB and a CD 700 MiB;
those stay on the disc.  This loop sizes the eight standard windows and the
heap they share so the live working set fits in RAM, the way a PS2 title
keeps one level in memory and the rest on media.

Feedback: each candidate is printed.  The first that closes is written to
include/btron/ps2_tile.h.
"""
from __future__ import annotations

import argparse
import os
import sys

SCREEN_W = 800
SCREEN_H = 600
PANEL = 28
TITLE = 28
BORDER = 4

# Measured leftover after three boot windows on the 5500 KiB heap, once
# GTermState (2.68 MiB) and TEditor (512 KiB) are removed from the model.
MISC_KB = 1024
SLACK_KB = 256

# XMB atlas (resident in TinyGL after upload).  Staging is one sheet at a time.
XMB_ATLAS = 384 * 384 * 4
XMB_FONT = 128 * 96 * 4

APPS = (
    "term",
    "cabinet",
    "editor",
    "xmb",
    "mars",
    "gears",
    "cu",
    "quake",
)

HEADROOM_NOW = 691840  # bytes below 0x01FB0000 on the current ELF
EDITOR_BSS_FULL = 1000 * 512
HEAP_BASE_KB = 5500


def align16(n: int) -> int:
    return (n + 15) & ~15


def gdev_bytes(cell_w: int, cell_h: int) -> tuple[int, int, int]:
    cw = (cell_w - 2 * BORDER) & ~3
    ch = cell_h - TITLE - 2 * BORDER
    if cw < 160 or ch < 100:
        return 0, 0, 0
    return cw, ch, cw * ch * 4


def gterm_bytes(hist: int, cols: int) -> int:
    return hist * (cols + 1) + hist * 4 + 32 * 256 + 4096


def editor_bytes(rows: int, cols: int) -> int:
    return rows * cols + 4096


def try_budget(cols: int, rows: int, hist: int, ed_rows: int, ed_cols: int, heap_kb: int) -> dict | None:
    if cols * rows < len(APPS):
        return None
    cell_w = SCREEN_W // cols
    cell_h = (SCREEN_H - PANEL) // rows
    cw, ch, one = gdev_bytes(cell_w, cell_h)
    if one == 0:
        return None
    n = len(APPS)
    gdevs = n * one
    zbuf = cw * ch * 2  # one TinyGL z-buffer, shared
    gterm = gterm_bytes(hist, 256)
    editor = editor_bytes(ed_rows, ed_cols)
    xmb_res = XMB_ATLAS + XMB_FONT
    xmb_peak = XMB_ATLAS  # sequential bake: one staging sheet
    steady = gdevs + zbuf + gterm + editor + xmb_res + MISC_KB * 1024
    peak = steady + xmb_peak
    need = peak + SLACK_KB * 1024
    heap = heap_kb * 1024
    ok = need <= heap
    rects = []
    for i, name in enumerate(APPS):
        c, r = i % cols, i // cols
        rects.append(
            {
                "name": name,
                "x": c * cell_w,
                "y": PANEL + r * cell_h,
                "w": cell_w,
                "h": cell_h,
                "cw": cw,
                "ch": ch,
            }
        )
    return {
        "ok": ok,
        "cols": cols,
        "rows": rows,
        "cell_w": cell_w,
        "cell_h": cell_h,
        "cw": cw,
        "ch": ch,
        "hist": hist,
        "ed_rows": ed_rows,
        "ed_cols": ed_cols,
        "heap_kb": heap_kb,
        "gdev_one_kb": one // 1024,
        "gdevs_kb": gdevs // 1024,
        "zbuf_kb": zbuf // 1024,
        "gterm_kb": gterm // 1024,
        "editor_kb": editor // 1024,
        "xmb_res_kb": xmb_res // 1024,
        "peak_kb": peak // 1024,
        "need_kb": need // 1024,
        "steady_kb": steady // 1024,
        "rects": rects,
        "editor_bss_save": EDITOR_BSS_FULL - ed_rows * ed_cols,
    }


def search() -> dict:
    """Shrink buffers, then grow heap, until the eight-app set fits."""
    grids = ((4, 2), (2, 4), (3, 3))
    hists = (512, 256, 128)
    editors = ((200, 128), (120, 96), (80, 80))
    # Heap steps: keep 5500 if it closes; else spend ELF headroom + editor BSS save.
    heap_steps = (5500, 5800, 6100, 6400)

    n = 0
    best_fail = None
    for heap_kb in heap_steps:
        for hist in hists:
            for ed_rows, ed_cols in editors:
                for cols, rows in grids:
                    n += 1
                    b = try_budget(cols, rows, hist, ed_rows, ed_cols, heap_kb)
                    if not b:
                        continue
                    tag = "OK" if b["ok"] else "no"
                    print(
                        f"[{n:03d} {tag}] {cols}x{rows} cell={b['cell_w']}x{b['cell_h']} "
                        f"client={b['cw']}x{b['ch']} hist={hist} ed={ed_rows}x{ed_cols} "
                        f"heap={heap_kb}k peak={b['peak_kb']}k need={b['need_kb']}k "
                        f"gdev={b['gdevs_kb']}k zbuf={b['zbuf_kb']}k gterm={b['gterm_kb']}k"
                    )
                    if b["ok"]:
                        bss_delta = (heap_kb - HEAP_BASE_KB) * 1024 - b["editor_bss_save"]
                        if bss_delta > HEADROOM_NOW - 65536:
                            print(
                                f"    skip: BSS delta {bss_delta} exceeds ELF headroom "
                                f"{HEADROOM_NOW}"
                            )
                            continue
                        b["tries"] = n
                        b["bss_delta"] = bss_delta
                        return b
                    best_fail = b
    if best_fail:
        print("FAILED: no candidate closed the budget", file=sys.stderr)
        return best_fail
    raise SystemExit("no candidates")


def write_header(path: str, b: dict) -> None:
    lines = [
        "/* Generated by tools/ps2_tile_budget.py — RAM working set for the",
        " * eight-app PS2 desktop.  Game payloads (PAK, ROMs) stay on the disc.",
        " */",
        "#ifndef BTRON_PS2_TILE_H",
        "#define BTRON_PS2_TILE_H",
        "",
        f"#define PS2_BUDGET_HEAP_KB        {b['heap_kb']}",
        f"#define PS2_BUDGET_GTERM_HIST     {b['hist']}",
        f"#define PS2_BUDGET_TEDITOR_ROWS   {b['ed_rows']}",
        f"#define PS2_BUDGET_TEDITOR_COLS   {b['ed_cols']}",
        f"#define PS2_TILE_SCREEN_W         {SCREEN_W}",
        f"#define PS2_TILE_SCREEN_H         {SCREEN_H}",
        f"#define PS2_TILE_PANEL            {PANEL}",
        f"#define PS2_TILE_COLS             {b['cols']}",
        f"#define PS2_TILE_ROWS             {b['rows']}",
        f"#define PS2_TILE_N                {len(APPS)}",
        "",
        "enum {",
    ]
    for i, name in enumerate(APPS):
        comma = "," if i + 1 < len(APPS) else ""
        lines.append(f"    PS2_TILE_{name.upper()} = {i}{comma}")
    lines += [
        "};",
        "",
        "typedef struct { int x, y, w, h; } ps2_tile_rect_t;",
        "",
        "static inline const ps2_tile_rect_t *ps2_tile_rect(int id)",
        "{",
        "    static const ps2_tile_rect_t r[PS2_TILE_N] = {",
    ]
    for rec in b["rects"]:
        lines.append(
            f"        {{ {rec['x']}, {rec['y']}, {rec['w']}, {rec['h']} }}, "
            f"/* {rec['name']} */"
        )
    lines += [
        "    };",
        "    if (id < 0 || id >= PS2_TILE_N) return 0;",
        "    return &r[id];",
        "}",
        "",
        "int ps2_tile_open_standard(void);",
        "",
        "#endif /* BTRON_PS2_TILE_H */",
        "",
    ]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "-o",
        default=os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "include/btron/ps2_tile.h",
        ),
    )
    ap.add_argument("--check", action="store_true", help="fail unless a candidate fits")
    args = ap.parse_args()
    b = search()
    if not b["ok"]:
        print("budget did not close", file=sys.stderr)
        return 1
    write_header(args.o, b)
    print(
        f"WROTE {args.o}: {b['cols']}x{b['rows']} heap={b['heap_kb']} KiB "
        f"peak={b['peak_kb']} KiB tries={b['tries']} bss_delta={b['bss_delta']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
