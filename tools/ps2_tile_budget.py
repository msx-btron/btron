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
# One BSP (e1m1 1333 KiB) + progs.dat (403 KiB) + a couple of MDLs.
# The rest of pak0 stays on the disc.
LEVEL_KB = 2100

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

HEADROOM_NOW = 1182336  # bytes below 0x01FB0000 after editor BSS shrink
EDITOR_BSS_FULL = 1000 * 512
HEAP_BASE_KB = 5500

# Time budget: one TinyGL raster per compositor pass.  Focused GL is
# picked first (paint-rate limited); unfocused round-robin the rest.
# Measured initial (five serial TinyGL paints) was ~7 FPS each.
LIVE_MS = 16
REST_MS = 100
GL_PER_PASS = 1
UI_BUDGET_US = 16667
FOREGROUND_GL_US = 9800   # mix: XMB-flat + gears + cu + quake
BG_BLIT_US = 200
N_GL = 4  # xmb, gears, cu, quake (mars is software blit)
SLOW_PAINT_US = 0
INITIAL_FPS = 7.0
XMB_WAVE_US = 12600
XMB_FLAT_US = 3200
GEARS_US = 9000
CU_US = 9000
QUAKE_US = 18000
PACE_US = 1000
SKIP_WAVE = 1


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
    steady = gdevs + zbuf + gterm + editor + xmb_res + (MISC_KB + LEVEL_KB) * 1024
    peak = steady + xmb_peak
    need = peak + SLACK_KB * 1024
    heap = heap_kb * 1024
    paint_us = int(
        FOREGROUND_GL_US * GL_PER_PASS
        + (N_GL - GL_PER_PASS) * BG_BLIT_US
        + 500
    )
    ok_ram = need <= heap
    ok_time = paint_us <= UI_BUDGET_US
    ok = ok_ram and ok_time
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
        "ok_ram": ok_ram,
        "ok_time": ok_time,
        "paint_us": paint_us,
        "live_ms": LIVE_MS,
        "rest_ms": REST_MS,
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
    heap_steps = (5500, 5800, 6100, 6400, 6500, 7000)

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
                    fps_each, _, comp_hz = fps_each_hz(SKIP_WAVE, PACE_US, GL_PER_PASS)
                    print(
                        f"[{n:03d} {tag}] {cols}x{rows} cell={b['cell_w']}x{b['cell_h']} "
                        f"client={b['cw']}x{b['ch']} hist={hist} ed={ed_rows}x{ed_cols} "
                        f"heap={heap_kb}k peak={b['peak_kb']}k need={b['need_kb']}k "
                        f"gdev={b['gdevs_kb']}k zbuf={b['zbuf_kb']}k gterm={b['gterm_kb']}k "
                        f"paint={b['paint_us']}us live={LIVE_MS}ms "
                        f"fps/win={fps_each:.1f} (N={N_GL}, close→{comp_hz:.0f})"
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
        f"#define PS2_TILE_LIVE_MS          {b['live_ms']}",
        f"#define PS2_TILE_REST_MS          {b['rest_ms']}",
        f"#define PS2_TILE_GL_PER_PASS      {GL_PER_PASS}",
        f"#define PS2_TILE_UI_PACE_US       {PACE_US}",
        f"#define PS2_TILE_SKIP_WAVE        {SKIP_WAVE}",
        f"#define PS2_TILE_QUAKE_ALONE_FPS  {int(b.get('quake_alone_fps', 55))}",
        f"#define PS2_TILE_FPS_EACH         {int(b.get('fps_each', 17))}",
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


def fps_pass_us(skip_wave: int, gl_per_pass: int) -> int:
    xmb = XMB_FLAT_US if skip_wave else XMB_WAVE_US
    paints = (xmb, GEARS_US, CU_US, QUAKE_US)
    avg = sum(paints) / len(paints)
    return int(avg * gl_per_pass + (N_GL - gl_per_pass) * BG_BLIT_US + 500)


def fps_each_hz(skip_wave: int, pace_us: int, gl_per_pass: int) -> tuple[float, int, float]:
    """Unfocused fair-share FPS (N_GL in rotation).  Focused is faster: see quake_alone."""
    pass_us = fps_pass_us(skip_wave, gl_per_pass)
    period = max(pass_us, pace_us, 1)
    hz = 1_000_000.0 / period
    return hz * gl_per_pass / N_GL, pass_us, hz


def quake_alone_fps(pace_us: int) -> tuple[float, int]:
    pass_us = QUAKE_US + 500
    period = max(pass_us, pace_us, 1)
    fps = 1_000_000.0 / period
    if fps > 60.0:
        fps = 60.0
    return fps, pass_us


def fps_search() -> dict:
    """Loop until 2x the measured 7 FPS, and +10 if a candidate closes."""
    target_2x = INITIAL_FPS * 2.0
    target_plus = INITIAL_FPS + 10.0
    best = None
    n = 0
    print("FPS harness: initial=%.1f  2x=%.1f  +10=%.1f  quake-alone cap 60" %
          (INITIAL_FPS, target_2x, target_plus))
    for skip in (1, 0):
        for pace in (1000, 8000, 16667):
            for gpp in (1, 2):
                n += 1
                each, pass_us, hz = fps_each_hz(skip, pace, gpp)
                qfps, qus = quake_alone_fps(pace if gpp == 1 else 1000)
                ok2 = each >= target_2x
                ok10 = each >= target_plus
                tag = "OK" if ok2 else "no"
                print(
                    f"[F{n:03d} {tag}] skip_wave={skip} pace={pace} gpp={gpp} "
                    f"pass={pass_us}us comp={hz:.1f}Hz fps/win={each:.1f} "
                    f"quake-alone={qfps:.1f} (paint={qus}us)"
                )
                rec = {
                    "skip_wave": skip,
                    "pace_us": pace,
                    "gl_per_pass": gpp,
                    "fps_each": each,
                    "pass_us": pass_us,
                    "comp_hz": hz,
                    "quake_alone_fps": qfps,
                    "ok_2x": ok2,
                    "ok_plus10": ok10,
                }
                if best is None or each > best["fps_each"]:
                    best = rec
                if ok2 and ok10:
                    rec["tries"] = n
                    return rec
    if best is None:
        raise SystemExit("no FPS candidates")
    best["tries"] = n
    return best


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
    f = fps_search()
    global GL_PER_PASS, PACE_US, SKIP_WAVE
    GL_PER_PASS = f["gl_per_pass"]
    PACE_US = f["pace_us"]
    SKIP_WAVE = f["skip_wave"]
    b["fps_each"] = f["fps_each"]
    b["quake_alone_fps"] = f["quake_alone_fps"]
    write_header(args.o, b)
    print(
        f"WROTE {args.o}: {b['cols']}x{b['rows']} heap={b['heap_kb']} KiB "
        f"peak={b['peak_kb']} KiB tries={b['tries']} bss_delta={b['bss_delta']}"
    )
    print(
        f"FPS  each={f['fps_each']:.1f} (was {INITIAL_FPS:.1f})  "
        f"quake-alone={f['quake_alone_fps']:.1f}  "
        f"skip_wave={f['skip_wave']} pace={f['pace_us']} gpp={f['gl_per_pass']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
