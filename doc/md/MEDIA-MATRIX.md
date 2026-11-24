# B-System Native Matrix Screensaver (`./src/media/matrix/`) — Implementation Plan

## 1. Executive Summary & Architectural Decision

This plan details a clean, low-CPU, high-FPS C99 native port of Jeremy Sachs' **[Rezmason/matrix](https://github.com/Rezmason/matrix)** digital rain simulation for **B-System (BTRON)**, paired with a clean-room **B-RT / NASA JPL Power-of-10 / MISRA C** rewrite of **[tomolt/libschrift](https://github.com/tomolt/libschrift)** in `src/desktop/font/schrift.c` (`include/btron/schrift.h`).

### OpenGL (B-GL) vs. Native `wnd.c` Raw BitBlitting — Architectural Verdict

> [!IMPORTANT]
> **Decision: Native B-System `wnd.c` + `GDEV` Raw BitBlitting (with pre-rasterized `libschrift` glyph atlas & integer LUTs).**
> We do **not** need B-GL (`egl_create_window_surface`), and using raw 2D BitBlitting will be **10×–30× lower CPU** and **higher FPS** across B-System targets.

**Codebase-grounded rationale:**

| Criterion | B-GL (`src/desktop/graphics/gles/`) | Native `wnd.c` + `GDEV` Raw BitBlit |
| :--- | :--- | :--- |
| **Shader support** | Fixed-function OpenGL ES 1.1 (`gl_dispatch.h`) — **no GLSL/WGSL fragment shaders**. Cannot run `Rezmason/matrix`'s `rainPass` / `bloomPass` / `palettePass` / `stripePass` shaders natively. | Full pixel-level control in C99: exact film palettes, Pride stripes, Operator square ripples, and glow halos. |
| **CPU cost on POSIX / QEMU / UEFI / ARM64 / PC-98** | Runs **TinyGL / VirGL on CPU** (4×4 matrix transform, clipping, Z-buffer test, perspective UV interpolation, per-pixel blend for 2,000–4,000 quads/frame) + allocates ~2 MiB depth buffer. | **Zero 3D pipeline overhead.** Pre-rasterized 8-bit glyph atlas + 256-entry `0xAARRGGBB` LUT + **dirty-cell culling** (dark/unchanged cells are skipped). |
| **Inner-loop math** | Floating-point vertex/span math per frame. | **100% integer / Q16.16 fixed-point** in simulation and blitting; zero `float`/`double` per pixel. |
| **Compositor & target portability** | Requires GL stack; competes for PS2's single GS layer (`wnd->gl_layer`). | Works on **every** B-System target (POSIX, QEMU, T-Kernel, UEFI, ARM64, ARM32, PS2, PC-98, m68k, PicoCalc) and uses B-System's SIMD `btron_row_blit` ([fast_blit.h](file:///Users/tonpa/depot/bitedits/btron/include/btron/fast_blit.h)). |
| **Peak FPS & Idle CPU** | CPU-bound by TinyGL triangle rasterization (~15–30 FPS at high res). | **60+ FPS** with `< 1–2%` CPU via μITRON `dly_tsk()` frame pacing and dirty-cell blitting. |

*Clean decoupling:* The simulation (`matrix_sim.c`) is completely decoupled from the blitter (`matrix_render.c`), so if B-System later gains a programmable shader backend, a shader presenter can be added without modifying simulation or font logic.

---

## 2. Credits & Licensing

Proper attribution will be embedded in file headers, `licenses/`, and the application's **About** dialog:
- **Matrix Digital Rain Design, Modes & Fonts**: Ported from [Rezmason/matrix](https://github.com/Rezmason/matrix) by **Jeremy Sachs (Rezmason)** (MIT License). Typography assets `Matrix-Code.ttf` and `Matrix-Resurrected.ttf` and glyph mappings derived from the Rezmason/matrix project and the unofficial Matrix glyph database.
- **TrueType Rasterizer**: Clean-room BTRON B-RT / NASA JPL MISRA C port of [tomolt/libschrift](https://github.com/tomolt/libschrift) by **Thomas Oltmann and contributors** (ISC License, preserving `src/desktop/font/LICENSE.schrift`).

---

## 3. Phase 1 — Clean-Room `libschrift` Rewrite (B-RT / NASA JPL / MISRA C)

### Current State Audit
The repository currently contains raw upstream `libschrift 0.10.2` in [schrift.c](file:///Users/tonpa/depot/bitedits/btron/src/desktop/font/schrift.c) along with duplicate headers [include/btron/schrift.h](file:///Users/tonpa/depot/bitedits/btron/include/btron/schrift.h) and `src/desktop/font/schrift.h`. Currently it:
- Includes POSIX/Win32 headers (`<sys/mman.h>`, `<fcntl.h>`, `<unistd.h>`, `<windows.h>`), breaking freestanding targets.
- Calls `calloc`, `realloc` (`sft_reallocarray`), and `free` on every font load and glyph render (violates **NASA JPL Rule 3**: zero dynamic allocation after boot).
- Uses recursive functions (`tesselate_curves`, compound glyph resolution) and unbounded `STACK_ALLOC` macros (violates **NASA JPL Rules 1 & 2**).
- Is linked into `Makefile` and smoke-tested by [test_sps.c](file:///Users/tonpa/depot/bitedits/btron/verify/tests/test_sps.c#L197-L202) (`sft_version()`).

### Rewrite Plan

#### Files to Modify / Remove
- **[MODIFY] `include/btron/schrift.h`** — Single canonical public header (using B-RT `<rt/stddef.h>`, `<rt/stdtype.h>` / `<stdint.h>`).
- **[DELETE] `src/desktop/font/schrift.h`** — Remove duplicate header; update `schrift.c` to `#include <btron/schrift.h>`.
- **[REWRITE] `src/desktop/font/schrift.c`** — Clean-room BTRON B-RT NASA JPL Power-of-10 & MISRA C:2012 implementation.

#### NASA JPL Power-of-10 & MISRA C Engineering Rules Applied
1. **Rule 1 (Simple Control Flow / No Recursion)**:
   - Replace recursive compound-glyph traversal with an explicit fixed stack (`SFT_MAX_COMPOUND_DEPTH = 8`).
   - Replace recursive Bézier tessellation with an iterative subdivision loop (`SFT_MAX_BEZIER_STEPS = 64`).
   - Eliminate `goto` error chains in favor of structured status checks.
2. **Rule 2 (Fixed Loop Bounds)**:
   - Every `for`/`while` loop bounded by explicit compile-time constants (`SFT_MAX_TABLES = 64`, `SFT_MAX_CONTOURS = 256`, `SFT_MAX_POINTS = 2048`, `SFT_MAX_LINES = 4096`, `SFT_MAX_RASTER_W = 256`, `SFT_MAX_RASTER_H = 256`).
3. **Rule 3 (Zero Post-Boot Heap Allocation)**:
   - Introduce zero-heap B-System native APIs alongside backward-compatible wrappers:
     ```c
     typedef struct SFT_Font    SFT_Font;    /* Exposed fixed-size struct (~64 bytes) */
     typedef struct SFT_Scratch SFT_Scratch; /* Caller-owned static rasterization workspace */

     int sft_init_mem  (SFT_Font *font, const uint8_t *mem, size_t size);
     int sft_render_buf(const SFT *sft, SFT_Glyph glyph, SFT_Image image, SFT_Scratch *scratch);
     ```
   - Retain `sft_version()`, `sft_lmetrics()`, `sft_lookup()`, `sft_gmetrics()`, `sft_kerning()`, and `sft_loadmem()` / `sft_freefont()` (backed by a fixed static pool `SFT_STATIC_FONT_SLOTS = 8` instead of `calloc`/`free`) so [test_sps.c](file:///Users/tonpa/depot/bitedits/btron/verify/tests/test_sps.c) and existing callers remain 100% compatible with zero heap calls.
   - Remove all POSIX `mmap`/`fcntl`/`unistd` and Win32 `CreateFileMapping` code. `sft_loadfile()` either loads via B-System VFS into a caller/static buffer or returns `NULL` cleanly on freestanding builds where `sft_init_mem()` is preferred.
4. **Rule 4 & 5 (Short Functions & Assertions/Guards)**:
   - Every TrueType table read (`head`, `maxp`, `hhea`, `hmtx`, `loca`, `glyf`, `cmap` Format 4 & Format 12, `kern`) validated with overflow-safe `sft_safe_range(size, offset, len)` checks.
   - Zero `assert()` aborts; return explicit error codes (`0` success, `-1` invalid/bounds error).
5. **Non-FPU Fixed-Point Core & B-RT Native Math (Optimal for Cortex-M0+ / RP2040 & PS2)**:
   - Implement the entire internal outline transform, Bézier midpoint subdivision (`(a + b) >> 1` — zero divisions), DDA edge rasterizer, and `Cell { int32_t area, cover; }` coverage accumulator in **Q16.16 signed 32-bit fixed-point (`int32_t`)**.
   - Expose **zero-FPU native entry points** (`sft_gmetrics_q16()`, `sft_render_q16()`) that accept integer/Q16.16 scale directly so non-FPU MCU targets (RP2040 Cortex-M0+, m68k, ARMv6-M) execute **zero soft-float (`__aeabi_d*` / `__aeabi_f*`) instructions** during font rasterization.
   - Keep the standard `double`-based `sft_lmetrics()`, `sft_gmetrics()`, `sft_kerning()`, and `sft_render()` as thin wrappers around the Q16.16 core so desktop/hosted callers and [test_sps.c](file:///Users/tonpa/depot/bitedits/btron/verify/tests/test_sps.c) stay 100% API-compatible while also eliminating PS2 `trunc.w.s` FPU hazards.
   - Use B-RT `<rt/string.h>` (`memset`, `memcpy`) and compile cleanly under `-std=c99 -Wall -Wextra -Werror -Wconversion` across all B-System targets (`posix`, `qemu`, `arm64`, `ps2`, `uefi`, `tkernel`, `picocalc`).

---

## 4. Phase 2 — Typography Assets (`./assets/ttf/`)

### Asset Acquisition & Storage
Create `./assets/ttf/` and fetch the two official TrueType fonts from `Rezmason/matrix`:
- `assets/ttf/Matrix-Code.ttf` (~30–50 KB) — Classic, Operator, Megacity, Pride glyph set.
- `assets/ttf/Matrix-Resurrected.ttf` (~40–80 KB) — Resurrections & Trinity 135-glyph set.

### Glyph Database & Codepoint Mapping
Using the [unofficial glyph database](https://docs.google.com/spreadsheets/d/1NRJP88EzQlj_ghBbtjkGi-NbluZzlWpAqVIAq1MDGJc) and font `cmap` inspection:
- Write a small build/dev inspection script (or inspect during Phase 2) to extract the exact valid Unicode codepoints present in `Matrix-Code.ttf` and `Matrix-Resurrected.ttf`, and partition them into clean C99 `const uint32_t` glyph-set arrays:
  - `MATRIX_GLYPHS_CLASSIC[]` (katakana, digits, symbols from `Matrix-Code.ttf`)
  - `MATRIX_GLYPHS_MEGACITY[]` (Megacity / *Revolutions* subset from `Matrix-Code.ttf`)
  - `MATRIX_GLYPHS_OPERATOR[]` (dense operator terminal subset from `Matrix-Code.ttf`)
  - `MATRIX_GLYPHS_RESURRECTIONS[]` (full 135-glyph set from `Matrix-Resurrected.ttf`, also used by `trinity`)
- Support horizontal glyph mirroring (`glyph_flip`) at atlas rasterization time (zero runtime cost — flipped once into the atlas!).

### Dual Asset Loading Strategy (Hosted + Bare-Metal / MCU)
1. **Primary (VFS / Filesystem)**: Package `Matrix-Code.ttf` and `Matrix-Resurrected.ttf` into `tools/manifest.txt` (under `/SYS`) and load from `assets/ttf/` on hosted/VFS builds.
2. **Embedded C-Array Fallback (MCU / Non-FS Targets)**: Generate `src/media/matrix/matrix_ttf_embed.inc` (`static const uint8_t g_matrix_code_ttf[]` and `g_matrix_resurrected_ttf[]`, ~90 KB total) from `assets/ttf/*.ttf`. Because the arrays are `const`, they reside in **XIP Flash (`.rodata`)** on RP2040/RP2350 and other MCUs — consuming **0 bytes of SRAM** — and feed directly into `sft_init_mem()` with zero heap copies.

---

## 5. Phase 3 — Mathematical & Visual Design of the 6 Modes

All 6 modes are driven by a single compact `const MatrixModeSpec` table (`src/media/matrix/matrix_modes.h`) — no duplicated rendering pipelines.

| # | Mode ID | Upstream URL | Font | Visual & Mathematical Signature |
| :-: | :--- | :--- | :--- | :--- |
| 1 | `MATRIX_MODE_TRINITY` | `?version=trinity` | `Matrix-Resurrected.ttf` (135 glyphs) | **Volumetric 3-plane parallax** (far 0.6×, mid 0.85×, near 1.15× pre-rasterized glyph tiers), desaturated silvery-emerald / cyan-steel metallic palette, slower meditative fall, luminous specular cursor glints. |
| 2 | `MATRIX_MODE_CLASSIC` | `?version=classic` | `Matrix-Code.ttf` | Iconic 1999/2003 opening titles: deep black -> emerald green (`#003B00` -> `#00FF41`) -> incandescent white-green cursor head (`#D8FFE8`), classic raindrop length & exponential trail decay, active glyph cycling, soft glow halo. |
| 3 | `MATRIX_MODE_RESURRECTIONS` | `?version=resurrections` | `Matrix-Resurrected.ttf` (135 glyphs) | Updated 2021 look: 135-glyph set, brighter cyan-green highlights (`#40FFAA` -> `#E8FFFF`), slightly faster glyph mutation cadence, subtle glint shimmer along active trails. |
| 4 | `MATRIX_MODE_OPERATOR` | `?version=operator` | `Matrix-Code.ttf` | Nebuchadnezzar operator terminal: dense flat 2D grid, flatter contrast curve (less vertical gradient, crisp CRT phosphor green `#00D838`), plus **square Chebyshev ripple wave** ($d = \max(|x - c_x|, |y - c_y|)$) radiating periodically from center or mouse click. |
| 5 | `MATRIX_MODE_MEGACITY` | `?version=megacity` | `Matrix-Code.ttf` (Megacity subset) | *Matrix Revolutions* opening sequence: Megacity glyph subset, taller layered raindrop columns with high density, emerald-to-warm-gold-green highlight curve (`#106020` -> `#88FF55` -> `#FFFFC8`). |
| 6 | `MATRIX_MODE_PRIDE` | `?effect=pride` | `Matrix-Code.ttf` | Classic raindrop dynamics mapped through the **6-stripe Pride flag palette** (`#E40303` Red, `#FF8C00` Orange, `#FFED00` Yellow, `#008026` Green, `#24408E` Blue, `#732982` Violet) via a precomputed per-row stripe LUT (optional subtle diagonal slant). |

---

## 6. Phase 4 — Software Architecture (`./src/media/matrix/`)

Clean, modular, small C99 codebase adhering strictly to NASA JPL Power-of-10 (static/bounded buffers, zero per-frame heap allocations, deterministic Q16.16 fixed-point simulation):

```
src/media/matrix/
├── matrix.h              # Public B-System API (open_matrix_window*, MatrixMode enum)
├── matrix_internal.h     # Shared bounded structs (Atlas, SimState, ModeSpec)
├── matrix_modes.h        # Const data tables for all 6 modes + palettes + glyph sets
├── matrix_ttf_embed.inc  # Const .rodata TTF fallback arrays for MCU / non-FS targets
├── matrix_atlas.c        # libschrift rasterization, glyph flip, 3×3 glow halo, 256-entry COLOR/RGB565 LUTs
├── matrix_sim.c          # Fixed-point (Q16.16) raindrop physics, glyph cycling, Chebyshev ripple, dirty mask
├── matrix_render.c       # Integer-only GDEV (0xAARRGGBB) & RGB565 pixel blitter with dirty-cell culling
├── matrix_app.c          # Native B-System wnd.c window (640×480 default, 'f' fullscreen), μITRON task, About dialog
└── matrix_segui.c        # PicoCalc / SegUI SDK demo adapter (RGB565 band blit via dp_pico_blit_rgb565)
```

### Key Performance & Low-CPU Engineering Details

1. **Pre-Rasterized Glyph Atlas (`matrix_atlas.c`)**:
   - Built **once** on startup, mode switch (only if font or cell size changes), or window resize.
   - Each glyph stores two compact 8-bit buffers of size `cell_w × cell_h`:
     - `core[y * cell_w + x]` — crisp antialiased alpha coverage from `sft_render_buf()`.
     - `glow[y * cell_w + x]` — pre-blurred 3×3 separable box-filter halo computed once at atlas build time.
   - **Zero runtime font rasterization or blur passes needed** for 5 of the 6 modes; even bloom glow is a single pre-baked lookup!
   - For `trinity` (volumetric mode), 3 small scale tiers (e.g., `10×14`, `14×20`, `20×28`) are pre-rasterized into the static atlas arena (~120 KB total).

2. **Precomputed Palette LUTs (`uint32_t lut[64][16]` or `lut[256]`)**:
   - Each mode compiles its color ramp (background -> dim trail -> bright trail -> white-hot cursor) into a tiny `COLOR` (`0xAARRGGBB`) LUT.
   - Inner pixel loop per active cell is pure integer arithmetic:
     ```c
     /* Combined core + bloom intensity -> single LUT fetch */
     uint32_t c_val = (core_alpha * cell_bright) >> 8;
     uint32_t g_val = (glow_alpha * cell_glow)   >> 8;
     uint32_t idx   = c_val + g_val;
     if (idx > 255u) { idx = 255u; }
     dst_row[x] = mode_lut[idx];
     ```
   - For `pride`, `mode_lut` is simply `pride_luts[stripe_band_for_row]` — **identical 4-instruction inner loop**!

3. **Dirty-Cell Culling (`matrix_sim.c` + `matrix_render.c`)**:
   - Each grid cell tracks `prev_glyph`, `prev_bright_q6`, `prev_glow_q6`.
   - On any frame, if `(glyph, bright_q6, glow_q6)` equals the previous frame (true for ~40–60% of cells — mostly black background and static dim cells), **that cell is skipped completely** (zero memory writes).
   - For `trinity` (multi-plane parallax), a fast `rt_memset` / `0xFF000000` clear followed by additive blitting of only active drops across the 3 depth planes keeps writes minimal (~15–25% screen coverage).

4. **Deterministic XorShift32 PRNG + Fixed-Point Q16.16 Physics**:
   - Zero `rand()` / libc state contention; pure `xorshift32` for drop spawn, speed variance, raindrop length, and glyph flicker.
   - Frame-rate independent `dt_ms` derived from B-System's `get_tim(&now)` (clamped to `<= 100 ms` to avoid jump-spikes).

5. **Low-CPU Frame Pacing (`matrix_app.c`)**:
   - Driven by a dedicated μITRON task (`cre_tsk` / `sta_tsk`, priority `10`, matching [glgears.c](file:///Users/tonpa/depot/bitedits/btron/src/media/sdk/glgears.c#L334-L349)) that sleeps via `dly_tsk(16)` (~60 FPS) or `dly_tsk(33)` (~30 FPS configurable) between frames, calling `inval_wnd(wnd)` only when the window is visible and not paused.
   - Optional lightweight title-bar FPS counter (using `btron_render_perf_us()` + `wnd_set_title()`, togglable with `I` key) so performance can be verified live on every target.

### User Interaction, Defaults & Controls
- **Default launch configuration**: `open_matrix_window()` opens a resizable **`640×480`** B-System window in **`MATRIX_MODE_CLASSIC`** (`version=classic`).
- **`f` / `F`**: Toggle fullscreen (saves/restores previous windowed bounds via `mov_wnd` / `rsz_wnd`, matching B-System GL apps).
- **Keys `1`–`6`**: Instant switch between `1: Trinity`, `2: Classic`, `3: Resurrections`, `4: Operator`, `5: Megacity`, `6: Pride`.
- **`Tab` / `Right` / `Left`**: Cycle modes forward/backward.
- **`Space`**: Pause / Resume simulation (drops CPU usage to 0%).
- **`+` / `-`**: Dynamically scale font size (rebuilds `libschrift` atlas cleanly).
- **`B`**: Toggle pre-baked bloom glow halo.
- **`I`**: Toggle FPS / mode HUD in window title bar.
- **Mouse Click**: Triggers a Chebyshev square ripple in `Operator` mode (or spawns a bright drop in other modes).
- **`Esc` / `Q`**: Close screensaver window (or exit fullscreen first if active).

---

## 7. Phase 5 — B-System Desktop & PicoCalc MCU SDK Integration

### 7.1 Common Shell Integration (Desktop + PicoCalc)
Share a single pure-integer mode parser `int matrix_parse_mode(const char *arg)` (accepting `trinity`, `classic`, `resurrections`, `operator`, `megacity`, `pride`, or `1`–`6`; defaults to `MATRIX_MODE_CLASSIC`) across both shells:
- **Common Headless Shell (`src/apps/terminal/shell_core.h` & `src/apps/terminal/shell_core.c`)**:
  - Extend `BtronShellHooks` with `int (*launch_matrix)(int mode);`.
  - Add `matrix [mode]` command to `btron_shell_core_exec()` and list `matrix` in `help` output.
  - On PicoCalc (`src/desktop/industrial/segui/segui_palm.c`), wire `hooks.launch_matrix` to set the requested `MatrixMode` and switch to `PALM_APP_MATRIX` (on `SEGUI_PICO_QUAKE` images, prints a clean `"matrix: not in quake image\n"` notice).
- **Desktop CLU / Terminal (`src/apps/clu/clu.c` & `src/apps/terminal/gterm.c`)**:
  - Register `matrix [trinity|classic|resurrections|operator|megacity|pride]` so desktop shell and PicoCalc shell share identical command syntax.

### 7.2 Desktop / Hosted / Bare-Metal B-System Targets
- **`include/btron/apps.h`**: Declare `__WEAK_APP WND* open_matrix_window(void);`, `open_matrix_window_rect(int x, int y, int w, int h);`, and `open_matrix_window_mode(int mode);`.
- **`src/apps/workbench/global_menu.c`**: Add **Matrix** launcher entry alongside `glgears` / `xmb` / `paint`.
- **`Makefile`**:
  - Add `src/media/matrix/matrix_atlas.c`, `matrix_sim.c`, `matrix_render.c`, `matrix_app.c` to desktop/bare-metal build targets (`posix`, `qemu`, `tkernel`, `arm64`, `ps2`, `uefi`, etc.).
  - Ensure `src/desktop/font/schrift.c` is included on all targets that build `matrix` (now possible everywhere since POSIX/heap/FPU dependencies are removed).
- **`tools/manifest.txt`**: Add `Matrix-Code.ttf` and `Matrix-Resurrected.ttf` entries.

### 7.3 PicoCalc MCU SDK Demo (Regular Non-Quake Image with XMB & GLGears)
Include the Matrix SDK demo in the **regular non-Quake PicoCalc image** alongside `glgears_segui.c` and `xmb_pico.c`, while keeping the Quake PicoCalc image (`PICOCALC_APP=quake`) untouched:

- **Non-FPU Cortex-M0+ Arithmetic Optimality**:
  - **Zero `float` / `double`**: The entire PicoCalc execution path (`matrix_atlas.c` -> `sft_render_q16()`, `matrix_sim.c`, `matrix_render.c`, `matrix_segui.c`) uses **100% 32-bit integer / Q16.16 fixed-point math** — zero `__aeabi_f*` / `__aeabi_d*` soft-float calls.
  - **Zero Inner-Loop Division**: Cortex-M0+ has single-cycle `MULS` and no hardware `SDIV`/`UDIV`, so per-cell simulation, Pride band lookup, decay curves, and RGB565 blitting use only bit-shifts, multiplies, and precomputed LUTs (no `/` or `%` in hot loops).
- **Flash & SRAM Budget Engineering (RP2040: 264 KB SRAM, 2 MB XIP Flash)**:
  - **Embedded TTFs in XIP Flash**: `matrix_ttf_embed.inc` (`const uint8_t[]`) lives strictly in `.rodata` Flash (**0 bytes SRAM**).
  - **Compact MCU Atlas (`-DMATRIX_PICO_PROFILE`)**: Uses 4-bit packed coverage (`2 px/byte`) for an `8×12` (or `10×14`) glyph cell across the active mode's glyph set (~6–10 KB SRAM), rasterized on demand when entering the Matrix app or switching modes via `sft_init_mem()` + `sft_render_q16()` using a compact `SFT_Scratch` (bounded for `16×16` max glyph on MCU).
  - **RGB565 Row-Band Streaming (`matrix_segui.c`)**: Renders one character row at a time into a tiny `uint16_t band[320 * 14]` (~8.75 KB) buffer using precomputed `uint16_t rgb565_lut[256]` and flushes directly to the ST7365P LCD via `dp_pico_blit_rgb565(dst, band, w, h)` (same seam as [glgears_segui.c](file:///Users/tonpa/depot/bitedits/btron/src/media/sdk/glgears_segui.c#L39) and `xmb_pico.c`). On host SegUI capture (`SEGUI_HOST_XMB`), writes directly into `GDEV->pixels`.
  - **Total Pico SRAM overhead**: **< 22 KB** static BSS (zero heap), fitting comfortably alongside `xmb_pico` and `glgears_segui`.
- **SegUI Palm, Common Shell & Pico XMB Wiring**:
  - `include/btron/segui_palm.h` & `src/desktop/industrial/segui/segui_palm.c`: Add `PALM_APP_MATRIX` (guarded by `#if !defined(SEGUI_PICO_QUAKE)`) with `build_matrix()`, `draw_matrix_canvas()`, `key_matrix()` (`1`–`6` / `Left`/`Right` mode switch, `Space` pause, `Enter` ripple, `Esc` back), tick hook, and `shell_core` `launch_matrix` hook.
  - `src/media/xmb/xmb_pico.h` & `src/media/xmb/xmb_pico.c`: Add `XMB_GO_MATRIX` under the Applications/Demos column right next to `XMB_GO_GEARS`, mapped in `xmb_go_palm()` to `PALM_APP_MATRIX`.
- **`Makefile` (PicoCalc section, ~lines 4614–4730)**:
  - Add `src/desktop/font/schrift.c`, `src/media/matrix/matrix_atlas.c`, `src/media/matrix/matrix_sim.c`, `src/media/matrix/matrix_render.c`, and `src/media/matrix/matrix_segui.c` to `PICOCALC_COMMON_C` (and host `SEGUI` test build around line 4291).
  - Add them to the `ifeq ($(PICOCALC_APP),quake)` `$(filter-out ...)` list right alongside `glgears_segui.c` and `xmb_pico.c` so the Quake PicoCalc image retains 100% of its SRAM.

---

## 8. Phase 6 — Verification & Performance Benchmarking Plan

1. **Unit Tests (`verify/tests/test_schrift.c` & `verify/tests/test_matrix.c`)**:
   - **Schrift NASA JPL / MISRA C tests**:
     - Verify `sft_init_mem()` loads both `assets/ttf/Matrix-Code.ttf` and `assets/ttf/Matrix-Resurrected.ttf` with **zero heap allocations**.
     - Verify `sft_lookup()`, `sft_gmetrics()`, `sft_lmetrics()`, and `sft_render_buf()` produce non-empty antialiased bitmaps for every codepoint in all 4 glyph tables.
     - Fuzz/bounds tests: truncated buffers, corrupt `loca`/`glyf`/`cmap` offsets, excessive contour/point counts return clean `-1` without crashes or OOB reads.
     - Existing [test_sps.c](file:///Users/tonpa/depot/bitedits/btron/verify/tests/test_sps.c) continues to pass unchanged.
   - **Matrix Screensaver & Pico SegUI tests**:
     - Headless `GDEV` render test for all 6 modes (`trinity`, `classic`, `resurrections`, `operator`, `megacity`, `pride`): step 120 frames, verify non-black output, verify Pride mode produces all 6 flag hue bands, verify Operator ripple changes brightness cleanly, verify dirty-cell culling matches full redraw bit-for-bit.
     - Host `matrix_segui` RGB565 band-render test (verifies the PicoCalc MCU path + `MATRIX_PICO_PROFILE` 4-bit atlas & XMB navigation `XMB_GO_MATRIX`).
     - **CPU / FPS microbenchmark**: Measure microseconds per frame (`sim_us` + `blit_us`) at `640×480` and `1024×768` to confirm `< 1 ms/frame` (>1,000 theoretical FPS, `< 2%` CPU at 60 FPS).
2. **Cross-Target Build Verification**:
   - Compile with strict flags (`-std=c99 -Wall -Wextra -Werror`) on `posix` host target, run `test_sps`, `test_schrift`, `test_matrix`, `test-xmb-pico`, and verify the regular non-Quake PicoCalc build (and confirm `PICOCALC_APP=quake` still links cleanly with Matrix filtered out).

---

## 9. Phase 7 — Documentation, Footer Reclassification & Automated Screenshots

1. **Rewrite [`apps/Matrix.html`](file:///Users/tonpa/depot/bitedits/btron/apps/Matrix.html)**:
   - Replace the legacy spreadsheet ("表計算") placeholder with the real **Matrix Digital Rain Screensaver & TrueType Font SDK Demo** specification page (matching B-System's bilingual JP/EN specification style and `index.css` / `settings.css` / `apps.css`).
   - Document the 6 ported modes (`trinity`, `classic`, `resurrections`, `operator`, `megacity`, `pride`), clean-room Q16.16 fixed-point `libschrift` TrueType rasterizer (`src/desktop/font/schrift.c`), native `wnd.c`/`GDEV` raw BitBlit vs. B-GL architecture, PicoCalc non-FPU RP2040 SegUI & XMB integration, common shell (`matrix [mode]`) command reference, and full credits to **Jeremy Sachs (`Rezmason/matrix`)** and **Thomas Oltmann (`libschrift`)**.
   - Embed the 3 automated screenshots (`MATRIX_MODE_CLASSIC`, `MATRIX_MODE_TRINITY`, `MATRIX_MODE_PRIDE`) directly in `apps/Matrix.html`.
2. **Move `Matrix` from `TAD` to `Media` in All HTML Footers**:
   - Update every `apps/*.html` (27+ pages) and `settings/*.html` (11 pages) footer to remove `<li><a href="...Matrix.html">Matrix</a></li>` from `<h4>TAD</h4>` and place it under `<h4>Media</h4>` (as a Media / Font SDK Demo alongside `SDK.html`, `XMB.html`, and `Quake.html`).
   - Update `apps/index.html` and `apps/Clarity.html` cross-links where Matrix was previously labeled as spreadsheet ("表計算").
3. **Automated Matrix Screenshot Pipeline (`Makefile` + `tools/capture_matrix.c`)**:
   - Add a standalone, deterministic headless C99 screenshot generator (`tools/capture_matrix.c` + `Makefile` target `capture-matrix`) that runs the real `libschrift` + `matrix_atlas` + `matrix_sim` + `matrix_render` pipeline for ~90 simulated frames (so rain trails and volumetric depth layers are fully populated) and writes PNGs directly to:
     - `img/screens/Matrix_Classic.png` (`MATRIX_MODE_CLASSIC`)
     - `img/screens/Matrix_Trinity.png` (`MATRIX_MODE_TRINITY`)
     - `img/screens/Matrix_Pride.png` (`MATRIX_MODE_PRIDE`)
   - Wire into `Makefile` so regenerating screenshots is a single reproducible command (`make capture-matrix`).

---

## 10. Locked-In Decisions Summary
- **Embedded TTF fallback**: Enabled (`matrix_ttf_embed.inc` in `.rodata` Flash) for MCU / non-FS targets, with `assets/ttf/*.ttf` also packaged on disk/manifest.
- **Default mode**: `MATRIX_MODE_CLASSIC`.
- **Window & Fullscreen**: Resizable `640×480` B-System window with `'f'` fullscreen toggle (matching GL apps).
- **PicoCalc target**: Included in the regular non-Quake PicoCalc image alongside `xmb_pico` and `glgears_segui`, using 100% non-FPU Q16.16 fixed-point arithmetic (`sft_render_q16()`), and filtered out of `PICOCALC_APP=quake`.
- **Common Shell**: `matrix [mode]` registered in shared `shell_core.c` (available on PicoCalc Terminal), desktop `clu.c`/`gterm.c`, and Pico XMB (`XMB_GO_MATRIX`).
- **Docs & Screenshots**: `apps/Matrix.html` rewritten for Matrix Screensaver / Font SDK Demo, `Matrix` moved to `Media` in all `apps/*.html` & `settings/*.html` footers, and automated PNG capture for `MATRIX_MODE_CLASSIC`, `MATRIX_MODE_TRINITY`, and `MATRIX_MODE_PRIDE`.
