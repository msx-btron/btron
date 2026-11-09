#!/usr/bin/env python3
"""PS2 desktop FPS feedback loop.

Measured initial: ~7 FPS per window (five TinyGL rasters in one 140 ms pass).
This loop searches skip-wave / UI pace / GL-per-pass until each window is at
least 2x that (14 FPS) and, if a candidate closes, +10 (17 FPS).  Focused GL
is paint-rate (quake-alone, NTSC-capped); unfocused rest.

Does not claim hardware-proven numbers.  UART `[RT] fps~=` is the live check.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ps2_tile_budget as budget  # noqa: E402


def main() -> int:
    f = budget.fps_search()
    print(
        "RESULT 2x=%s +10=%s fps/win=%.1f quake-alone=%.1f "
        "skip_wave=%s pace=%s gpp=%s"
        % (
            f["ok_2x"],
            f["ok_plus10"],
            f["fps_each"],
            f["quake_alone_fps"],
            f["skip_wave"],
            f["pace_us"],
            f["gl_per_pass"],
        )
    )
    return 0 if f["ok_2x"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
