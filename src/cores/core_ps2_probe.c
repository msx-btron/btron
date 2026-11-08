/*
 * BTRON PlayStation 2 Architecture - Diagnostic Probes & Verification Suite
 *
 * This file contains diagnostic probes, hardware rasterizer validators,
 * and automated headless benchmark/census suites extracted from core_ps2.c.
 *
 * Subsystems included:
 *   - Stack high-water & heap integrity probes (ps2_stack_probe_*, ps2_heap_*)
 *   - OHCI USB host controller diagnostic logging (ps2_log_ohci_probe)
 *   - GS primitive rasterization verification (gsquad, gsread, gsalpha, gstex)
 *   - GS hardware race & ink discrimination probes (ps2_bench_gs_race_probe, etc.)
 *   - Keyboard cadence verification (ps2_bench_kbd_cadence)
 *   - Automated headless benchmark/census suite (ps2_bench_*)
 *   - Workbench app frame census & hook (ps2_bench_app_frame_hook)
 */

#include "ps2_heap.h"

typedef struct {
    uint32_t left;
    uint32_t rows;
    uint32_t prof;
    uint32_t pn;
    uint32_t pc[7];
    uint32_t stage[6];
    uint32_t pass_us;
    uint32_t tail_us;
    uint32_t hold_us;
    uint32_t t_end;
    uint32_t r_us, s_us, u_us, px;
    uint32_t stamped;
    uint32_t why;
    int bands, full;
    int bx0, by0, bx1, by1;
    int ndmg;
    RENDER_STATS st;
} ps2_cycle_t;

static ps2_cycle_t s_cycle;

static uint32_t s_gs_groups, s_gs_qw, s_gs_us, s_gs_cover, s_gs_cover_drop, s_gs_cover_merge;
static uint32_t s_gs_empty_stream;
static uint32_t s_gs_plated_passes, s_gs_plated_px;
static eegs_present_stats s_gs_st;

static void ps2_cycle_mark(void)
{
    if (!s_cycle.prof || s_cycle.pn >= 7u) return;
    s_cycle.pc[s_cycle.pn++] = ps2_count_read();
}

static void ps2_cycle_begin(int task_driven)
{
    s_cycle.prof = (s_cycle.left != 0u && task_driven) ? 1u : 0u;
    s_cycle.pn = 0u;
    s_cycle.why = 0u;
    s_cycle.ndmg = 0;
    s_cycle.bx0 = s_cycle.by0 = s_cycle.bx1 = s_cycle.by1 = 0;
    s_gs_groups = s_gs_qw = s_gs_us = 0u;
    s_gs_cover = s_gs_cover_drop = s_gs_cover_merge = 0u;
    s_gs_empty_stream = 0u;
    s_gs_plated_passes = s_gs_plated_px = 0u;
    ps2_cycle_mark();
}

static void ps2_cycle_why(uint32_t bits)
{
    if (s_cycle.prof) s_cycle.why |= bits;
}

static void ps2_cycle_damage(const ps2_bandset_t *set)
{
    if (!s_cycle.prof || set->n == 0u) return;
    s_cycle.bx0 = set->b[0].x0;
    s_cycle.by0 = set->b[0].top;
    s_cycle.bx1 = set->b[0].x1;
    s_cycle.by1 = set->b[0].end;
    s_cycle.ndmg = (int)set->n;
}

static void ps2_cycle_merged(const ps2_bandset_t *set)
{
    if (!s_cycle.prof || s_cycle.why != 0u) return;
    if (ps2_band_is_full(&set->b[0])) s_cycle.why = PS2_CYCLE_WHY_MERGED;
}

static void ps2_cycle_stamp(uint32_t r_us, uint32_t s_us, uint32_t u_us, uint32_t px,
                            int bands, int full)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    const int armed = s_cycle.prof != 0u;
    const int n = (int)s_cycle.pn;
    int i;

    s_cycle.prof = 0u;
    s_cycle.stamped = 0u;
    if (!armed || n < 2 || tpu == 0u) return;
    for (i = 1; i < 7; i++) {
        const int hi = i < n ? i : n - 1;
        const int lo = i - 1 < n ? i - 1 : n - 1;
        s_cycle.stage[i - 1] = (uint32_t)(s_cycle.pc[hi] - s_cycle.pc[lo]) / tpu;
    }
    s_cycle.t_end = s_cycle.pc[n - 1];
    s_cycle.pass_us = (uint32_t)(s_cycle.pc[n - 1] - s_cycle.pc[0]) / tpu;
    s_cycle.r_us = r_us; s_cycle.s_us = s_us; s_cycle.u_us = u_us;
    s_cycle.px = px; s_cycle.bands = bands; s_cycle.full = full;
    btron_render_stats_take(&s_cycle.st);
    s_cycle.stamped = 1u;
}

static void ps2_cycle_print(int asked_ms)
{
    const int rst = (int)s_cycle.r_us - (int)s_cycle.st.comp_us - (int)s_cycle.st.overlay_us;

    ps2_kprintf("[XMBT] cycle %u period=%u us pass=%u tail=%u hold=%u/%d | "
                "poll=%u hook=%u evt=%u net=%u paint=%u note=%u | "
                "render=%u swap=%u upload=%u px=%u nb=%d full=%d why=%x dmg=%d,%d,%d,%d/%d | "
                "comp bg=%u frame=%u paint=%u blit=%u panel=%u bars=%u total=%u ovl=%u rst=%d "
                "bfn=%u wins=%u/%u body=%upx skip=%upx\n",
                (unsigned int)s_cycle.rows,
                (unsigned int)(s_cycle.pass_us + s_cycle.tail_us + s_cycle.hold_us),
                (unsigned int)s_cycle.pass_us, (unsigned int)s_cycle.tail_us,
                (unsigned int)s_cycle.hold_us, asked_ms,
                (unsigned int)s_cycle.stage[0], (unsigned int)s_cycle.stage[1],
                (unsigned int)s_cycle.stage[2], (unsigned int)s_cycle.stage[3],
                (unsigned int)s_cycle.stage[4], (unsigned int)s_cycle.stage[5],
                (unsigned int)s_cycle.r_us, (unsigned int)s_cycle.s_us,
                (unsigned int)s_cycle.u_us, (unsigned int)s_cycle.px,
                s_cycle.bands, s_cycle.full, (unsigned int)s_cycle.why,
                s_cycle.bx0, s_cycle.by0, s_cycle.bx1, s_cycle.by1, s_cycle.ndmg,
                (unsigned int)s_cycle.st.bg_us, (unsigned int)s_cycle.st.frame_us,
                (unsigned int)s_cycle.st.paint_us, (unsigned int)s_cycle.st.blit_us,
                (unsigned int)s_cycle.st.panel_us, (unsigned int)s_cycle.st.bars_us,
                (unsigned int)s_cycle.st.comp_us, (unsigned int)s_cycle.st.overlay_us,
                rst, (unsigned int)s_cycle.st.bg_full_calls,
                (unsigned int)s_cycle.st.wins_drawn, (unsigned int)s_cycle.st.wins_walked,
                (unsigned int)s_cycle.st.frame_body_px,
                (unsigned int)s_cycle.st.frame_body_skip_px);
}

static uint32_t ps2_cycle_hold_begin(void)
{
    return s_cycle.stamped ? ps2_count_read() : 0u;
}

static void ps2_cycle_hold_end(uint32_t t0, int asked_ms)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    if (t0 == 0u || !s_cycle.stamped || tpu == 0u) return;
    s_cycle.tail_us = (uint32_t)(t0 - s_cycle.t_end) / tpu;
    s_cycle.hold_us = (uint32_t)(ps2_count_read() - t0) / tpu;
    ps2_cycle_print(asked_ms);
}

/* ── Part 1: Stack & Heap Diagnostic Probes ───────────────────────────── */
/* Walk the pool the way the allocator's own invariants require: blocks in address
 * order, none overlapping, no two adjacent ones free (coalescing must have merged
 * them), and their sizes plus their headers accounting for every byte of the array.
 * A free list that breaks is worse than a bump pointer that leaks -- it hands out
 * one block twice -- so the bench asks this after each app open rather than trust
 * that the run did not crash.  Returns NULL when the pool holds. */
#if BTRON_PS2_BENCH
__attribute__((unused)) static const char *ps2_heap_check(void)
{
    size_t covered = 0;
    const ps2_blk *prev = NULL;
    unsigned n = 0;

    if (!s_heap_list) return NULL;
    if ((const uint8_t *)s_heap_list != s_ps2_heap) return "first block is not the array";

    for (const ps2_blk *b = s_heap_list; b; prev = b, b = b->next) {
        const uint8_t *self = (const uint8_t *)b;

        if (++n > PS2_HEAP_BLOCKS_MAX) return "more blocks than the pool can hold (cycle)";
        /* Before the arithmetic below, whose bounds would wrap on a corrupt size. */
        if (b->size > PS2_HEAP_SIZE) return "block larger than the whole pool";
        if (self < s_ps2_heap || self + PS2_HEAP_HDR + b->size > s_ps2_heap + PS2_HEAP_SIZE)
            return "block outside the array";
        if (b->size % PS2_HEAP_ALIGN) return "block size not a multiple of the alignment";
        if (prev) {
            /* Equality is the normal case: a block begins where the one below it ends. */
            if (self < (const uint8_t *)prev + PS2_HEAP_HDR + prev->size) return "blocks overlap";
            if (!prev->used && !b->used) return "two adjacent free blocks never coalesced";
        }
        covered += PS2_HEAP_HDR + b->size;
    }
    if (covered != PS2_HEAP_SIZE) return "block sizes do not account for the pool";
    return NULL;
}

/* The list itself, for the row after a check that says something is wrong: where
 * each block starts, how big it is, and whether it is spoken for. */
__attribute__((unused)) static void ps2_heap_dump(void)
{
    unsigned n = 0;

    for (const ps2_blk *b = s_heap_list; b && n < 40u; b = b->next, n++)
        ps2_kprintf("[HEAP]  b%-2d 0x%x %u B %s\n", n, (unsigned int)(uintptr_t)b,
                    (unsigned int)b->size, b->used ? "used" : "free");
}
#endif

/* Stack high-water, because the pool's ceiling is the stack's floor.
 *
 * ps2.ld's ASSERT keeps `_end` below boot_ps2.s's $sp (0x01FF0000), and this pool is the bulk of
 * `_end`, so every kB the pool gains is a kB the deepest call chain loses.  "It linked" proves
 * nothing about that: an image can leave 400 kB of stack and still die in the XMB's own recursion.
 * So arm() paints the memory below the first C frame with a word nothing else writes, and depth()
 * reports how much of it came back dirty.  Then the pool question is arithmetic, not a guess. */
static uint32_t *s_stack_probe;       /* lowest painted word; NULL until arm() runs */

static void ps2_stack_probe_arm(void)
{
    uintptr_t frame = (uintptr_t)__builtin_frame_address(0) & ~(uintptr_t)(PS2_HEAP_ALIGN - 1u);
    uintptr_t lo;
    uint32_t *w;

    if (frame < PS2_STACK_PROBE_BYTES + 0x10000u) return;   /* nowhere sane to paint: give up quietly */
    lo = frame - PS2_STACK_PROBE_BYTES;
    s_stack_probe = (uint32_t *)(void *)lo;
    for (w = s_stack_probe; (uintptr_t)w < frame; w++) *w = PS2_STACK_PROBE_MARK;
}

/* kB of the probed region a call has written since arm(); 0 when never armed or never touched.
 * Saturating at PS2_STACK_PROBE_BYTES is the row that says "widen the probe", not "the stack fits"
 * -- so the caller prints the probe size next to it. */
static unsigned ps2_stack_probe_depth(void)
{
    uint32_t * const end = s_stack_probe ? s_stack_probe + (PS2_STACK_PROBE_BYTES / 4u) : NULL;
    uint32_t *w;

    if (!s_stack_probe) return 0u;
    for (w = s_stack_probe; w < end; w++) {
        if (*w != PS2_STACK_PROBE_MARK)
            return (unsigned)((PS2_STACK_PROBE_BYTES - (uintptr_t)((char *)w - (char *)s_stack_probe)) / 1024u);
    }
    return 0u;
}

/* ── Part 2: OHCI USB Hardware Diagnostic Logging ─────────────────────── */
static void ps2_log_ohci_probe(const ps2_ohci_probe_t *p)
{
    ps2_kprintf("[USB] ohci@0x%08x rev=0x%02x ports=%u fmInt=0x%08x\n",
                (unsigned int)OHCI_BASE_ADDR, p->rev & 0xFF,
                (unsigned int)(p->rh_a & 0x1Fu), p->fminterval);
#if BTRON_HID_TRACE
    /* The rest of this is the controller's own register file before and after the
     * probe touched it -- the rows that answered "can this thing master memory
     * the EE can read", which the verdict line below now carries as its answer.
     * They are one-off at boot, so they cost nothing but console rows, and those
     * are the thing this log has to fit on a screen. */
    ps2_kprintf("[USB] after reset+enable: rev 0x%02x->0x%02x  frames 0x%08x->0x%08x  int 0x%08x/0x%08x\n",
                p->rev & 0xFF, p->rev_enabled & 0xFF, p->fm_before, p->fm_after,
                p->intstatus, p->intenable);
    /* Port status as the probe found it and as it left it.  The connect bit is
     * one no driver write can set, so a controller reset is something this port
     * cannot afford to do once a device is on the bus; this pair is the check
     * that the probe really does leave the ports alone. */
    ps2_kprintf("[USB] rh=0x%x  p1 0x%x>0x%x  p2 0x%x>0x%x\n",
                p->rh_status, p->port1, p->port1_after,
                p->port2, p->port2_after);
    if (p->hcca_iop) {
        /* frame 0xa5a5 means the controller never wrote the HCCA we pointed it
         * at; anything else is its frame counter, stamped by its own bus master. */
        ps2_kprintf("[USB] hcca@0x%06x: frame 0x%04x done 0x%08x intAfter 0x%08x\n",
                    p->hcca_iop, p->hcca_frame, p->hcca_done, p->int_after);
    }
#endif
    ps2_kprintf("[USB] verdict: %s\n", ps2_usb_verdict_name(ps2_usb_verdict()));
}

/* ── Part 2: Pointer Loopback & Calibration Diagnostic Probe ─────────── */
static void ptr_cal_send(int dx, int dy)
{
    ps2_move_mouse(dx, dy);
}

static void ptr_cal_get(int *x, int *y)
{
    *x = s_mouse_x;
    *y = s_mouse_y;
}

static void ptr_cal_set(int x, int y)
{
    ps2_move_mouse(x - s_mouse_x, y - s_mouse_y);
}

static void ptr_cal_row(const char *tag, long v0, long v1, long v2, long v3)
{
    ps2_kprintf("[CAL] %-5s %4d %6d %6d %6d\n", tag,
                (int)v0, (int)v1, (int)v2, (int)v3);
}

static const dp_cal_port_t s_ptr_cal_port = {
    ptr_cal_send, ptr_cal_get, ptr_cal_set,
    PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT,
    ptr_cal_row,
};

static void ps2_ptrcal_probe(void)
{
    ps2_kprintf("[CAL] Pointer loopback: counts in, pixels out (1:1 direct hardware mapping)\n");
    (void)dp_cal_run(&s_ptr_cal_port);
}

/* ── Part 3: Stage 1 GS Primitive & Rasterizer Diagnostic Probes ──────── */
/* ── gsquad: Verification of the GS Hardware Rasterizer Pipeline ──────────────
 *
 * Direct hardware rasterization diagnostic probe for the Graphics Synthesizer.
 * Executes at the interactive shell prompt outside the desktop compositing loop,
 * verifying that the GS primitive rasterization pipeline correctly puts pixels
 * on the displayed framebuffer independently of software presentation layers.
 *
 * Pipeline verification architecture:
 * 1. Drawing Environment:
 *    Applies a 9-register A+D drawing environment setup packet targeting the visible
 *    page (ps2_stage1_gs_env), with origin at (0,0) and scissor covering the full display.
 * 2. Hardware Primitives:
 *    Emits flat SPRITE primitives via REGLIST format, with alpha blending (ABE=0) and
 *    depth testing (ZTE=0) disabled so source colors write directly to VRAM.
 * 3. Color and Endianness Calibration:
 *    Upper rectangle: Opaque RED (0xff0000ff).
 *    Lower rectangle: Opaque BLUE (0xffff0000).
 *    Verifies RGBAQ 32-bit color word mapping and channel ordering.
 * 4. Dual DMA Submission Paths:
 *    Upper RED rect: Dispatched via synchronous DMA transfer (ps2_gs_gif_send).
 *    Lower BLUE rect: Dispatched via asynchronous ring buffer DMA (ps2_gif_post).
 *    Partitions transmission path failures from rasterizer processing issues.
 * 5. Console Flush Hazard Prevention:
 *    Text console output flushes full-width bands to VRAM. To prevent newly printed
 *    shell prompts from overwriting rasterized primitives, text output and screen clears
 *    precede primitive dispatch. */
#define GSQUAD_X0       32
#define GSQUAD_X1       672
#define GSQUAD_RED_Y0   144   /* row 9: below the five printed rows and the prompt's own row */
#define GSQUAD_RED_Y1   320
#define GSQUAD_BLUE_Y0  336
#define GSQUAD_BLUE_Y1  496
#define GSQUAD_RED      0xff0000ffu   /* canvas order: A=ff B=00 G=00 R=ff -- opaque red */
#define GSQUAD_BLUE     0xffff0000u   /* canvas order: A=ff B=ff G=00 R=00 -- opaque blue */

/* ── Reference Test Shapes: GS GIF Packet Encoding Architecture ──────────────
 *
 * Graphics Synthesizer packet encoding on the PS2 supports two primary data modes:
 *
 * 1. FLG=PACKED:
 *    Each register listed in the REGS nibbles occupies one full 128-bit quadword.
 *    Sparse encoding: 64-bit register values are padded to 128 bits.
 *
 * 2. FLG=REGLIST:
 *    Registers are densely packed two per 128-bit quadword.
 *    For untextured primitives (RGBAQ + XYZ2), each loop consumes exactly 2 quadwords
 *    (1 quadword for 2 registers). For textured primitives (RGBAQ + UV + XYZ2),
 *    each loop consumes 3 quadwords (for 4 register slots with padding).
 *
 * Diagnostic calibration shapes verify that GIF decoding and rasterizer kicks
 * operate reliably across all primitive types:
 *
 *   CYAN:    Flat SPRITE using FLG=REGLIST with PRE=0, dense 2 QW/loop stride.
 *            Known reference baseline for hardware 2D rectangle acceleration.
 *   MAGENTA: SPRITE with subpixel corner nudge offsets (-7/+9 in 1/16th pixel fixed-point).
 *            Verifies coordinate conversion and viewport boundary rounding.
 *   GREEN:   Flat TRIANGLE using FLG=REGLIST (3 vertices, 3 QW total).
 *            Verifies 3-vertex rasterization pipeline and edge setup.
 *   YELLOW:  SPRITE constructed using four discrete A+D register writes.
 *            Verifies that A+D mode functions identically for immediate draws.
 *   ORANGE:  Batched multi-loop sprite (NLOOP=2, two adjacent rectangles).
 *            Verifies that the GS hardware correctly loops over dense REGLIST payloads.
 *   VIOLET:  Negative control using multi-register PACKED mode with PRE=1.
 *            Acts as a negative assertion baseline (expected to produce 0 pixels). */
#define GSQUAD_CYAN     0xffffff00u   /* A=ff B=ff G=ff R=00 -- opaque cyan */
#define GSQUAD_MAGENTA  0xffff00ffu   /* A=ff B=ff G=00 R=ff -- opaque magenta */
#define GSQUAD_GREEN    0xff00ff00u   /* A=ff B=00 G=ff R=00 -- opaque green */
#define GSQUAD_YELLOW   0xff00ffffu   /* A=ff B=00 G=ff R=ff -- opaque yellow */
#define GSQUAD_ORANGE   0xff0080ffu   /* A=ff B=00 G=80 R=ff -- opaque orange: the two-rect batch */
#define GSQUAD_VIOLET   0xffa020a0u   /* A=ff B=a0 G=20 R=a0 -- opaque violet: the retired shape */
#define GSREF_Y0        544
#define GSREF_Y1        590
#define GSREF_ORA_X0    16    /* the batch's own box, split into two 20-wide rects at 16..36, 36..56 */
#define GSREF_ORA_X1    56
#define GSREF_ORA_SPLIT 36    /* first pixel of the batch's SECOND rect */
#define GSREF_VIO_X0    224   /* rung 4 left this gutter; the negative control lives in it now */
#define GSREF_VIO_X1    240
#define GSREF_CYA_X0    64
#define GSREF_CYA_X1    224
#define GSREF_MAG_X0    240
#define GSREF_MAG_X1    400
#define GSREF_TRI_X0    432   /* the triangle's base row is GSREF_Y1-1 and its apex GSREF_Y0 */
#define GSREF_TRI_X1    512
#define GSREF_TRI_X2    592
#define GSREF_YEL_X0    608   /* the four-register AD control: right of the triangle */
#define GSREF_YEL_X1    768

/* ── Hardware ALPHA Blend Discriminator Layout ──────────────────────────────
 * Evaluates blending equation selection on the GS hardware rasterizer.
 * Displays reference probe boxes over the desktop canvas:
 * - Z: ABE=0 reference (plain unblended sprite with alpha forced; verifies geometry reaches VRAM)
 * - P: ABE=1 under standard live environment packing (CS, CD, AS, CD, fix=0)
 * - H: ABE=1 under alternate hardware register ordering
 * - M: Blend math (Cd - 0) * As >> 7 + Cd (destination brightened by vertex alpha)
 * - X: Blend math (Cs - 0) * FIX + Cd (destination plus source color) */
#define GSALPHA_Y0      200
#define GSALPHA_Y1      240
#define GSALPHA_Z_X0    64
#define GSALPHA_P_X0    176
#define GSALPHA_H_X0    288
#define GSALPHA_M_X0    400   /* Blend: (Cd-0)*As>>7 + Cd (destination brightened by vertex alpha) */
#define GSALPHA_X_X0    512   /* Blend: (Cs-0)*FIX + Cd (additive destination plus source) */
#define GSALPHA_BW      40    /* every box's width; x1 = x0 + GSALPHA_BW */
/* Opaque-ish source, distinct from navy, from console ink (0xFFE0E0E0) and from the read sentinel:
 * R=0 G=255 B=0 in the canvas word's own byte order.  The vertex alpha is NOT 0xFF: a blend of a fully
 * opaque source clamps back to the source (`(Cs-Cd)*255/128 + Cd` saturates to Cs on every channel),
 * which would make a real blend and a bypassed blender read identically.  A mid alpha keeps the three
 * outcomes three colours -- destination, a genuine mid mix, or the raw source -- and that separation is
 * the entire instrument. */
#define GSALPHA_CS      0x0000ff00u
#define GSALPHA_AS      0x40u   /* 64: the blender's multiplier, `(Cs-Cd)*64>>7` = half, so a real mix is mid */

/* ── Hardware Texture Stage Discriminator Layout ────────────────────────────
 * Isolates texture sampling (TEX0/TEX1) and CT32 modulation (TCC=1, TFUNC=MODULATE).
 * Uses uniform single-color texture planes to decouple sampler operation from UV mapping:
 * - U: Untextured reference sprite (positive control confirming geometry dispatch)
 * - R: Textured sprite, white vertex with opaque RED texture plane
 * - B: Textured sprite, white vertex with opaque BLUE texture plane
 *
 * Verifies that the GS texture unit correctly samples and modulates texels
 * rather than bypassing the stage or failing on PSM format mismatches. */
#define GS8_Y0        200
#define GS8_Y1        240
#define GS8_U_X0      64
#define GS8_R_X0      176
#define GS8_B_X0      288
#define GS8_BW        40
#define GS8_PLANE     64u   /* each plane is 64x64 CT32, whole thing one colour so any coord reads it */
#define GS8_VERTEX    0xffffffffu  /* R=G=B=A=255: modulate by 1, so a textured fragment == its texel */

/* Stage 1 GS Drawing Environment Setup:
 * Configures the GS drawing environment targeting the currently displayed page (gs_disp_shown()).
 * Depth buffer is mapped to top VRAM page with testing disabled (TEST=0, ZMSK=1).
 * Window offset is (0,0) and the scissor rectangle covers the full screen (800x600).
 * Alpha blending and depth testing are bypassed so primitive fragments write directly. */
static void ps2_stage1_gs_env(gs_draw_env *env)
{
    const gs_disp_page_t *pg = gs_disp_shown();

    env->frame_byte  = pg->byte;
    env->frame_px_w  = pg->fbw * 64u;
    env->frame_psm   = pg->psm;
    env->frame_fmsk  = 0u;
    env->zbuf_byte   = GS_VRAM_BYTES - GS_PAGE_FRAME;
    env->zbuf_psm    = GS_PSMZ32;
    env->zbuf_zmsk   = 1u;
    env->win_x = 0u;
    env->win_y = 0u;
    env->clip_x0 = 0u;
    env->clip_y0 = 0u;
    env->clip_x1 = (uint32_t)(PS2_SCREEN_WIDTH - 1);
    env->clip_y1 = (uint32_t)(PS2_SCREEN_HEIGHT - 1);
    env->reg_test  = 0u;
    env->reg_alpha = 0u;
    env->reg_clamp = 0u;
    env->reg_tex0  = 0u;
    env->reg_tex1  = 0u;
}

static void ps2_gsquad_probe(void)
{
    /* One tag pair plus GS_POSTER_REGS_N data quadwords, 16-byte aligned because the DMA reads
     * quadwords.  Sized by the emitter's own macro and never by a literal, so the packet cannot be
     * longer than the buffer that holds it.  The same static is reused for the second rect, and it
     * may be: the first send is synchronous, and the second is reaped before this returns, so no
     * transfer is ever reading a buffer a later call is rewriting. */
    static uint64_t s_pkt[2u * GS_POSTER_QWC(1u)] __attribute__((aligned(16)));
    gs_poster_rect r;
    gs_draw_env env;
    ps2_gif_stats_t st;
    int32_t qw_red, qw_blue;

    /* The canvas is repainted navy here, in front of everything, because the repaint is the point:
     * it is the last full-canvas upload this probe allows itself, and it leaves the page in the one
     * state where a red block has no rival explanation.  See the ordering law above. */
    ps2_gs_text_clear();

    r.x0 = GSQUAD_X0;  r.y0 = GSQUAD_RED_Y0;
    r.x1 = GSQUAD_X1;  r.y1 = GSQUAD_RED_Y1;

    /* The displayed page, named exactly the way src/gl/eegs_present.c names it: frame page 0 at
     * the width the PCRTC reads it with, the top page of VRAM for a depth buffer that TEST=0 and
     * ZMSK=1 leave untouched, the page origin at (0,0) so the emitter's gs_vertex_coord16() pixels
     * are screen pixels, and a scissor over the whole canvas.  One function holds it because this
     * verb and `gsread` must not be two models of the same page -- a census taken against an
     * environment that differs from the draw it censuses is reading a page nobody painted. */
    ps2_stage1_gs_env(&env);

    qw_red = gs_poster_packet(&r, 1u, GSQUAD_RED, s_pkt, gs_poster_qwc(1u));

    ps2_kprintf("[GSQUAD] two SPRITES at x=%d..%d. This command writes no canvas byte.\n",
                GSQUAD_X0, GSQUAD_X1 - 1);
    ps2_kprintf("[GSQUAD] RED  y=%d..%d 0x%08x -- the synchronous sender\n",
                GSQUAD_RED_Y0, GSQUAD_RED_Y1 - 1, (unsigned int)GSQUAD_RED);
    ps2_kprintf("[GSQUAD] BLUE y=%d..%d 0x%08x -- the LANE 10 ring, drained\n",
                GSQUAD_BLUE_Y0, GSQUAD_BLUE_Y1 - 1, (unsigned int)GSQUAD_BLUE);

    if (qw_red < 0) {
        ps2_kprintf("[GSQUAD] the emitter refused the rect (dropped=%u): nothing was sent\n",
                    (unsigned int)gs_poster_dropped());
        return;
    }

    if (ps2_gs_draw_env_apply(&env) != 0) {
        ps2_kprintf("[GSQUAD] the environment was refused (-1): no draw page, so no sprite is sent\n");
        return;
    }

    /* The receipt, and it is the last thing printed -- the senders follow and nothing prints after
     * them.  What each column can and cannot mean: `posted`/`started` moving says the environment
     * left through the ring, `refused` says a producer was turned away, and none of them says a
     * pixel appeared anywhere.  That is the blocks on the monitor, which is the reason this verb
     * exists at all. */
    ps2_gif_stats(&st);
    ps2_kprintf("[GSQUAD] env on the bus: posted=%u started=%u queued=%u inflight=%u refused=%u sync=%u\n",
                (unsigned int)st.posted, (unsigned int)st.started,
                (unsigned int)st.queued, (unsigned int)st.in_flight,
                (unsigned int)st.refused, (unsigned int)st.sync_sends);
    ps2_kprintf("[GSQUAD] sending now. both=GS draws | RED only=the ring | BLUE only=sync | none=neither\n");

    /* Rung 1's first half: the SPRITE group over the sender every console upload has already used,
     * so the only new thing on the bus is a four-register REGLIST group carried as one loop. */
    ps2_gs_gif_send(s_pkt, (uint32_t)qw_red);

    /* ...and the second half over the path the shipped stream rides.  The one buffer is rebuilt
     * here only because the send above has already completed, and the post below is drained and
     * reaped before returning, so no transfer is ever reading bytes a later call rewrites. */
    r.y0 = GSQUAD_BLUE_Y0;
    r.y1 = GSQUAD_BLUE_Y1;
    qw_blue = gs_poster_packet(&r, 1u, GSQUAD_BLUE, s_pkt, gs_poster_qwc(1u));
    if (qw_blue < 0) {
        ps2_kprintf("[GSQUAD] the emitter refused the BLUE rect: only RED was sent\n");
        return;
    }
    if (ps2_gif_post(s_pkt, (uint32_t)qw_blue) != 0) {
        /* Printed on the ring's own failure path only: at this point the RED block is already on its
         * way and the page is about to be repainted, so the answer this run can still give is
         * "RED, and BLUE was refused", which is a ring-capacity reading rather than a raster one. */
        ps2_kprintf("[GSQUAD] the ring refused the BLUE packet (ring full)\n");
        return;
    }
    (void)ps2_gif_drain();
    (void)ps2_gif_reap();
}

/* ── gsread: VRAM Read-Back and Pixel Telemetry Verification ──────────────────
 *
 * Automated verification of GS rasterization via Local->Host VRAM DMA transfers.
 * Directly reads back rectangular strips from VRAM into EE host memory via VIF1
 * and DMA channel 1, allowing headless verification of drawing operations.
 *
 * Architectural constraints and readback invariants:
 * 1. Ordering and Console Flush Hazard:
 *    No uploads or console writes may occur between the draw and its readback,
 *    as console flushes would overwrite the target VRAM region with text bands.
 * 2. Buffer Poisoning with Sentinel:
 *    Buffers are pre-filled with GSREAD_SENT (0x00FF00FF) prior to transfer.
 *    Any byte matching the sentinel indicates an incomplete or clipped readback.
 * 3. Diagnostic Error Discrimination:
 *    Readback failures discriminate bus stalls, builder errors, and FIFO drains
 *    to separate hardware communication failures from genuine black/blank pixels.
 * 4. Control Baseline:
 *    A control strip samples the background canvas color to confirm that readback
 *    coordinates and color formats match expected desktop state. */
#define GSREAD_X       GSQUAD_X0    /* 32: a multiple of 4, which is CT32's quadword law */
#define GSREAD_W       64
#define GSREAD_H       16
#define GSREAD_RED_Y   208           /* inside RED's 144..319 */
#define GSREAD_BLUE_Y  344           /* inside BLUE's 336..495 */
#define GSREAD_CTRL_Y  512           /* below both: the desktop's own navy */
#define GSREAD_SENT    0x00FF00FFu   /* a word neither the sprites nor the navy background can be */

typedef struct {
    uint32_t match;        /* words that read back as the expected colour */
    uint32_t sent;         /* words the download never touched */
    uint32_t other;        /* words that are neither of those */
    uint32_t first_other;  /* the first of those, so a shift is recognisable as a value */
    uint32_t qw;           /* quadwords the read asked for, or 0 when it failed */
    gs_read_bus_err err;
} gsread_strip_t;

static const char *gsread_bus_name(gs_read_bus_err e)
{
    switch (e) {
    case GS_READ_BUS_OK:       return "ok";
    case GS_READ_BUS_E_ARGS:   return "bad-args";
    case GS_READ_BUS_E_BUSY:   return "bus-busy";
    case GS_READ_BUS_E_BUILD:  return "builder-refused";
    case GS_READ_BUS_E_STR:    return "block-never-finished";
    case GS_READ_BUS_E_FINISH: return "no-finish-receipt";
    case GS_READ_BUS_E_FIFO:   return "vif-fifo-never-drained";
    case GS_READ_BUS_E_PAGE:   return "page-not-decodable";
    default:                   return "data-never-finished";
    }
}

static const char *gsread_law_name(gs_read_err e)
{
    switch (e) {
    case GS_READ_OK:      return "none";
    case GS_READ_E_ARGS:  return "args";
    case GS_READ_E_PSM:   return "psm";
    case GS_READ_E_SIZE:  return "RRW/RRH";
    case GS_READ_E_QWC:   return "over-16-bit-count";
    case GS_READ_E_POS:   return "SSAX/SSAY";
    case GS_READ_E_ALIGN: return "quadword-alignment";
    case GS_READ_E_PITCH: return "SBW/row-end";
    default:              return "page-extent";
    }
}

/* What the page being scanned answers for a canvas word.
 *
 * A census compares whole 32-bit words, and on a PSMCT16 display page the word that comes back is
 * the expansion of five bits per channel, not the word the CPU wrote: the full-scale primaries are
 * exact both ways -- which is why RED, BLUE, CYAN, MAGENTA, GREEN and YELLOW have always matched --
 * but the console's navy is not (its green field is 0x1B, and 27 of 8 bits becomes 24 of 5 then
 * 24 again), and neither are ORANGE or VIOLET.  Asking a half-width page for the canvas's own words
 * would count a correctly-drawn screen as `other=` and report a broken screen where the expectation
 * is what broke, so every expectation goes through here.  On a 32-bit page -- the default, and the
 * shipped path -- this is the identity.
 *
 * The phase is x=0,y=0 because the dither matrix's cell there is 4, the value the no-dither branch
 * uses, so the answer is the plain truncation and the centre of the spread rather than one of its
 * edges.  A dithered page does NOT hold one word across a flat field: ±4 around the centre tips
 * some of the sixteen phase cells into the neighbouring 5-bit value, so on a 16-bit page with the
 * dither on, `navy=` counts the pixels that land on the centre phase and the rest show up as
 * `other=` of a colour one step away.  That is what an error-diffused 16-bit background is, and
 * `page dither 0` is the dial that removes it when the number has to mean pixels. */
static uint32_t gsread_expect(uint32_t canvas_word)
{
    const gs_disp_page_t *pg = gs_disp_shown();

    if (pg == NULL || pg->bytes_pp != 2u) return canvas_word;
    return gs_unpack_5551(gs_pack_5551(canvas_word, 0u, 0u, ps2_gs_dither()));
}

/* One strip out of page 0 and one row about it.  The fields are stored into *out one at a time rather
 * than by assigning a finished struct: a struct assignment in a hard-float TU of this port has put
 * sdc1/ldc1 -- illegal on the R5900 -- into the image before, and `make test-ps2` is what catches it.
 * Nothing prints between the fill and the read, and a failed read reports a stage, not a colour. */
static void ps2_gsread_strip(const char *label, int y, uint32_t want,
                             uint32_t *buf, uint32_t n, gsread_strip_t *out)
{
    uint32_t i;
    uint32_t t0;
    int32_t got;

    /* Asked in canvas words, compared in the page's own: see gsread_expect().  The row below prints
     * this transformed value, so a `want=` on a 16-bit page is the number the read can actually
     * match rather than the one the canvas holds. */
    want = gsread_expect(want);

    for (i = 0; i < n; i++) buf[i] = GSREAD_SENT;

    t0 = btron_render_perf_us();
    got = ps2_gs_vram_read(GSREAD_X, y, GSREAD_W, GSREAD_H, buf);
    out->err = ps2_gs_vram_read_err();
    out->qw = (got > 0) ? (uint32_t)got : 0u;

    if (out->err != GS_READ_BUS_OK) {
        out->match = 0u;
        out->sent = n;
        out->other = 0u;
        out->first_other = 0u;
        ps2_kprintf("[GSREAD] %s y=%d FAILED: %s", label, y, gsread_bus_name(out->err));
        if (out->err == GS_READ_BUS_E_BUILD) {
            ps2_kprintf(" (law: %s)", gsread_law_name(gs_read_last_err()));
        }
        ps2_kprintf("\n");
        return;
    }

    out->match = 0u;
    out->sent = 0u;
    out->other = 0u;
    out->first_other = 0u;
    for (i = 0; i < n; i++) {
        if (buf[i] == want) {
            out->match++;
        } else if (buf[i] == GSREAD_SENT) {
            out->sent++;
        } else {
            if (out->other == 0u) out->first_other = buf[i];
            out->other++;
        }
    }
    ps2_kprintf("[GSREAD] %s y=%-4d want=0x%08x match=%u/%u sent=%u other=%u first=0x%08x %u us %u qw\n",
                label, y, (unsigned int)want, (unsigned int)out->match, (unsigned int)n,
                (unsigned int)out->sent, (unsigned int)out->other,
                (unsigned int)out->first_other,
                (unsigned int)(btron_render_perf_us() - t0), (unsigned int)out->qw);
}

/* ── Full Display Page Census in Multi-Row Bands ─────────────────────────────
 *
 * Performs a comprehensive scan of the complete 800x600 CT32 display page in VRAM.
 * Partitions the screen into four 150-row bands (30,000 quadwords each) to stay
 * within the 16-bit DMA channel quadword counter limit (CH_QWC < 65,536).
 *
 * Classifies all 480,000 pixels across standard categories:
 * - Desktop navy background
 * - Incomplete transfer sentinels
 * - Console glyphs (rows 0..143)
 * - Calibration shape colors (CYAN, MAGENTA, GREEN, YELLOW, ORANGE, VIOLET)
 * - Upper RED and lower BLUE test rectangles
 * - Unidentified pixels with bounding box tracking */
#define GSREAD_BAND_ROWS 150u   /* four bands over the 600 rows, and 480,000 bytes of buffer */
#define GSREAD_BAND_QWC  (((uint32_t)PS2_SCREEN_WIDTH * GSREAD_BAND_ROWS * 32u) >> 7)
#define GSREAD_RED_PX    ((uint32_t)(GSQUAD_X1 - GSQUAD_X0) * (uint32_t)(GSQUAD_RED_Y1 - GSQUAD_RED_Y0))
#define GSREAD_BLUE_PX   ((uint32_t)(GSQUAD_X1 - GSQUAD_X0) * (uint32_t)(GSQUAD_BLUE_Y1 - GSQUAD_BLUE_Y0))
#define GSREAD_TEXT_ROWS ((uint32_t)GSQUAD_RED_Y0)   /* at or below this row, not-navy is glyphs */
#define GSREAD_CYAN_PX   ((uint32_t)(GSREF_CYA_X1 - GSREF_CYA_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_MAGENTA_PX ((uint32_t)(GSREF_MAG_X1 - GSREF_MAG_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_YELLOW_PX ((uint32_t)(GSREF_YEL_X1 - GSREF_YEL_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_ORANGE_PX ((uint32_t)(GSREF_ORA_X1 - GSREF_ORA_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_VIOLET_PX ((uint32_t)(GSREF_VIO_X1 - GSREF_VIO_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
/* One rect of the batch, derived from where the batch's SECOND rect starts and not from
 * GSREAD_ORANGE_PX / 2: the reading this names is "the group's first loop rasterised", so the
 * expectation has to be the first rect's own area, and a split that moved would otherwise move the
 * expectation with it.  The two rects are equal by construction here, so the two spellings agree. */
#define GSREAD_ORANGE_ONE ((uint32_t)(GSREF_ORA_SPLIT - GSREF_ORA_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
/* base x height / 2, i.e. the area the three vertices enclose -- an expectation to read a count
 * against, not a bit-exact promise about a rasteriser's half-pixel edges. */
#define GSREAD_GREEN_PX  ((uint32_t)(GSREF_TRI_X2 - GSREF_TRI_X0) * \
                          (uint32_t)(GSREF_Y1 - GSREF_Y0 - 1) / 2u)

typedef struct {
    uint32_t navy, sent, text_other, below_other;
    uint32_t red, blue, first_below;
    uint32_t cyan, magenta, green, yellow;   /* rung 4: REGLIST / + nudge / TRIANGLE / PACKED+AD */
    uint32_t orange;      /* the shipping emitter at NLOOP 2: the batch's two rects */
    uint32_t violet;      /* the retired PACKED/NREG 4 shape, which must produce no pixel */
    uint32_t qw, us;
    int red_box[4];          /* x0 y0 x1 y1, all -1 when the count is 0 */
    int blue_box[4];
    int cyan_box[4];
    int magenta_box[4];
    int green_box[4];
    int yellow_box[4];
    int orange_box[4];
    int violet_box[4];
    int below_box[4];
    gs_read_bus_err err;
} gsread_page_t;

static void gsread_box(int *box, uint32_t x, uint32_t y)
{
    if (box[0] < 0 || (int)x < box[0]) box[0] = (int)x;
    if (box[1] < 0 || (int)y < box[1]) box[1] = (int)y;
    if ((int)x > box[2]) box[2] = (int)x;
    if ((int)y > box[3]) box[3] = (int)y;
}

/* Every field stored one at a time, as the strip helper's note says: this TU is compiled -mhard-float
 * and a struct assignment here has put sdc1/ldc1 -- illegal on the R5900 -- into the image before. */
static void gsread_page_census(uint32_t *buf, uint32_t navy, gsread_page_t *o)
{
    const uint32_t width = (uint32_t)PS2_SCREEN_WIDTH;
    const uint32_t height = (uint32_t)PS2_SCREEN_HEIGHT;
    uint32_t y_start, i, total = 0u;
    uint32_t t0;
    /* The nine references, asked in the page's own words.  gsread_expect() is the identity on a
     * 32-bit page -- the default, and every run that has ever printed these rows -- and on a 16-bit
     * one it is the decode the read performs, so `navy=`/`orange=` count what the census can match
     * instead of what the canvas holds. */
    uint32_t exp_red, exp_blue, exp_cyan, exp_magenta, exp_green, exp_yellow;
    uint32_t exp_orange, exp_violet;

    navy = gsread_expect(navy);           /* the caller handed us the canvas's word */
    exp_red = gsread_expect(GSQUAD_RED);
    exp_blue = gsread_expect(GSQUAD_BLUE);
    exp_cyan = gsread_expect(GSQUAD_CYAN);
    exp_magenta = gsread_expect(GSQUAD_MAGENTA);
    exp_green = gsread_expect(GSQUAD_GREEN);
    exp_yellow = gsread_expect(GSQUAD_YELLOW);
    exp_orange = gsread_expect(GSQUAD_ORANGE);
    exp_violet = gsread_expect(GSQUAD_VIOLET);

    o->navy = 0u; o->sent = 0u; o->text_other = 0u; o->below_other = 0u;
    o->red = 0u;  o->blue = 0u;  o->first_below = 0u;
    o->cyan = 0u; o->magenta = 0u; o->green = 0u; o->yellow = 0u; o->orange = 0u;
    o->violet = 0u;
    o->qw = 0u;   o->us = 0u;    o->err = GS_READ_BUS_OK;
    for (i = 0u; i < 4u; i++) {
        o->red_box[i] = -1; o->blue_box[i] = -1; o->below_box[i] = -1;
        o->cyan_box[i] = -1; o->magenta_box[i] = -1;
        o->green_box[i] = -1; o->yellow_box[i] = -1; o->orange_box[i] = -1;
        o->violet_box[i] = -1;
    }

    t0 = btron_render_perf_us();
    for (y_start = 0u; y_start < height; y_start += GSREAD_BAND_ROWS) {
        const uint32_t rows = (height - y_start < GSREAD_BAND_ROWS)
                                ? (height - y_start) : GSREAD_BAND_ROWS;
        int32_t got;

        for (i = 0u; i < width * rows; i++) buf[i] = GSREAD_SENT;
        got = ps2_gs_vram_read(0, (int)y_start, (int)width, (int)rows, buf);
        o->err = ps2_gs_vram_read_err();
        if (o->err != GS_READ_BUS_OK) {
            ps2_kprintf("[GSREAD] page y=%u FAILED: %s", (unsigned int)y_start,
                        gsread_bus_name(o->err));
            if (o->err == GS_READ_BUS_E_BUILD)
                ps2_kprintf(" (law: %s)", gsread_law_name(gs_read_last_err()));
            ps2_kprintf("\n");
            return;
        }
        o->qw += (uint32_t)got;
        total += width * rows;

        for (i = 0u; i < width * rows; i++) {
            const uint32_t v = buf[i];
            uint32_t x, y;

            if (v == navy)           { o->navy++; continue; }
            if (v == GSREAD_SENT)    { o->sent++; continue; }
            x = i % width;
            y = y_start + i / width;
            if (v == exp_red)   { o->red++;     gsread_box(o->red_box, x, y);   continue; }
            if (v == exp_blue)  { o->blue++;    gsread_box(o->blue_box, x, y);  continue; }
            if (v == exp_cyan)    { o->cyan++;    gsread_box(o->cyan_box, x, y);    continue; }
            if (v == exp_magenta) { o->magenta++; gsread_box(o->magenta_box, x, y); continue; }
            if (v == exp_green)   { o->green++;   gsread_box(o->green_box, x, y);   continue; }
            if (v == exp_yellow)  { o->yellow++;  gsread_box(o->yellow_box, x, y);  continue; }
            if (v == exp_orange)  { o->orange++;  gsread_box(o->orange_box, x, y);  continue; }
            if (v == exp_violet)  { o->violet++;  gsread_box(o->violet_box, x, y);  continue; }
            if (y < GSREAD_TEXT_ROWS) { o->text_other++; continue; }
            if (o->below_other == 0u) o->first_below = v;
            o->below_other++;
            gsread_box(o->below_box, x, y);
        }
    }
    o->us = btron_render_perf_us() - t0;

    ps2_kprintf("[GSREAD] page %ux%u: navy=%u/%u text_other=%u below_other=%u first=0x%08x "
                "sent=%u %u us %u qw\n",
                (unsigned int)width, (unsigned int)height,
                (unsigned int)o->navy, (unsigned int)total,
                (unsigned int)o->text_other, (unsigned int)o->below_other,
                (unsigned int)o->first_below, (unsigned int)o->sent,
                (unsigned int)o->us, (unsigned int)o->qw);
    ps2_kprintf("[GSREAD] page hunts: red=%u/%u bbox=%d,%d..%d,%d blue=%u/%u bbox=%d,%d..%d,%d "
                "other_bbox=%d,%d..%d,%d\n",
                (unsigned int)o->red, (unsigned int)GSREAD_RED_PX,
                o->red_box[0], o->red_box[1], o->red_box[2], o->red_box[3],
                (unsigned int)o->blue, (unsigned int)GSREAD_BLUE_PX,
                o->blue_box[0], o->blue_box[1], o->blue_box[2], o->blue_box[3],
                o->below_box[0], o->below_box[1], o->below_box[2], o->below_box[3]);
    /* Two rows rather than one because each control's box is part of its answer: a count that landed
     * in the wrong rows would otherwise read as the branch that says nothing landed.  They are two
     * rows and not one long one because the console wraps at 100 columns, and a wrapped row is a row
     * of ink at y 144 -- inside RED's own rows, in the one bucket the fork below reads. */
    ps2_kprintf("[GSREAD] page refs: cyan=%u/%u bbox=%d,%d..%d,%d magenta=%u/%u bbox=%d,%d..%d,%d\n",
                (unsigned int)o->cyan, (unsigned int)GSREAD_CYAN_PX,
                o->cyan_box[0], o->cyan_box[1], o->cyan_box[2], o->cyan_box[3],
                (unsigned int)o->magenta, (unsigned int)GSREAD_MAGENTA_PX,
                o->magenta_box[0], o->magenta_box[1], o->magenta_box[2], o->magenta_box[3]);
    ps2_kprintf("[GSREAD] page refs: green=%u/~%u bbox=%d,%d..%d,%d yellow=%u/%u bbox=%d,%d..%d,%d\n",
                (unsigned int)o->green, (unsigned int)GSREAD_GREEN_PX,
                o->green_box[0], o->green_box[1], o->green_box[2], o->green_box[3],
                (unsigned int)o->yellow, (unsigned int)GSREAD_YELLOW_PX,
                o->yellow_box[0], o->yellow_box[1], o->yellow_box[2], o->yellow_box[3]);
    /* ORANGE and VIOLET on rows of their own rather than bolted onto the yellow one, because they are
     * this run's two questions and not rung 4's precedents: ORANGE is the shipping emitter at NLOOP 2,
     * VIOLET is the emitter's retired shape and carries `want=0` in the row because its box area is a
     * ceiling and never an expectation -- a denominator there would read as "736 is the number to hit".
     * Nothing here is at risk of putting ink in a censused row -- this function prints only after every
     * band has been read, and the next run's text_clear() repaints the page -- but each row is still
     * kept inside the console's 100 columns, which is the budget the pre-draw rows are measured against
     * and the reason the boxes are several rows and not one. */
    ps2_kprintf("[GSREAD] page refs: orange=%u/%u bbox=%d,%d..%d,%d (the NLOOP 2 batch)\n",
                (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX,
                o->orange_box[0], o->orange_box[1], o->orange_box[2], o->orange_box[3]);
    ps2_kprintf("[GSREAD] page refs: violet=%u want=0 box=%u bbox=%d,%d..%d,%d (retired shape)\n",
                (unsigned int)o->violet, (unsigned int)GSREAD_VIOLET_PX,
                o->violet_box[0], o->violet_box[1], o->violet_box[2], o->violet_box[3]);

    /* The reading, in the order that keeps an incomplete census from being quoted as a negative
     * result: a page with sentinel left in it is a page this loop did not finish reading, whatever
     * the counts above look like.  After that guard the controls are read as the bits they are, and
     * the shape they used to decide between is decided -- rung 4's four shapes and PRE are findings,
     * this verb's remaining job is to re-measure them and answer the batch.
     *
     *   `retired` is VIOLET, gs_poster_ref_sprite_packed_packet() at PRE=1 -- the packet that shipped
     *     from LANE 9b until rung 5 measured it as drawing nothing at either value of PRE.  It is read
     *     FIRST because it is the only row that tests the read rather than the GS: its expectation is
     *     zero by measurement and not by arithmetic, so a violet pixel anywhere says this census is
     *     reporting something other than the shapes its labels name.
     *   `regl` is the SDK's dense four-register REGLIST list (cyan) and that plus its corner nudge
     *     (magenta); `ad` is the same four registers as four single-register AD writes; `tri` is the
     *     SDK's six-nibble triangle list.  All three landed on every run so far, so each is now a
     *     positive control: what its absence says is that this verb lost a send, not that the GS
     *     changed its mind.
     *   `ship` is RED and BLUE -- gs_poster_packet() itself, the emitter as it now stands, at NLOOP 1
     *     on each of the two senders.  Their silence was the defect and their landing is its repair,
     *     so a run in which they do not fill their rects exactly is a run to re-read the image of.
     *   `batch` is ORANGE, the same emitter with NLOOP 2 in one packet.  It is the open question: the
     *     recorder's groups are three, twelve and up to four thousand loops long, and nothing measured
     *     so far says a multi-loop dense group rasterises past its first loop.
     *
     * The order of the branches is the order of what they buy: the read's own truth, then the
     * controls that say the draw half works, then the emitter, then the batch -- because each branch
     * below the emitter is only quotable if the emitter itself landed. */
    if (o->sent != 0u) {
        ps2_kprintf("[GSREAD] page: INCOMPLETE -- %u words the download never touched, so the rows "
                    "above are not a census of the page\n", (unsigned int)o->sent);
    } else {
        const uint32_t regl = (uint32_t)((o->cyan != 0u) | ((o->magenta != 0u) << 1));
        const uint32_t ad = (uint32_t)(o->yellow != 0u);
        const uint32_t tri = (uint32_t)(o->green != 0u);
        const uint32_t retired = (uint32_t)(o->violet != 0u);
        const uint32_t ship = (uint32_t)((o->red != 0u) | ((o->blue != 0u) << 1));
        const int red_in_box = (o->red == GSREAD_RED_PX && o->red_box[0] == GSQUAD_X0
                                && o->red_box[1] == GSQUAD_RED_Y0
                                && o->red_box[2] == GSQUAD_X1 - 1 && o->red_box[3] == GSQUAD_RED_Y1 - 1);
        const int blue_in_box = (o->blue == GSREAD_BLUE_PX && o->blue_box[0] == GSQUAD_X0
                                 && o->blue_box[1] == GSQUAD_BLUE_Y0
                                 && o->blue_box[2] == GSQUAD_X1 - 1
                                 && o->blue_box[3] == GSQUAD_BLUE_Y1 - 1);

        if (retired != 0u) {
            ps2_kprintf("[GSREAD] page: THE READ IS NOT TELLING THE TRUTH -- %u violet pixels, in a "
                        "box whose shape rung 5 measured as drawing nothing on this GS at either value "
                        "of PRE.  Nothing on this run is evidence about a packet until the violet "
                        "count is 0: read the navy/sent/other rows, then the refused row for a shape "
                        "this verb never sent\n", (unsigned int)o->violet);
        } else if (ship == 0u) {
            ps2_kprintf("[GSREAD] page: THE FIXED EMITTER DRAWS NOTHING -- gs_poster_packet() at NLOOP "
                        "1 leaves no red and no blue anywhere, where CYAN's identical dense shape fills "
                        "its box.  Either this image does not carry the fix -- make test-ps2-gs-poster "
                        "asserts the shipped tag against the retired one -- or the defect is below the "
                        "packet: the environment, the sender, or the GIF->GS path\n");
        } else if (ship != 3u) {
            ps2_kprintf("[GSREAD] page: ONE SENDER DELIVERED AND THE OTHER DID NOT (red=%u blue=%u) -- "
                        "the shape is fine on whichever path landed, so the loss is in the sender the "
                        "recorded eegs stream rides, not in the format\n",
                        (unsigned int)o->red, (unsigned int)o->blue);
        } else if (!red_in_box || !blue_in_box) {
            ps2_kprintf("[GSREAD] page: THE DRAW LANDS WHERE ITS RECT DOES NOT SAY -- both magic "
                        "colours are in page 0, but not at the count and corners their own rects name "
                        "(see the bbox columns).  That is the pitch, the window origin or the scissor, "
                        "and no verdict below this one can be quoted until it is read\n");
        } else if ((regl & 1u) == 0u || ad == 0u || tri == 0u) {
            /* CYAN alone is enough to keep the positive controls alive: MAGENTA differs from it by the
             * nudge and has its own branch at the bottom, so a run that lost only MAGENTA is a run with
             * a nudge finding, not a run with a broken verb. */
            ps2_kprintf("[GSREAD] page: A POSITIVE CONTROL IS EMPTY -- cyan=%u magenta=%u green=%u "
                        "yellow=%u, and every one of those landed on the run that settled the format.  "
                        "This verb's own sends are suspect: read the refused row before quoting any "
                        "count on this page as the GS's answer\n",
                        (unsigned int)o->cyan, (unsigned int)o->magenta,
                        (unsigned int)o->green, (unsigned int)o->yellow);
        } else if (o->orange == GSREAD_ORANGE_PX) {
            ps2_kprintf("[GSREAD] page: BOTH LOOPS OF THE BATCH DREW -- %u/%u orange px, one tag and "
                        "two dense SPRITE loops in the same group, so a multi-loop dense REGLIST group "
                        "rasterises on this GS.  The recorder may ship one group per primitive run, as "
                        "it does today, and the eegs stream's emptiness is NOT a group-length effect\n",
                        (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX);
        } else if (o->orange == GSREAD_ORANGE_ONE) {
            ps2_kprintf("[GSREAD] page: ONLY THE GROUP'S FIRST LOOP RASTERISED -- %u orange px of %u, "
                        "i.e. one of the two rects inside one NLOOP 2 tag.  The format is right and the "
                        "BATCH is not: a loop after the first is dropped somewhere between the tag and "
                        "the raster, so ps2_gs_rec.c must close its group at every vertex and the "
                        "recorded stream's triangles are arriving complete and drawing one apiece\n",
                        (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX);
        } else if (o->orange == 0u) {
            ps2_kprintf("[GSREAD] page: THE BATCH DRAWS NOTHING -- no orange px in a box the same "
                        "emitter fills at NLOOP 1 on both senders.  A two-loop group is inert where a "
                        "one-loop group lands, which is a third finding and not the second one's: read "
                        "the bbox columns and the refused row, then the GS's own receipts, before "
                        "quoting it as why the eegs XMB is empty inside\n");
        } else {
            ps2_kprintf("[GSREAD] page: A PARTIAL BATCH -- %u orange px, which is neither the pair of "
                        "rects (%u) nor one of them (%u) nor none.  Its bbox says which: a count "
                        "between the two is a rect clipped, not a loop dropped\n",
                        (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX,
                        (unsigned int)GSREAD_ORANGE_ONE);
        }

        /* The nudge, read separately because it is orthogonal to every branch above: cyan and
         * magenta differ only by the SDK's START_OFFSET/END_OFFSET, so they can disagree while the
         * group-shape verdict stands.  It is a smaller finding than the shape and a real one: it says
         * a SPRITE whose edges sit exactly on whole pixels can be the shape that draws nothing. */
        if (regl == 1u || regl == 2u) {
            ps2_kprintf("[GSREAD] page: NUDGE -- %s sprite drew and the other did not, so the SDK's "
                        "-7/+9 corner offsets are what makes a SPRITE rasterise here, independent "
                        "of the shape answer above\n",
                        regl == 1u ? "the un-nudged (cyan)" : "the nudged (magenta)");
        }
        /* And the triangle, for the same reason: it shares the sprite's tag and format but not its
         * two-corner loop, so a lone disagreement points at the vertex pipeline rather than at
         * anything the poster emits. */
        if (tri != 0u && regl == 0u) {
            ps2_kprintf("[GSREAD] page: TRIANGLE-DISAGREES -- REGLIST with NREG 6 draws and REGLIST "
                        "with NREG 4 does not, so the fault is in the two-quadword sprite group "
                        "itself, not in the format the recorder would have to adopt\n");
        }
        /* Orthogonal to every branch above and worth its own row even now: ink below the text rows in
         * a class this verb does not own.  The branches read the colours they name; this one reads the
         * page that is not navy, which is what a texture write, a second canvas or another task's
         * upload would look like. */
        if (o->below_other != 0u) {
            ps2_kprintf("[GSREAD] page: FOREIGN INK -- %u pixels below the text rows are none of the "
                        "nine classes this verb draws or clears (first=0x%08x at %d,%d..%d,%d).  "
                        "Whatever verdict is printed above was reached on the named colours only\n",
                        (unsigned int)o->below_other, (unsigned int)o->first_below,
                        o->below_box[0], o->below_box[1], o->below_box[2], o->below_box[3]);
        }
    }
}

static void ps2_gsread_probe(void)
{
    static uint64_t s_pkt[2u * GS_POSTER_QWC(2u)] __attribute__((aligned(16)));
    /* rung 4's controls and rung 5's retired shape share one buffer, and may: each is sent before the
     * next is built, and ps2_gs_gif_send() spins until its own transfer is done.  Sized by the longest
     * of the shapes -- the AD packet and the retired PACKED control are both five quadwords, the dense
     * sprite three and the triangle four -- so no control can be handed a buffer shorter than its
     * packet.  s_pkt is sized by the batch, which is the longest thing this verb builds. */
    static uint64_t s_ref[2u * GS_REF_AD_QWC] __attribute__((aligned(16)));
    static uint32_t s_buf[GSREAD_W * GSREAD_H] __attribute__((aligned(16)));
    const uint32_t n = (uint32_t)GSREAD_W * (uint32_t)GSREAD_H;
    gs_poster_rect r;
    gs_poster_rect ora[2];
    gs_draw_env env;
    gsread_strip_t red, blue, ctrl;
    gsread_page_t page;
    uint32_t *page_buf = 0;
    uint32_t navy;
    uint32_t ref_refused = 0u;   /* bit 1 cyan, 2 magenta, 4 GREEN, 8 yellow, 16 ORANGE, 32 VIOLET */
    uint32_t ora_dropped = 0u;   /* rects gs_poster_packet() refused out of the NLOOP 2 batch */
    int32_t tri[6];
    int32_t qw;

    /* Rung 1's ordering law, inherited whole: this clear is the last full-canvas upload the verb
     * allows itself, and it comes before the draws. */
    ps2_gs_text_clear();

    /* The control's expectation: the canvas's own word at the strip's centre, taken after the clear
     * and before anything is drawn into the canvas -- i.e. the exact word the flush below uploads. */
    navy = ps2_gs_get_framebuffer()[(uint32_t)(GSREAD_CTRL_Y + GSREAD_H / 2)
                                    * (uint32_t)PS2_SCREEN_WIDTH
                                    + (uint32_t)(GSREAD_X + GSREAD_W / 2)];

    r.x0 = GSREAD_X;   r.y0 = GSQUAD_RED_Y0;
    r.x1 = GSQUAD_X1;  r.y1 = GSQUAD_RED_Y1;
    ps2_stage1_gs_env(&env);

    /* Two lines and one line, deliberately: the console is 100 columns x 16-px rows, the page's text
     * is censused as text only above GSREAD_TEXT_ROWS (row 9 = y 144), and the pre-draw prompt, the
     * three strip rows and these three together fill eight of those nine rows.  A third intro line
     * would put ink at y 144..159 -- inside the RED rect's own rows, in the one bucket the fork
     * reads -- so the post-fix wording is compressed rather than grown.  What it says is also shorter
     * now: the shape question that took three rungs to split is a finding, and this run asks one
     * question of the emitter it replaced. */
    ps2_kprintf("[GSREAD] draws the shipping emitter 2x NLOOP 1 (RED sync, BLUE ring) + once at\n");
    ps2_kprintf("[GSREAD] NLOOP 2 (ORANGE), 4 SDK refs, VIOLET=retired shape (want 0), 3 strips.\n");

    qw = gs_poster_packet(&r, 1u, GSQUAD_RED, s_pkt, gs_poster_qwc(1u));
    if (qw < 0) {
        ps2_kprintf("[GSREAD] the emitter refused the RED rect: nothing was drawn, nothing is read\n");
        return;
    }
    if (ps2_gs_draw_env_apply(&env) != 0) {
        ps2_kprintf("[GSREAD] the environment was refused: no draw page, so nothing is read\n");
        return;
    }

    /* rung 3's buffer, asked for HERE rather than at the census: Imalloc() prints a [HEAP] row for a
     * block over its own 96 kB threshold, and a print between a draw and its read is the failure mode
     * this verb exists to avoid.  It is freed at every exit below for the same reason in reverse --
     * the free prints too, and only once the reads are done does that stop mattering. */
    page_buf = (uint32_t *)Imalloc((size_t)GSREAD_BAND_QWC * 16u);

    /* The last row before the reads.  After this it is draws, then reads, then the census rows -- and
     * the reason to be strict is this verb's own failure mode: an upload that follows a draw erases
     * it, so a census printed too early would read back its own prompt and report the page as navy. */
    ps2_kprintf("[GSREAD] sends RED sync, BLUE ring, CYAN/MAG/GREEN/YEL/VIO/ORANGE sync; no print\n");

    ps2_gs_gif_send(s_pkt, (uint32_t)qw);

    r.y0 = GSQUAD_BLUE_Y0;
    r.y1 = GSQUAD_BLUE_Y1;
    qw = gs_poster_packet(&r, 1u, GSQUAD_BLUE, s_pkt, gs_poster_qwc(1u));
    if (qw < 0) {
        ps2_kprintf("[GSREAD] the emitter refused the BLUE rect: reading RED and the control only\n");
    } else if (ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
        ps2_kprintf("[GSREAD] the ring refused the BLUE packet (ring full)\n");
    } else {
        (void)ps2_gif_drain();
        (void)ps2_gif_reap();
    }

    /* ── the four SDK-shaped controls, the batch and the retired shape, one variable each ────────
     *
     * CYAN is the SDK's sprite tag and dense stride with this port's own corner words; MAGENTA is
     * CYAN plus the SDK's -7/+9 sprite nudge; GREEN is the SDK's three-vertex list at the plain page
     * offset; YELLOW is the same four registers delivered as four AD writes, the shape this port's own
     * console uploads use.  All four drew on every run that measured them, which is why they are still
     * sent: a census is only as trustworthy as the rows that agree with what is already known.  They go
     * out the SYNCHRONOUS sender because the sender was rung 1's variable and rung 3 answered it --
     * neither sender delivered the shape of the day -- so re-varying it here would confound the
     * variable that is open now.
     *
     * ORANGE is that variable.  It is gs_poster_packet(), the emitter as it ships, with TWO rects in
     * one group instead of one: one tag, NLOOP 2, four dense quadwords.  RED and BLUE are the same call
     * at NLOOP 1, and they are expected to land now, so ORANGE's count is a reading about loop count
     * and nothing else -- and the recorder's groups are three, twelve and up to four thousand loops
     * long, which is the thing this run cannot infer from any of the others.
     *
     * VIOLET is the packet that used to ship, kept as the instrument's negative control: PRE=1 over
     * four sparse quadwords, which rung 5 measured as drawing nothing at either value of PRE.  Its box
     * is the 16-pixel gutter CYAN and MAGENTA leave between them, and any pixel that comes back from it
     * is a finding about this read rather than about the GS.
     *
     * The geometry is a compile-time constant, so a refusal here is this verb's own bug and not the
     * GS's; it is counted in a mask and said AFTER the reads, because a print inside this window
     * repaints its own row and would erase the draw above it -- the failure mode rung 1's ordering
     * law exists to keep out. */
    r.x0 = GSREF_CYA_X0; r.y0 = GSREF_Y0;
    r.x1 = GSREF_CYA_X1; r.y1 = GSREF_Y1;
    qw = gs_poster_ref_sprite_packet(&r, GSQUAD_CYAN, 0u, s_ref, GS_REF_SPRITE_QWC);
    if (qw < 0) ref_refused |= 1u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    r.x0 = GSREF_MAG_X0; r.x1 = GSREF_MAG_X1;
    qw = gs_poster_ref_sprite_packet(&r, GSQUAD_MAGENTA, 1u, s_ref, GS_REF_SPRITE_QWC);
    if (qw < 0) ref_refused |= 2u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* Six separate stores, not an initializer list: this TU is -mhard-float and a bulk copy of a
     * local aggregate is the shape that has put sdc1/ldc1 -- illegal on the R5900 -- into the image.
     * The same law covers the field-by-field fills below, including the two-rect batch's.  The base
     * corners are GSREF_Y1-1 and the apex GSREF_Y0, so the enclosed area the census reads against
     * GSREAD_GREEN_PX is 160 x 45 / 2. */
    tri[0] = GSREF_TRI_X0;  tri[1] = GSREF_Y1 - 1;
    tri[2] = GSREF_TRI_X2;  tri[3] = GSREF_Y1 - 1;
    tri[4] = GSREF_TRI_X1;  tri[5] = GSREF_Y0;
    qw = gs_poster_ref_tri_packet(tri, GSQUAD_GREEN, s_ref, GS_REF_TRI_QWC);
    if (qw < 0) ref_refused |= 4u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* YELLOW's rect is the right-hand strip of the same row band, and its four VALUES are CYAN's to
     * the bit -- the corners come from the same helper and the colour from the same canvas word -- so
     * a census that has CYAN and YELLOW disagreeing is a statement about the register list's shape
     * and nothing else.  Its expected count is CYAN's: 160 x 46, un-nudged, at 608..767 x 544..589. */
    r.x0 = GSREF_YEL_X0; r.x1 = GSREF_YEL_X1;
    qw = gs_poster_ref_sprite_ad_packet(&r, GSQUAD_YELLOW, 0u, s_ref, GS_REF_AD_QWC);
    if (qw < 0) ref_refused |= 8u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* VIOLET, and the one draw on this page whose answer is known in advance.  The `pre` argument is
     * 1, i.e. the packet exactly as LANE 9b shipped it; with 0 this call would rebuild the packet rung
     * 5 also measured as inert, and one negative control per retired shape is enough for a census. */
    r.x0 = GSREF_VIO_X0; r.x1 = GSREF_VIO_X1;
    qw = gs_poster_ref_sprite_packed_packet(&r, GSQUAD_VIOLET, 1u, s_ref, GS_REF_PACKED_QWC);
    if (qw < 0) ref_refused |= 32u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* ORANGE last among the draws, and through the shipping emitter rather than a reference builder --
     * which is the point: what this verb sends is what the poster and the recorder send.  A dropped
     * rect is read off gs_poster_dropped() the instant the packet is built, because that counter is
     * cleared by the next call and a batch the GS never saw would otherwise be censused as a batch the
     * GS refused. */
    ora[0].x0 = GSREF_ORA_X0; ora[0].y0 = GSREF_Y0;
    ora[0].x1 = GSREF_ORA_SPLIT; ora[0].y1 = GSREF_Y1;
    ora[1].x0 = GSREF_ORA_SPLIT; ora[1].y0 = GSREF_Y0;
    ora[1].x1 = GSREF_ORA_X1; ora[1].y1 = GSREF_Y1;
    qw = gs_poster_packet(ora, 2u, GSQUAD_ORANGE, s_pkt, gs_poster_qwc(2u));
    ora_dropped = gs_poster_dropped();
    if (qw < 0) ref_refused |= 16u;
    else ps2_gs_gif_send(s_pkt, (uint32_t)qw);

    ps2_gsread_strip("red",  GSREAD_RED_Y,  GSQUAD_RED,  s_buf, n, &red);
    ps2_gsread_strip("blue", GSREAD_BLUE_Y, GSQUAD_BLUE, s_buf, n, &blue);
    ps2_gsread_strip("ctrl", GSREAD_CTRL_Y, navy,        s_buf, n, &ctrl);

    if (ctrl.err != GS_READ_BUS_OK) {
        ps2_kprintf("[GSREAD] verdict: NO EVIDENCE -- the control read failed, so nothing here is known\n");
        if (page_buf) Ifree(page_buf);
        return;
    }
    if (ctrl.match != n) {
        /* Said out loud, because every verdict below would otherwise be inferred from a census this
         * row has already disqualifed. */
        ps2_kprintf("[GSREAD] verdict: UNTRUSTED -- control is not the canvas (match=%u/%u",
                    (unsigned int)ctrl.match, (unsigned int)n);
        if (ctrl.sent == n)      ps2_kprintf(", the download wrote NOTHING");
        else if (ctrl.first_other == 0u) ps2_kprintf(", it reads ZERO: the GS returned no data");
        else                     ps2_kprintf(", first other=0x%08x", (unsigned int)ctrl.first_other);
        ps2_kprintf(")\n");
        if (page_buf) Ifree(page_buf);
        return;
    }

    /* Said here rather than among the draws, because by now there is nothing left for a print to
     * erase.  A non-zero mask means this verb built a shape it could not send, so the census's zero
     * for that colour is this file's bug and not the GS's answer -- the one reading of a `refs` row
     * that would otherwise be mistaken for "that shape does not land either".  ORANGE's `dropped` rides
     * the same row because it does the same job the other way round: a non-zero count there means the
     * emitter kept a rect out of the batch, so a 920 px census is one rect refusing to be a square and
     * NOT the group's second loop failing to rasterise -- the reading this verb exists to make. */
    if (ref_refused || ora_dropped) {
        ps2_kprintf("[GSREAD] refs: refused%s%s%s%s%s%s orange dropped=%u -- NOT a GS answer\n",
                    (ref_refused & 1u) ? " CYAN" : "",
                    (ref_refused & 2u) ? " MAGENTA" : "",
                    (ref_refused & 4u) ? " GREEN" : "",
                    (ref_refused & 8u) ? " YELLOW" : "",
                    (ref_refused & 16u) ? " ORANGE" : "",
                    (ref_refused & 32u) ? " VIOLET" : "",
                    (unsigned int)ora_dropped);
    }

    /* rung 3, and only now: the page walk is worthless until the control has proved the read is
     * reading the canvas, and it is worthless twice over if a strip refused -- the strips are the
     * cheap look at where the rects claim to be, the bands are the expensive look at everywhere
     * else, and both go through the same channel with the same preconditions. */
    if (page_buf) gsread_page_census(page_buf, navy, &page);
    else ps2_kprintf("[GSREAD] page: SKIPPED -- the pool refused the %u kB band\n",
                     (unsigned int)((GSREAD_BAND_QWC * 16u) / 1024u));

    /* The strips' own answer, and it is the cheap one: three 64x16 windows at one x, inside RED, inside
     * BLUE, and below both.  Their expectations flipped with the format fix -- both colours are what a
     * repaired emitter owes this run -- so what each branch can still say is which SENDER delivered,
     * which is the column the recorder's ring lives on.  The navy control outranks all of it: a census
     * whose control is not the canvas is not evidence about anything. */
    if (red.err == GS_READ_BUS_OK && red.match == n &&
        blue.err == GS_READ_BUS_OK && blue.match == n) {
        ps2_kprintf("[GSREAD] verdict: BOTH strips full -- the dense emitter rasterised on the "
                    "synchronous sender AND on the LANE 10 ring, so the emptiness is in the stream's "
                    "content and the open page question is ORANGE's loop count\n");
    } else if (red.err == GS_READ_BUS_OK && red.match == n) {
        ps2_kprintf("[GSREAD] verdict: RED only -- the synchronous sender delivered the dense group, "
                    "the LANE 10 ring did not reach the strip, and the recorded stream rides that ring\n");
    } else if (blue.err == GS_READ_BUS_OK && blue.match == n) {
        ps2_kprintf("[GSREAD] verdict: BLUE only -- the ring delivered and the synchronous sender did "
                    "not, which is the reverse of what this port's console uploads do\n");
    } else {
        ps2_kprintf("[GSREAD] verdict: NEITHER strip holds its colour, though the navy control does, so "
                    "the read works -- the page rows above are what say whether the dense emitter landed "
                    "somewhere else or nowhere at all\n");
    }

    /* Last, because the free prints ([HEAP] again, over the same threshold) and a print after the
     * reads is only harmless once there is nothing left to erase. */
    if (page_buf) Ifree(page_buf);
}

/* Rung 28's census of one box, and the arithmetic it is asked to match.
 *
 * The five boxes differ ONLY in the ALPHA word, so the question each answers is "which word came
 * back", and the reading is a tally, not a picture.  `other` is the interesting bucket -- a pixel that
 * is neither the destination nor the raw source is a real mix -- and the two expectations name WHICH
 * mix: with As = 0x40 = 64, a `>>7` divisor predicts dest*3/2 and a `/255` divisor predicts dest*5/4,
 * so the brightening box says both whether the law painted and which scaling this GS applies.
 *
 * The compares are per channel, +-2, and ignore the alpha byte: the stored alpha of a blended
 * fragment is this port's RGBAQ business, not the blend equation's, and rung 28's subject is colour. */
typedef struct {
    uint32_t dest, src, other, m1, m2, first_other;
} gsalpha_box_t;

static uint32_t gsalpha_bright(uint32_t w, uint32_t num, uint32_t den)
{
    uint32_t out = 0u;
    uint32_t i;
    for (i = 0u; i < 24u; i += 8u) {
        uint32_t c = ((w >> i) & 0xffu) * num / den;
        if (c > 255u) c = 255u;
        out |= c << i;
    }
    return out;
}

/* The saturated sum of two canvas words, channel by channel -- the additive wash's prediction. */
static uint32_t gsalpha_add(uint32_t a, uint32_t b)
{
    uint32_t out = 0u;
    uint32_t i;
    for (i = 0u; i < 24u; i += 8u) {
        uint32_t c = ((a >> i) & 0xffu) + ((b >> i) & 0xffu);
        if (c > 255u) c = 255u;
        out |= c << i;
    }
    return out;
}

static int gsalpha_near(uint32_t a, uint32_t b)
{
    uint32_t i;
    for (i = 0u; i < 24u; i += 8u) {
        const int d = (int)((a >> i) & 0xffu) - (int)((b >> i) & 0xffu);
        if (d > 2 || d < -2) return 0;
    }
    return 1;
}

/* Read one box and sort every pixel into destination / raw source / something else, counting how many
 * of the "else" land on each of the two predictions.  -1 when the block did not come back at all,
 * which the caller treats exactly like the three tallies this helper replaced: an unread box is a
 * SKIP, not a zero. */
static int gsalpha_read_box(uint32_t *buf, int x0, uint32_t dest, uint32_t cs_stored,
                            uint32_t want1, uint32_t want2, gsalpha_box_t *t)
{
    const uint32_t n = (uint32_t)GSALPHA_BW * (uint32_t)(GSALPHA_Y1 - GSALPHA_Y0);
    uint32_t i;

    t->dest = t->src = t->other = t->m1 = t->m2 = 0u;
    t->first_other = 0u;

    if (ps2_gs_vram_read(x0, GSALPHA_Y0, GSALPHA_BW, GSALPHA_Y1 - GSALPHA_Y0, buf) <= 0 ||
        ps2_gs_vram_read_err() != GS_READ_BUS_OK)
        return -1;

    for (i = 0u; i < n; i++) {
        const uint32_t v = buf[i];
        if (v == dest)             t->dest++;
        else if (v == cs_stored)   t->src++;
        else {
            if (t->other == 0u) t->first_other = v;
            t->other++;
            if (want1 != 0u && gsalpha_near(v, want1)) t->m1++;
            if (want2 != 0u && gsalpha_near(v, want2)) t->m2++;
        }
    }
    return 0;
}

/* The canvas word under one box, taken AFTER the clear and BEFORE its draw -- the destination the
 * blender will read.  Each box asks about its own ground rather than about Z's, because the boxes are
 * 112 px apart on a canvas this port does not promise is flat; a shared word would make a blend on a
 * sloping background indistinguishable from a no-op. */
static uint32_t gsalpha_dest_at(int x0)
{
    return ps2_gs_get_framebuffer()[(uint32_t)(GSALPHA_Y0 + GSALPHA_Y1) / 2u
                                    * (uint32_t)PS2_SCREEN_WIDTH + x0 + GSALPHA_BW / 2];
}

/* Paint one box: program `env` with this ALPHA word, draw the one ABE-controlled sprite over
 * x0..x0+GSALPHA_BW, and drain the ring so the environment and its draw land in FIFO order and the
 * read's idle precondition holds.  The draws go out the ring (ps2_gif_post) rather than the
 * synchronous sender because the ENV rides the same ring; keeping both on it is what makes "env
 * before its draw" a property of the FIFO instead of a hope about two senders.  Five boxes ran this
 * shape as five copies until rung 28; the only thing that ever differed was the word and ABE. */
static int gsalpha_draw(int x0, uint32_t abe, uint64_t alpha_word, uint64_t *pkt, gs_draw_env *env)
{
    gs_poster_rect r;
    int32_t qw;

    r.y0 = GSALPHA_Y0; r.y1 = GSALPHA_Y1;
    r.x0 = x0;         r.x1 = x0 + GSALPHA_BW;

    env->reg_alpha = alpha_word;
    qw = gs_poster_ref_sprite_blend_packet(&r, GSALPHA_CS, GSALPHA_AS, abe, pkt, GS_REF_SPRITE_QWC);
    if (qw < 0 || ps2_gs_draw_env_apply(env) != 0 || ps2_gif_post(pkt, (uint32_t)qw) != 0)
        return -1;
    (void)ps2_gif_drain();
    (void)ps2_gif_reap();
    return 0;
}

/* ── GS Hardware ALPHA Register Blending Algebra ─────────────────────────────
 *
 * The Graphics Synthesizer fixed-function rasterizer implements the programmable
 * blending equation:
 *     Output = ((Color_A - Color_B) * Alpha_C >> 7) + Color_D
 *
 * Encoded via `gs_pack_alpha(a, b, c, d, fix)` into the 64-bit GS_REG_ALPHA register:
 *   - A[1:0]: Color_A select (0=Cs, 1=Cd, 2=0)
 *   - B[3:2]: Color_B select (0=Cs, 1=Cd, 2=0)
 *   - C[5:4]: Alpha_C select (0=As, 1=Ad, 2=FIX)
 *   - D[7:6]: Color_D select (0=Cs, 1=Cd, 2=0)
 *   - FIX[39:32]: Constant 8-bit alpha scalar (0..255)
 *
 * Standard OpenGL Emulation Equations:
 * 1. Standard Source-Over Blending (`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`):
 *    (Cs - Cd) * As >> 7 + Cd
 *    a=Cs(0), b=Cd(1), c=As(0), d=Cd(1), fix=0
 *
 * 2. Destination Brightening (`GL_DST_COLOR, GL_ONE`):
 *    Used by the 3D surface ribbon mesh to modulate underlying background blue
 *    without requiring per-channel color multiplication:
 *    (Cd - 0) * As >> 7 + Cd == Cd * (1 + As / 128)
 *    a=Cd(1), b=ZERO(2), c=As(0), d=Cd(1), fix=0
 *    Retains background hue while scaling luminance directly by vertex alpha.
 *
 * 3. Additive Wash Fallback:
 *    (Cs - 0) * FIX >> 7 + Cd
 *    a=Cs(0), b=ZERO(2), c=FIX(2), d=Cd(1), fix=255 */
static void ps2_gsalpha_probe(void)
{
    static uint64_t s_pkt[2u * GS_REF_SPRITE_QWC] __attribute__((aligned(16)));
    static uint32_t s_buf[GSALPHA_BW * (GSALPHA_Y1 - GSALPHA_Y0)] __attribute__((aligned(16)));
    const uint32_t cs = GSALPHA_CS;        /* opaque green source, the one place the word is spelled */
    const uint32_t src_alpha = GSALPHA_AS; /* mid As: the blend's multiplier operand, not a stray A byte */
    const uint32_t cs_stored = (src_alpha << 24) | (cs & 0x00ffffffu);
    /* The word an UNBLENDED fragment leaves in CT32: the renderer writes the fragment's RGBAQ
     * straight out, so the destination's alpha byte becomes the fragment's own `src_alpha`.  Comparing
     * the raw source (`cs`, A=00) would count every unblended pixel as a blend; this is the exact word
     * the reference box must come back as. */
    const uint32_t n = (uint32_t)GSALPHA_BW * (uint32_t)(GSALPHA_Y1 - GSALPHA_Y0);
    gs_draw_env env;
    gsalpha_box_t z, p, h, m, x;
    uint32_t zd, pd, hd, md, xd;
    int trusted = 1;

    /* Destinations first, all five, before anything is drawn: the canvas this verb uploaded last is
     * what the blender will read, and the GS draws go to VRAM, not to that mirror. */
    ps2_gs_text_clear();
    zd = gsalpha_dest_at(GSALPHA_Z_X0);
    pd = gsalpha_dest_at(GSALPHA_P_X0);
    hd = gsalpha_dest_at(GSALPHA_H_X0);
    md = gsalpha_dest_at(GSALPHA_M_X0);
    xd = gsalpha_dest_at(GSALPHA_X_X0);

    ps2_kprintf("[GSALPHA] five 40x40 boxes at y=%d..%d, src=0x%08x As=0x%02x\n",
                GSALPHA_Y0, GSALPHA_Y1 - 1, (unsigned int)cs, (unsigned int)src_alpha);
    ps2_kprintf("[GSALPHA] dest: Z=0x%08x P=0x%08x H=0x%08x M=0x%08x X=0x%08x\n",
                (unsigned int)zd, (unsigned int)pd, (unsigned int)hd, (unsigned int)md,
                (unsigned int)xd);
    ps2_kprintf("[GSALPHA] Z=ABE0 ref  P=ABE1 live-word  H=ABE1 hardware-word  "
                "M=ABE1 dst-brighten  X=ABE1 additive; navy=write-back, blend=real mix\n");

    ps2_stage1_gs_env(&env);

    /* Z -- ABE=0 reference, under the live word (ABE=0 ignores it, so this also proves that claim). */
    if (gsalpha_draw(GSALPHA_Z_X0, 0u,
                     gs_pack_alpha(GS_BLEND_CS, GS_BLEND_CD, GS_ALPHA_AS, GS_BLEND_CD, 0u),
                     s_pkt, &env) < 0)
        trusted = 0;

    /* P -- ABE=1 under the word the shipping environment programs. */
    if (trusted &&
        gsalpha_draw(GSALPHA_P_X0, 1u,
                     gs_pack_alpha(GS_BLEND_CS, GS_BLEND_CD, GS_ALPHA_AS, GS_BLEND_CD, 0u),
                     s_pkt, &env) < 0)
        trusted = 0;

    /* H -- ABE=1 under the hardware-order src-over word (multiplier As in [1:0], FIX=128). */
    if (trusted && gsalpha_draw(GSALPHA_H_X0, 1u, gs_pack_alpha(3u, 0u, 1u, 1u, 128u),
                                s_pkt, &env) < 0)
        trusted = 0;

    /* M -- rung 28's destination-brighten: (Cd - 0) * As >> 7 + Cd. */
    if (trusted &&
        gsalpha_draw(GSALPHA_M_X0, 1u,
                     gs_pack_alpha(GS_BLEND_CD, GS_BLEND_ZERO, GS_ALPHA_AS, GS_BLEND_CD, 0u),
                     s_pkt, &env) < 0)
        trusted = 0;

    /* X -- rung 28's additive wash: (Cs - 0) * FIX + Cd with FIX at full. */
    if (trusted &&
        gsalpha_draw(GSALPHA_X_X0, 1u,
                     gs_pack_alpha(GS_BLEND_CS, GS_BLEND_ZERO, GS_ALPHA_FIX, GS_BLEND_CD, 255u),
                     s_pkt, &env) < 0)
        trusted = 0;

    if (!trusted) {
        ps2_kprintf("[GSALPHA] SKIP: a draw or the environment was refused, so the boxes are not all "
                    "on the bus -- nothing here names the ALPHA model\n");
        return;
    }

    /* Census each box into destination / raw source / a real mix, and -- for the two rung-28 boxes --
     * which mix.  A pixel that is neither navy nor the source colour is the whole answer to rung 7;
     * whether that pixel is the destination scaled is the whole answer to rung 28. */
    if (gsalpha_read_box(s_buf, GSALPHA_Z_X0, zd, cs_stored, 0u, 0u, &z) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_P_X0, pd, cs_stored, 0u, 0u, &p) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_H_X0, hd, cs_stored, 0u, 0u, &h) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_M_X0, md, cs_stored,
                         gsalpha_bright(md, 3u, 2u), gsalpha_bright(md, 5u, 4u), &m) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_X_X0, xd, cs_stored,
                         gsalpha_add(xd, cs), 0u, &x) < 0) {
        ps2_kprintf("[GSALPHA] SKIP: a page read failed -- the tallies are not a picture\n");
        return;
    }

    ps2_kprintf("[GSALPHA] Z ref : dest=%u src=%u blend=%u /%u\n",
                (unsigned int)z.dest, (unsigned int)z.src, (unsigned int)z.other, (unsigned int)n);
    ps2_kprintf("[GSALPHA] P live: dest=%u src=%u blend=%u first_blend=0x%08x /%u\n",
                (unsigned int)p.dest, (unsigned int)p.src, (unsigned int)p.other,
                (unsigned int)p.first_other, (unsigned int)n);
    ps2_kprintf("[GSALPHA] H hw  : dest=%u src=%u blend=%u first_blend=0x%08x /%u\n",
                (unsigned int)h.dest, (unsigned int)h.src, (unsigned int)h.other,
                (unsigned int)h.first_other, (unsigned int)n);
    ps2_kprintf("[GS28] M dstmul: dest=%u src=%u blend=%u first=0x%08x x3/2=%u x5/4=%u /%u\n",
                (unsigned int)m.dest, (unsigned int)m.src, (unsigned int)m.other,
                (unsigned int)m.first_other, (unsigned int)m.m1, (unsigned int)m.m2,
                (unsigned int)n);
    ps2_kprintf("[GS28] X add   : dest=%u src=%u blend=%u first=0x%08x add=%u /%u\n",
                (unsigned int)x.dest, (unsigned int)x.src, (unsigned int)x.other,
                (unsigned int)x.first_other, (unsigned int)x.m1, (unsigned int)n);

    if (z.src != n) {
        /* The reference did not paint, so the run measured no pipeline at all. */
        ps2_kprintf("[GSALPHA] SKIP: the ABE=0 reference is not solid source (src=%u/%u) -- the box never "
                    "reached the page, so the blend rows say nothing about the GS\n",
                    (unsigned int)z.src, (unsigned int)n);
        return;
    }

    /* The reference proves the geometry and the read, so the fork on P is a statement about the ALPHA
    ** register and nothing else. */
    if (p.dest == n) {
        ps2_kprintf("[GSALPHA] verdict: HARDWARE FIELD ROLES. The live word writes the destination back "
                    "(P is dest everywhere), so the port's gs_pack_alpha(CS,CD,AS,CD,fix=0) is a NO-OP and "
                    "is the blackout -- a blended fragment paints Cd and looks like no fragment to rung 6. ");
        if (h.other > 0u && h.dest == 0u)
            ps2_kprintf("H blends (%u px) -> the fix is gs_pack_alpha(3,0,1,1,128) at eegs_present.c:457.\n",
                        (unsigned int)h.other);
        else
            ps2_kprintf("H blend=%u dest=%u -- the fix word did NOT blend either, so re-read the "
                        "As path before shipping anything.\n",
                        (unsigned int)h.other, (unsigned int)h.dest);
    } else if (p.other > 0u) {
        ps2_kprintf("[GSALPHA] verdict: PORT FIELD ROLES. The live word blends (P has %u mixed px, none "
                    "left as dest), so eegs_present.c:457 is NOT the blackout -- the stream reaches a real "
                    "pixel and the emptiness must be the cover re-upload after send (core_ps2.c:1456) or a "
                    "prim state no rung varied. Run the re-send probe next.\n",
                    (unsigned int)p.other);
    } else if (p.src == n) {
        ps2_kprintf("[GSALPHA] verdict: ABE had no effect -- P is solid source like the reference, so the "
                    "blend stage was bypassed; the ALPHA register is not on this path and the blackout is "
                    "elsewhere.\n");
    } else {
        ps2_kprintf("[GSALPHA] verdict: MIXED -- P is %u dest / %u src / %u blend, which no single model "
                    "predicts; read the words against the tallies before believing this run.\n",
                    (unsigned int)p.dest, (unsigned int)p.src, (unsigned int)p.other);
    }

    /* Rung 28's own verdict, deliberately after the field-role fork because it is a question about a
     * LAW and not about the encoding: the ribbon needs the destination's brightness to move and its
     * hue to survive.  `blend == n` with the scaled-destination bucket filled says that happened;
     * `src == n` says the word had no effect and Cs was written, and `dest == n` says the box was a
     * no-op.  All three are answers about the grey wave; only the first is a way to fix it. */
    if (m.other == n && (m.m1 > 0u || m.m2 > 0u)) {
        ps2_kprintf("[GS28] verdict: the destination-brighten law PAINTS (%u/%u px are a mix, %u at "
                    "dest*3/2, %u at dest*5/4) and leaves the source colour out of it -- the ribbon's "
                    "dst*(1+src) is expressible as gs_pack_alpha(CD,ZERO,AS,CD,0) once the mesh carries "
                    "its brightness in As.  Divisor: %s\n",
                    (unsigned int)m.other, (unsigned int)n, (unsigned int)m.m1, (unsigned int)m.m2,
                    m.m1 >= m.m2 ? "As/128 (>>7)" : "As/255");
    } else if (x.other == n && x.m1 > 0u) {
        ps2_kprintf("[GS28] verdict: M did not hold the destination scaled (blend=%u, x3/2=%u) but X is "
                    "the destination PLUS the source on %u/%u px -- additive is the law this GS gives a "
                    "blended ribbon, and the wave's hue survives only as a wash.\n",
                    (unsigned int)m.other, (unsigned int)m.m1, (unsigned int)x.other,
                    (unsigned int)n);
    } else {
        ps2_kprintf("[GS28] verdict: neither candidate painted what its equation predicts -- M is "
                    "%u dest / %u src / %u blend (first 0x%08x), X is %u dest / %u src / %u blend "
                    "(first 0x%08x).  The grey wave is not explained by a brightening law this register "
                    "can program; read those words before shipping either.\n",
                    (unsigned int)m.dest, (unsigned int)m.src, (unsigned int)m.other,
                    (unsigned int)m.first_other, (unsigned int)x.dest, (unsigned int)x.src,
                    (unsigned int)x.other, (unsigned int)x.first_other);
    }
}

/* ── gstex: Hardware Texture Sampling & Cache Management Telemetry ──────────
 *
 * Verifies that the GS texture unit correctly samples uploaded VRAM texture pages
 * and applies fixed-function modulation (out = vertex * texel).
 *
 * Test Methodology:
 * Uses a pure white vertex (all channels 0xFF) to reduce modulation arithmetic
 * directly to texel output: out = 1.0 * texel == texel.
 * Single-color uniform 64x64 texture planes isolate sampling from UV mapping bugs.
 *
 * Evaluation Matrix:
 * - Box U: Untextured reference sprite (positive control verifying geometry path).
 * - Box R: Textured sprite (RED plane) with explicit A+D PRIM setup and TEXFLUSH.
 * - Box B: Textured sprite (BLUE plane) with explicit A+D PRIM setup and no TEXFLUSH.
 *
 * Outcomes:
 * 1. Box reads plane color (RED/BLUE): Texture sampler and address translation operate correctly.
 * 2. Box reads white: Texture mapping enabled (TME) was not latched or sampler bypassed.
 * 3. Box reads black: Texel format mismatch (PSM translation failure).
 * 4. Box reads background destination: Fragment rejected by texture alpha or rasterizer.
 *
 * Mid-Stream Texture State Protocol:
 * Texture parameters (CLAMP, TEX0, TEX1) and PRIM must be written via inline A+D
 * GIF packets preceding primitive rendering to ensure GS texture buffer address (TBA)
 * cache reload and reliable TME bit configuration. */
#define GS8_PACKET_QW 9u    /* 6 for the flush AD group (tag + PRIM/CLAMP/TEX0/TEX1/TEXFLUSH pairs), 3 for the sprite */

static int32_t gs8_textured_sprite(const gs_poster_rect *r, uint32_t vertex, const gs_tex_t *t,
                                   uint32_t flush, uint64_t *qw, uint32_t cap_qw)
{
    /* TME=1, ABE=0 (opaque), CTXT=0 (the _1 context these AD writes program).  PRE=1 carries
     * this PRIM in the tag so the register list is exactly the four that pack into two dense
     * quadwords: RGBAQ, ST, XYZ2, XYZ2 -- a sprite kicks on its second XYZ2.  The tag's PRIM is
     * inert on this GS (rung 12u); the AD write of the same word below is what sets TME. */
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 1u, 0u, 0u, 0u);
    static const uint32_t ad_regs[1] = { GS_NIB_AD };
    static const uint32_t sp_regs[4] = { GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2, GS_NIB_XYZ2 };
    uint64_t *c = qw;
    uint32_t nad = 4u + (flush ? 1u : 0u);   /* CLAMP, TEX0, TEX1, PRIM, + TEXFLUSH when asked for */

    if (r == 0 || qw == 0 || t == 0) return -1;
    if (cap_qw < GS8_PACKET_QW) return -1;
    if (r->x1 <= r->x0 || r->y1 <= r->y0 || r->x0 < 0 || r->y0 < 0) return -1;

    /* AD group: NLOOP `nad`, one register each, EOP clear (the sprite loop follows in this same
     * DMA).  PRIM is written last among the state so it is the freshest AD word before the kick;
     * TEXFLUSH, when present, sits between the texture trio and PRIM. */
    *c++ = gs_pack_giftag(nad, 0u, 0u, 0u, GS_FLG_PACKED, 1u);
    *c++ = gs_pack_regs(ad_regs, 1u);
    c = gs_pack_pair(c, t->clamp, GS_GP_CLAMP_1);
    c = gs_pack_pair(c, t->tex0,  GS_GP_TEX0_1);
    c = gs_pack_pair(c, t->tex1,  GS_GP_TEX1_1);
    if (flush) c = gs_pack_pair(c, 1u, GS_GP_TEXFLUSH);
    c = gs_pack_pair(c, prim,     GS_GP_PRIM);

    /* The sprite loop, EOP ends the whole transfer. */
    *c++ = gs_pack_giftag(1u, 1u, 1u, prim, GS_FLG_REGLIST, 4u);
    *c++ = gs_pack_regs(sp_regs, 4u);
    *c++ = gs_pack_rgbaq(vertex & 0xFFu, (vertex >> 8) & 0xFFu,
                         (vertex >> 16) & 0xFFu, (vertex >> 24) & 0xFFu, GS_Q_ONE);
    *c++ = gs_pack_st(32.0f, 32.0f);
    *c++ = gs_pack_xyz2(gs_vertex_coord16(r->x0), gs_vertex_coord16(r->y0), 0u);
    *c++ = gs_pack_xyz2(gs_vertex_coord16(r->x1), gs_vertex_coord16(r->y1), 0u);

    return (int32_t)(c - qw) / 2;   /* GIF quadwords, the unit ps2_gif_post takes */
}

static void ps2_gstex_probe(void)
{
    static uint64_t s_pkt[2u * GS8_PACKET_QW] __attribute__((aligned(16)));
    static uint32_t s_buf[GS8_BW * (GS8_Y1 - GS8_Y0)] __attribute__((aligned(16)));
    static uint32_t s_plane_r[GS8_PLANE * GS8_PLANE] __attribute__((aligned(16)));
    static uint32_t s_plane_b[GS8_PLANE * GS8_PLANE] __attribute__((aligned(16)));
    gs_poster_rect r;
    gs_draw_env env;
    const gs_tex_t *tr, *tb;
    const uint32_t bw = GS8_BW, bh = (uint32_t)(GS8_Y1 - GS8_Y0);
    const uint32_t n = bw * bh;
    const uint32_t red_low = GSQUAD_RED & 0x00ffffffu,
                   blue_low = GSQUAD_BLUE & 0x00ffffffu, white_low = 0x00ffffffu;
    uint32_t dest;
    uint32_t i, u_green, r_plane, r_white, r_black, r_dest, b_plane, b_white, b_black, b_dest, b_other;
    uint32_t first_b_other = 0u;
    int trusted = 1;
    int32_t qw;

    ps2_gs_text_clear();
    dest = ps2_gs_get_framebuffer()[(uint32_t)(GS8_Y0 + GS8_Y1) / 2u
                                    * (uint32_t)PS2_SCREEN_WIDTH + GS8_R_X0 + (int)(bw / 2u)];

    /* Fill each plane with ONE canvas-order colour, verbatim: ps2_gs_tex_upload() streams these bytes
     * with no conversion and TEX0 says CT32, so a texel's word IS what a full-coverage modulate reads. */
    for (i = 0u; i < GS8_PLANE * GS8_PLANE; i++) { s_plane_r[i] = GSQUAD_RED; s_plane_b[i] = GSQUAD_BLUE; }

    /* Register -- the installed sink uploads each plane's bytes synchronously before this returns. */
    tr = gs_tex_register(s_plane_r, GS8_PLANE, GS8_PLANE, GS8_PLANE, GS8_PLANE);
    tb = gs_tex_register(s_plane_b, GS8_PLANE, GS8_PLANE, GS8_PLANE, GS8_PLANE);
    if (tr == 0 || tb == 0) {
        ps2_kprintf("[GSTEX] SKIP: a plane was refused (VRAM full?), so nothing can sample it -- "
                    "this run measured no texture stage\n");
        return;
    }

    ps2_kprintf("[GSTEX] three 40x40 boxes at y=%d..%d, dest=0x%08x; U=flat green, "
                "R=textured-over-red-plane+TEXFLUSH, B=textured-over-blue-plane-NOFLUSH, "
                "vertex=0x%08x\n",
                GS8_Y0, GS8_Y1 - 1, (unsigned int)dest, (unsigned int)GS8_VERTEX);
    ps2_kprintf("[GSTEX] plane=the sampler latched, white=TME/TEX0 did not take (or no flush "
                "latched it), black=page read as zero, navy=fragment discarded\n");

    /* U -- untextured opaque green, the geometry+read positive (rung 4's shape, unchanged). */
    r.y0 = GS8_Y0; r.y1 = GS8_Y1;
    r.x0 = GS8_U_X0; r.x1 = GS8_U_X0 + (int)bw;
    ps2_stage1_gs_env(&env);   /* reg_tex0/1/clamp are 0; a TME=0 sprite reads none of them */
    qw = gs_poster_ref_sprite_packet(&r, GSQUAD_GREEN, 0u, s_pkt, GS_REF_SPRITE_QWC);
    if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0 || ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
        trusted = 0;
    } else { (void)ps2_gif_drain(); (void)ps2_gif_reap(); }

    /* R -- textured over the red plane, PRIM AD-written, TEXFLUSH before the draw. */
    r.x0 = GS8_R_X0; r.x1 = GS8_R_X0 + (int)bw;
    if (trusted) {
        ps2_stage1_gs_env(&env);
        env.reg_tex0 = tr->tex0; env.reg_tex1 = tr->tex1; env.reg_clamp = tr->clamp;
        qw = gs8_textured_sprite(&r, GS8_VERTEX, tr, 1u, s_pkt, GS8_PACKET_QW);
        if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0 || ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
            trusted = 0;
        } else { (void)ps2_gif_drain(); (void)ps2_gif_reap(); }
    }

    /* B -- textured over the blue plane, same everything EXCEPT no TEXFLUSH: the flush fork. */
    r.x0 = GS8_B_X0; r.x1 = GS8_B_X0 + (int)bw;
    if (trusted) {
        ps2_stage1_gs_env(&env);
        env.reg_tex0 = tb->tex0; env.reg_tex1 = tb->tex1; env.reg_clamp = tb->clamp;
        qw = gs8_textured_sprite(&r, GS8_VERTEX, tb, 0u, s_pkt, GS8_PACKET_QW);
        if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0 || ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
            trusted = 0;
        } else { (void)ps2_gif_drain(); (void)ps2_gif_reap(); }
    }

    if (!trusted) {
        ps2_kprintf("[GSTEX] SKIP: a draw or the environment was refused, so the boxes are not all on "
                    "the bus -- nothing here names the texture stage\n");
        return;
    }

    u_green = r_plane = r_white = r_black = r_dest = 0u;
    b_plane = b_white = b_black = b_dest = b_other = 0u;

    /* Tally the textured boxes on RGB only (mask off the A byte, which TCC=1 sets from the plane). */
    r.x0 = GS8_U_X0;
    (void)ps2_gs_vram_read(r.x0, GS8_Y0, (int)bw, (int)bh, s_buf);
    if (ps2_gs_vram_read_err() != GS_READ_BUS_OK) trusted = 0;
    else { for (i = 0u; i < n; i++) if (s_buf[i] == GSQUAD_GREEN) u_green++; }

    r.x0 = GS8_R_X0;
    if (trusted) (void)ps2_gs_vram_read(r.x0, GS8_Y0, (int)bw, (int)bh, s_buf);
    if (trusted && ps2_gs_vram_read_err() != GS_READ_BUS_OK) trusted = 0;
    else if (trusted) {
        for (i = 0u; i < n; i++) {
            uint32_t rgb = s_buf[i] & 0x00ffffffu;
            if (rgb == red_low) r_plane++;
            else if (rgb == white_low) r_white++;
            else if (rgb == 0u) r_black++;
            else if (s_buf[i] == dest) r_dest++;
        }
    }

    r.x0 = GS8_B_X0;
    if (trusted) (void)ps2_gs_vram_read(r.x0, GS8_Y0, (int)bw, (int)bh, s_buf);
    if (trusted && ps2_gs_vram_read_err() != GS_READ_BUS_OK) trusted = 0;
    else if (trusted) {
        for (i = 0u; i < n; i++) {
            uint32_t rgb = s_buf[i] & 0x00ffffffu;
            if (rgb == blue_low) b_plane++;
            else if (rgb == white_low) b_white++;
            else if (rgb == 0u) b_black++;
            else if (s_buf[i] == dest) b_dest++;
            else { if (b_other == 0u) first_b_other = s_buf[i]; b_other++; }
        }
    }

    if (!trusted) {
        ps2_kprintf("[GSTEX] SKIP: a page read failed -- the tallies are not a picture\n");
        return;
    }

    ps2_kprintf("[GSTEX] U flat-green : green=%u /%u\n", (unsigned int)u_green, (unsigned int)n);
    ps2_kprintf("[GSTEX] R text-red   : plane=%u white=%u black=%u dest=%u /%u\n",
                (unsigned int)r_plane, (unsigned int)r_white, (unsigned int)r_black,
                (unsigned int)r_dest, (unsigned int)n);
    ps2_kprintf("[GSTEX] B text-blue  : plane=%u white=%u black=%u dest=%u other=%u first=0x%08x /%u\n",
                (unsigned int)b_plane, (unsigned int)b_white, (unsigned int)b_black,
                (unsigned int)b_dest, (unsigned int)b_other, (unsigned int)first_b_other,
                (unsigned int)n);

    if (u_green != n) {
        ps2_kprintf("[GSTEX] SKIP: the untextured control painted only %u/%u px -- the box never reached "
                    "the page, so the textured rows say nothing about the GS\n",
                    (unsigned int)u_green, (unsigned int)n);
        return;
    }

    /* The control proves geometry and the read.  R names its plane WITH a flush, B names a different
     * plane WITHOUT one; the two forks together (colour, then flush) separate three questions the old
     * single-shape probe could only blur. */
    if (r_plane == n && b_plane == n) {
        ps2_kprintf("[GSTEX] verdict: TEXTURING WORKS, TEXFLUSH NOT REQUIRED. Both textured boxes came "
                    "back their own plane (R red, B blue), and B got there with no TEXFLUSH, so an "
                    "AD-written PRIM/TEX0 group latches the sampler on this GS without a flush.  The "
                    "eegs icon artefact is therefore NOT the sampler -- read the RGBAQ/geometry and the "
                    "filter/wrap the app feeds, next.\n");
    } else if (r_plane == n && b_white == n) {
        ps2_kprintf("[GSTEX] verdict: THE SAMPLER NEEDS TEXFLUSH. R flushed and read its red plane; B "
                    "named its plane but never flushed and read the WHITE vertex.  So a freshly-pointed "
                    "TEX0 does not reach the sampler until a TEXFLUSH forces the page reload -- and "
                    "eegs_texture() (backend_eegs.c) writes CLAMP/TEX0/TEX1 with NO flush, which is the "
                    "icon bug.  Add GS_GP_TEXFLUSH to that AD trio.\n");
    } else if (r_white == n && b_white == n) {
        ps2_kprintf("[GSTEX] verdict: TME/TEX0 STILL DO NOT TAKE even with PRIM AD-written -- both "
                    "boxes read the WHITE vertex.  The texture state is not the fix; the sampler never "
                    "engages on this send path at all, so look upstream of TEX0 (the upload, or whether "
                    "the dense chain carries its ST at all).\n");
    } else if (r_black == n && b_black == n) {
        ps2_kprintf("[GSTEX] verdict: the page reads ZERO -- both textured boxes came back black, so an "
                    "uploaded plane is sampled as 0.  The upload vs TEX0 format (PSM/pitch/byte order) is "
                    "the fault.\n");
    } else if (r_dest == n && b_dest == n) {
        ps2_kprintf("[GSTEX] verdict: the textured fragments are DISCARDED -- both boxes still hold their "
                    "destination, which is the [GSLIVE] symptom reproduced at the sprite level.\n");
    } else {
        ps2_kprintf("[GSTEX] verdict: MIXED -- R %u/%u/%u/%u (flush) and B %u/%u/%u/%u (no flush) plane/white/black/dest; "
                    "no single model predicts this, read the tallies against the words before believing "
                    "the run.\n",
                    (unsigned int)r_plane, (unsigned int)r_white, (unsigned int)r_black,
                    (unsigned int)r_dest, (unsigned int)b_plane, (unsigned int)b_white,
                    (unsigned int)b_black, (unsigned int)b_dest);
    }
}

/* ── Part 4: GS Race & Ink Diagnostics ────────────────────────────────── */
#if BTRON_PS2_BENCH_GS >= 5
/* Word-level inspection of staged GIF primitive packets on the bus.
 * Decodes GIF tag headers (NLOOP, FLG, NREG, PRE, EOP) and registers in the REGLIST
 * unpack sequence (RGBAQ, UV/ST, XYZ2).
 * Verifies that the fixed-point coordinate mapping (field >> 4) - 2048 matches
 * the requested screen client coordinates.
 * Called immediately after submission while packets remain in the staging buffer. */
static void ps2_bench_dump_groups(void)
{
    static uint32_t s_25_frames;
    const uint32_t ng = eegs_present_groups_sent();
    uint32_t g;

    if (s_25_frames >= 3u) return;
    s_25_frames++;

    ps2_kprintf("[GS25] frame %u: %u group(s) on the bus.  A GIF quadword is 16 bytes = 2 u64 "
                "registers; half 0 is the tag, half 1 the REGS list.  XYZ2 field -> ask px is "
                "(field>>4)-2048.  nreg 2 = RGBAQ+XYZ2, 3 = RGBAQ+ST/UV+XYZ2.\n",
                (unsigned int)s_25_frames, (unsigned int)ng);

    for (g = 0u; g < ng && g < 4u; g++) {
        static uint64_t w[2u * 16u];
        const uint32_t got = eegs_present_group_peek(g, w,
                                        (uint32_t)(sizeof w / sizeof w[0]));
        uint32_t nloop, nreg, flg, k, pos = 0u, nv;

        if (got < 2u) {
            ps2_kprintf("[GS25]  g%u: %u word(s), no tag pair staged\n",
                        (unsigned int)g, (unsigned int)got);
            continue;
        }
        nloop = (uint32_t)(w[0] & 0x7FFFu);
        flg   = (uint32_t)((w[0] >> 58) & 3u);
        nreg  = (uint32_t)((w[0] >> 60) & 0xFu);
        for (k = 0u; k < nreg && k < 10u; k++) {
            if (((w[1] >> (4u * k)) & 0xFu) == GS_NIB_XYZ2) pos = k;
        }
        ps2_kprintf("[GS25]  g%u tag nloop=%u eop=%u pre=%u flg=%u nreg=%u "
                    "prim(op=%u iip=%u tme=%u abe=%u) | u64words=%u\n",
                    (unsigned int)g, (unsigned int)nloop,
                    (unsigned int)((w[0] >> 15) & 1u),
                    (unsigned int)((w[0] >> 46) & 1u), (unsigned int)flg,
                    (unsigned int)nreg,
                    (unsigned int)((w[0] >> 47) & 7u),
                    (unsigned int)((w[0] >> 50) & 1u),
                    (unsigned int)((w[0] >> 51) & 1u),
                    (unsigned int)((w[0] >> 53) & 1u),
                    (unsigned int)got);
        if (flg != GS_FLG_IMAGE && nreg != 0u) {
            nv = (nloop < 4u) ? nloop : 4u;
            for (k = 0u; k < nv; k++) {
                const uint32_t idx = 2u + k * nreg + pos;
                uint32_t xf, yf;

                if (idx >= got) break;
                xf = (uint32_t)(w[idx] & 0xFFFFu);
                yf = (uint32_t)((w[idx] >> 16) & 0xFFFFu);
                ps2_kprintf("[GS25]      v%u X=%04x Y=%04x -> ask %d,%d  Z=%08x\n",
                            (unsigned int)k, (unsigned int)xf, (unsigned int)yf,
                            (int)(xf >> 4) - 2048, (int)(yf >> 4) - 2048,
                            (unsigned int)(w[idx] >> 32));
            }
        }
    }
}

/* Diagnostic probe for VRAM rasterization completion versus bus DMA transfer.
 *
 * A DMA channel 2 STR completion confirms that quadwords left EE memory, but the GS
 * rasterizer processes primitives asynchronously from its internal FIFO.
 * Reading VRAM immediately through VIF1 Local<-Host transfers can sample partially
 * rasterized primitives if the pipeline has not settled, making incomplete rendering
 * indistinguishable from dropped commands.
 *
 * This probe samples vertical pixel boxes across multiple frames with settled waits
 * and VSYNC synchronization to verify rasterizer completion before readback. */
#define GS26_BW  64u                        /* a read's width, and a multiple of the 4 px the */
#define GS26_BH  16u                        /* Local<-Host laws ask for                     */
#define GS26_N   6u
#define GS26_FRAMES 14u                     /* enough to cover the frames the census reads on */
#define GS26_SENT 0x00FF00FFu               /* GSREAD_SENT; its own define lives far below this
                                               function, and a fill word must not survive a read */

static const uint16_t s_26_local[GS26_N] = { 4u, 40u, 96u, 200u, 320u, 460u };

/* The column every rung-26/27 box is read out of, decided once so the census's cross-check can name
 * the same pixels the send did rather than recompute an offset that may have drifted. */
static int s_26_x = -1;

/* Force the GIF quiet, bounded the way every other spin in this port is.  Returns 1 if it never
 * settled.  `drain()` deliberately returns the moment a transfer is on the bus rather than waiting
 * for it, so one call is not a settle: it empties the queue and leaves the last group still
 * transferring. */
static uint32_t ps2_bench_ink_settle(void)
{
    uint32_t spins;

    for (spins = 0u; spins < 100000u; spins++) {
        (void)ps2_gif_drain();
        (void)ps2_gif_reap();
        if (ps2_gif_pending() == 0u && !ps2_gif_hw_busy()) break;
    }
    return (ps2_gif_pending() != 0u || ps2_gif_hw_busy()) ? 1u : 0u;
}

/* Count one box of an image that is already in main memory -- the census's own page buffer, or the
 * EE canvas -- with the same alpha law the VRAM read uses, so the two columns of rung 27's table are
 * the same measurement taken through two different roads.
 *
 * Counted by ALPHA, not by "any ink": the canvas that went out earlier in the pass fills the same
 * rect with 0xff______ whenever this build did not cut it, so a nonzero count says nothing about the
 * stream.  A=0xfe is the stream's own byte -- rung 21c established no other writer on this path
 * produces it -- and 0x00000000 is either a word nobody has written or a word the stream itself
 * wrote as its plate, which is what rung 27's canvas column separates those two readings apart.
 *
 * Rung 28 adds the fourth column, and it is not a refinement: a brightened pixel stores the vertex's
 * own As ([GS28]: "a blended fragment stores its own alpha in the page"), so the wave's px carry
 * A=9-ish, which is neither 0xfe nor 0xff nor zero.  Counting only `fe` would read a successfully
 * brightened ribbon as an unwritten page and make the fix look like the bug.  `br` is that ink: any
 * nonzero alpha below 0xfe, which no writer on this path produces except a blend whose As arrived. */
static void ps2_bench_ink_one(uint32_t v, uint32_t *fe, uint32_t *ff, uint32_t *z, uint32_t *br)
{
    if (v == 0u || v == GS26_SENT) (*z)++;         /* sentinel = a px the read under-delivered */
    else if ((v >> 24) == 0xfeu) (*fe)++;
    else if ((v >> 24) == 0xffu) (*ff)++;
    else if ((v >> 24) != 0u) (*br)++;
}

static void ps2_bench_ink_tally(const uint32_t *img, uint32_t stride, int x, int y,
                                uint32_t *fe, uint32_t *ff, uint32_t *z, uint32_t *br)
{
    uint32_t j, i, nfe = 0u, nff = 0u, nz = 0u, nbr = 0u;

    for (j = 0u; j < GS26_BH; j++) {
        const uint32_t *row = img + (size_t)(y + (int)j) * (size_t)stride + (size_t)x;

        for (i = 0u; i < GS26_BW; i++) ps2_bench_ink_one(row[i], &nfe, &nff, &nz, &nbr);
    }
    *fe = nfe; *ff = nff; *z = nz; *br = nbr;
}

/* One silent read of the six boxes, after the caller has settled the bus.  A whole box is 1024
 * words; 0xFFFFFFFF in a column means that box's read failed and the row says so as `-`.
 *
 * The column is decided on the first call and kept: the send-side probe and the census's cross-check
 * have to name the SAME pixels or the two columns of rung 27 are not the same measurement. */
static void ps2_bench_ink_boxes(const RECT *cl, uint32_t *fe, uint32_t *ff, uint32_t *z,
                                uint32_t *br)
{
    static uint32_t s_26_box[GS26_BW * GS26_BH] __attribute__((aligned(16)));
    uint32_t j, i;

    if (s_26_x < 0) {
        s_26_x = cl->left + 8;
        if (s_26_x + (int)GS26_BW > cl->right) s_26_x = cl->right - (int)GS26_BW;
        if (s_26_x < cl->left) s_26_x = cl->left;
        /* A read lands on quadword boundaries, so the column must be a multiple of 4 px. */
        s_26_x -= s_26_x % 4;
    }

    for (j = 0u; j < GS26_N; j++) {
        const int y = cl->top + (int)s_26_local[j];
        uint32_t nfe, nff, nz, nbr;

        for (i = 0u; i < GS26_BW * GS26_BH; i++) s_26_box[i] = GS26_SENT;
        (void)ps2_gs_vram_read(s_26_x, y, (int)GS26_BW, (int)GS26_BH, s_26_box);
        if (ps2_gs_vram_read_err() != GS_READ_BUS_OK) {
            fe[j] = ff[j] = z[j] = br[j] = 0xFFFFFFFFu;
            continue;
        }
        ps2_bench_ink_tally(s_26_box, GS26_BW, 0, 0, &nfe, &nff, &nz, &nbr);
        fe[j] = nfe; ff[j] = nff; z[j] = nz; br[j] = nbr;
    }
}

/* Rung 27's first half: the same six boxes read at the live-page census's FIRST act, before it
 * allocates, uploads a strip, draws a control or reads a page.  Held silently -- a print here would
 * repaint the console band between the census and the page it is about to read, which is the law
 * rung 12r was built to keep. */
static uint32_t s_27_fe[GS26_N], s_27_ff[GS26_N], s_27_z[GS26_N], s_27_br[GS26_N];
static uint32_t s_27_census[GS26_N][4];  /* the census's own road: [box][fe, ff, z, br] */
static uint32_t s_27_cov[GS26_N];        /* px of that box the walk reached, whatever their alpha */
static uint32_t s_27_settle_failed;
static int s_27_taken;
static uint32_t s_27_walk_on;             /* armed by the sampled census, discharged by the table */
static RECT s_27_rect;                  /* the client the boxes were placed in, kept for the walk */

static void ps2_bench_ink_at_census(const RECT *cl)
{
    uint32_t j;

    if (s_27_taken) return;                       /* one sample per census, on the first call's rect */

    s_27_taken = 1;
    s_27_rect = *cl;
    for (j = 0u; j < GS26_N; j++) {
        s_27_census[j][0] = s_27_census[j][1] = s_27_census[j][2] = s_27_census[j][3] = 0u;
        s_27_cov[j] = 0u;
    }
    s_27_settle_failed = ps2_bench_ink_settle();
    ps2_bench_ink_boxes(cl, s_27_fe, s_27_ff, s_27_z, s_27_br);
    s_27_walk_on = 1u;                            /* from here, this census's walk is the second road */
}

/* Rung 27's second road: the SAME six boxes, classified as the census's own whole-page walk passes
 * over them.  This is the column that used to be lifted out of `buf` after the read loop, which was
 * wrong twice over -- `buf` holds one 150-row band, not a page, so the slice was the heap (the same
 * bug that emptied the rung-21 maps), and the walk had already moved past those rows by the time a
 * slice could be taken.  Counting inside the walk costs one x comparison for 64 of 800 columns. */
static void ps2_bench_ink_census_px(uint32_t x, uint32_t y, uint32_t v)
{
    uint32_t j;

    if (!s_27_walk_on) return;
    if ((int)x < s_26_x || (int)x >= s_26_x + (int)GS26_BW) return;

    for (j = 0u; j < GS26_N; j++) {
        const int by = s_27_rect.top + (int)s_26_local[j];

        if ((int)y >= by && (int)y < by + (int)GS26_BH) break;
    }
    if (j == GS26_N) return;
    s_27_cov[j]++;
    ps2_bench_ink_one(v, &s_27_census[j][0], &s_27_census[j][1], &s_27_census[j][2],
                      &s_27_census[j][3]);
}

/* Rung 26's own read of the same column, at the send. */
static void ps2_bench_gs_race_probe(const RECT *cl, int cut)
{
    static uint32_t s_26_frames;
    uint32_t cfe[2u][GS26_N];
    uint32_t cz[2u][GS26_N];
    uint32_t cff[2u][GS26_N];
    uint32_t cbr[2u][GS26_N];
    uint32_t nz_fail[2u];
    uint32_t passes, pass, j;

    if (s_26_frames >= GS26_FRAMES) return;
    s_26_frames++;

    /* Frame 1 is the A/B that asks whether the census's page was simply read too early; every frame
     * after it is a single settled read, because the answer turned out to be "the page was finished"
     * and the next question is when that ink stops being there. */
    passes = (s_26_frames == 1u) ? 2u : 1u;

    for (pass = 0u; pass < passes; pass++) {
        /* The read refuses to start while anything is queued, so pass 0's wait is the wait the census
         * already pays -- and it is the boundary of the question: after it every byte of the frame
         * has been handed to the GS, and nothing says the GS has eaten them.  Pass 1 adds the only
         * settle this port has that the EE's clock cannot undo: a vsync. */
        if (pass == 0u) nz_fail[0u] = ps2_bench_ink_settle();
        else { ps2_gs_wait_vsync(); nz_fail[1u] = 0u; }

        ps2_bench_ink_boxes(cl, cfe[pass], cff[pass], cz[pass], cbr[pass]);
    }

    if (s_26_frames == 1u) {
        ps2_kprintf("[GS26] frame 1: box x=%d, 64x16 px, at client-local y", s_26_x);
        for (j = 0u; j < GS26_N; j++) ps2_kprintf(" %u", (unsigned int)s_26_local[j]);
        ps2_kprintf(".  fe = A=0xfe stream ink, ff = A=0xff upload, z = unwritten or undelivered, "
                    "br = A=1..0xfd, the alpha a BLEND stored ([GS28]) -- a brightened ribbon is `br`, "
                    "not `fe`; a whole box is 1024 words, -1 = the read failed.\n");
    }

    for (pass = 0u; pass < passes; pass++) {
        if (passes == 2u) ps2_kprintf("[GS26]  %s:  ", pass == 0u ? "bus empty, no wait "
                                                                   : "after one vsync ");
        else ps2_kprintf("[GS26] f%-2u cut=%u:                    ", (unsigned int)s_26_frames,
                         (unsigned int)cut);
        for (j = 0u; j < GS26_N; j++) {
            if (cfe[pass][j] == 0xFFFFFFFFu) ps2_kprintf("[  -   -   -   -] ");
            else ps2_kprintf("[%4u %4u %4u %4u] ", (unsigned int)cfe[pass][j],
                             (unsigned int)cff[pass][j], (unsigned int)cz[pass][j],
                             (unsigned int)cbr[pass][j]);
        }
        ps2_kprintf("\n");
    }

    if (passes == 1u) return;
    {
        uint32_t gained = 0u, same = 1u;

        for (j = 0u; j < GS26_N; j++) {
            if (cfe[0u][j] != cfe[1u][j] || cz[0u][j] != cz[1u][j] || cbr[0u][j] != cbr[1u][j])
                same = 0u;
            if (cfe[1u][j] > cfe[0u][j] || cbr[1u][j] > cbr[0u][j] || cz[1u][j] < cz[0u][j])
                gained = 1u;
        }
        ps2_kprintf("[GS26] verdict: %s\n",
                    (nz_fail[0u] || nz_fail[1u])
                        ? "SKIP -- the bus did not settle inside the spin bound, so the first read "
                          "is not a clean 'everything handed to the GS' sample"
                        : same
                            ? "identical -- the GS had already written every word it was going to "
                              "write when the census looked, so the missing rows were never rasterised"
                            : gained
                                ? "GAINED stream ink across the wait -- the census read a page still "
                                  "being drawn; the rows below are late, not lost"
                                : "CHANGED without gaining stream ink -- a settle moved the page, "
                                  "which is another writer, not a completion");
    }
}

/* Diagnostic cross-validation: compares ink telemetry across three access paths
 * inside the live-page census.
 *
 * Verifies consistency across differing transfer geometries:
 * - `read`: a discrete 64x16 Local<-Host VRAM request taken before other drawing.
 * - `walk`: 800-pixel full-width band reads classified as the census sweep advances.
 * - `canvas`: expected CPU backbuffer pixel state prior to presentation upload.
 *
 * Distinguishes unwritten/clipped surfaces from overwritten plates and geometry mismatches. */
static void ps2_bench_ink_cross(const RECT *cl, uint32_t stride, const uint32_t *canvas)
{
    uint32_t j, n_agree = 0u, n_ink = 0u;

    if (!s_27_walk_on) return;              /* one table, for the one census that took the sample */

    ps2_kprintf("[GS27] one law, three roads, x=%d 64x16 px.  `read` = a Local<-Host request of this "
                "box, taken at the census's first act; `walk` = the same coordinates as this census's "
                "own 800-px-wide band reads passed them; `canvas` = the EE pixels a canvas upload "
                "would lay there.  read-vs-walk disagreeing is a read-geometry bug; walk-zero with a "
                "coloured canvas is an unwritten page; walk-zero with a zero canvas is the plate being "
                "re-laid; read=ink with walk=zero is a present that erased between the two.  "
                "fe=A=0xfe stream ink, ff=A=0xff upload, br=A=1..0xfd a blend that stored its own As, "
                "z=zero or undelivered; a whole box is 1024 px and the four columns sum to it.  The "
                "wave's px should live in `br` once the brighten law lands: a fix that moves ink from "
                "`fe` to `br` is a fix, not a regression.\n", s_26_x);
    if (s_27_settle_failed) {
        ps2_kprintf("[GS27] SKIP: the bus did not settle inside the spin bound, so the `read` column "
                    "is not a clean sample of the page the census found.\n");
    }

    for (j = 0u; j < GS26_N; j++) {
        uint32_t cfe = 0u, cff = 0u, cz = 0u, cbr = 0u;
        const int y = cl->top + (int)s_26_local[j];
        const int agree = (s_27_fe[j] == s_27_census[j][0]
                           && s_27_ff[j] == s_27_census[j][1]
                           && s_27_z[j] == s_27_census[j][2]
                           && s_27_br[j] == s_27_census[j][3]);

        ps2_bench_ink_tally(canvas, stride, s_26_x, y, &cfe, &cff, &cz, &cbr);
        if (s_27_fe[j] + s_27_br[j] == GS26_BW * GS26_BH) n_ink++;
        if (s_27_cov[j] == GS26_BW * GS26_BH && agree) n_agree++;

        if (s_27_fe[j] == 0xFFFFFFFFu) ps2_kprintf("[GS27] y=%-4d read[  -   -   -   -]", y);
        else ps2_kprintf("[GS27] y=%-4d read[%4u %4u %4u %4u]", y, (unsigned int)s_27_fe[j],
                         (unsigned int)s_27_ff[j], (unsigned int)s_27_z[j],
                         (unsigned int)s_27_br[j]);
        ps2_kprintf(" walk[%4u %4u %4u %4u] cov=%4u canvas[%4u %4u %4u %4u]%s\n",
                    (unsigned int)s_27_census[j][0], (unsigned int)s_27_census[j][1],
                    (unsigned int)s_27_census[j][2], (unsigned int)s_27_census[j][3],
                    (unsigned int)s_27_cov[j],
                    (unsigned int)cfe, (unsigned int)cff, (unsigned int)cz, (unsigned int)cbr,
                    s_27_z[j] == 0xFFFFFFFFu ? "  (read failed)"
                        : (s_27_cov[j] != GS26_BW * GS26_BH)
                            ? "  <== the walk did not reach every px of this box"
                        : agree ? "" : "  <== the two roads DISAGREE");
    }

    ps2_kprintf("[GS27] verdict: %u of %u boxes gave the SAME four counts down both roads, and %u of "
                "%u are 1024/1024 stream-ink px (fe + br) in `read`.  Until the first number is %u this "
                "table is a statement about the two read geometries, not about the page; once it is, the "
                "second says whether the GS layer's client is the stream's ink or a canvas upload's.\n",
                (unsigned int)n_agree, (unsigned int)GS26_N,
                (unsigned int)n_ink, (unsigned int)GS26_N, (unsigned int)GS26_N);
    s_27_walk_on = 0u;
}
#endif

/* ── Part 5: Keyboard Cadence Diagnostic ──────────────────────────────── */
#if BTRON_PS2_BENCH
/* The repeat law priced against this machine's frame, with no hand on a key.  The tick
 * runs once per pass, so what the Input applet's three *FX 12 rows actually deliver is
 * this pass period divided by the interval.  A run that prints 5/3/1 shows the three
 * settings are distinguishable on this build; one that prints 1/1/1 says the frame is
 * slower than every rate the menu offers, which is a fact about the frame loop and not
 * about the keyboard.  The period is the last stamped cycle -- the same sum the [XMBT]
 * row prints -- so these numbers and the frame rows came from one image. */
static void ps2_bench_kbd_cadence(void)
{
    const uint32_t period = s_cycle.pass_us + s_cycle.tail_us + s_cycle.hold_us;
    uint32_t adv;

    ps2_kprintf("[BENCH] kbd cadence: enabled=%d delay=%u us pass=%u us -> "
                "*FX 12 25000=%u 40000=%u 80000=%u reps/pass (cap %u)\n",
                g_kbd_repeat_enabled ? 1 : 0, (unsigned int)g_kbd_repeat_delay_us,
                (unsigned int)period,
                (unsigned int)ps2_kbd_due(period, 25000u, &adv),
                (unsigned int)ps2_kbd_due(period, 40000u, &adv),
                (unsigned int)ps2_kbd_due(period, 80000u, &adv),
                (unsigned int)PS2_KBD_REPEAT_MAX);
}
#endif /* BTRON_PS2_BENCH */

/* ── Part 6: Automated Headless Benchmark & Census Suite ──────────────── */
#if BTRON_PS2_BENCH
/* ── Automated Present & Pointer Bench ────────────────────────────── */

/* Why this exists instead of another protocol for a hand: the present used to
 * repaint a full-width band of rows, and a band of that shape is not symmetric
 * between the two directions -- vertical travel adds rows, because the sprite's old
 * rows and its new ones part company, while horizontal travel never does.  Whether
 * that was worth fixing by presenting a rect instead is a question about what 800x16
 * costs against 32x16 on this hardware, including the per-row GIF setup a narrow
 * region pays (ps2_gs_upload() has to issue one transfer per row as soon as the
 * width is not the canvas's).  Only the machine can answer that, so this asks it, in
 * a build that needs no window, no keyboard and no protocol: make ps2 BENCH=1.
 *
 * And it did answer, on 2026-10-08: 800x16 = 5370 us against 16x16 = 528 us and
 * 80x16 = 1033 us, with upload -- the term the per-row setup lives in -- at 45 us
 * against 10 us.  The narrow region is an order of magnitude cheaper even paying
 * one setup per row, so the bands became rectangles.  This stays for the same
 * reason the [PSTAT] axis split stays: the shape is now the assumption, and only
 * the machine says when it stops holding. */

/* One present of one region, repeated, timed by stage and against the wall.  The
 * wall figure is the honest one, since it covers whatever the three stages do not
 * time, and the first repetition stays inside the average on purpose: a real pass
 * also begins with cold caches, and a table that warmed the region first would be
 * read as favouring it. */
static void ps2_bench_region(GDEV *screen, const char *tag,
                             int x0, int y0, int x1, int y1, int full, uint32_t reps)
{
    uint32_t r_sum = 0u, s_sum = 0u, u_sum = 0u;
    uint32_t r_max = 0u, s_max = 0u, u_max = 0u;
    const uint32_t t_start = ps2_count_read();
    RENDER_STATS st;

    for (uint32_t i = 0; i < reps; i++) {
        uint32_t r, s, u;

        ps2_paint_region(screen, x0, y0, x1, y1, full, &r, &s, &u);
        r_sum += r; s_sum += s; u_sum += u;
        if (r > r_max) r_max = r;
        if (s > s_max) s_max = s;
        if (u > u_max) u_max = u;
    }
    ps2_kprintf("[BENCH] %-9s %4dx%-4d total=%u us  render %u/%u  swap %u/%u  upload %u/%u  (avg/max)\n",
                tag, x1 - x0, y1 - y0,
                (unsigned int)(ps2_us_since(t_start) / reps),
                (unsigned int)(r_sum / reps), (unsigned int)r_max,
                (unsigned int)(s_sum / reps), (unsigned int)s_max,
                (unsigned int)(u_sum / reps), (unsigned int)u_max);
    /* The composite's own split for the same region, because the three stages above
     * are this port's and these are the shared compositor's: `paint` nonzero in a
     * region that only needed the blit is application callbacks being run, and a
     * non-zero `bfn` is the background cache not holding, which is the defect the
     * PS2's bands used to pay on every pass. */
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u(%u px skip=%u) paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.frame_body_px,
                (unsigned int)st.frame_body_skip_px,
                (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
}

/* ── RAM-to-RAM: the loop, not the DRAM ───────────────────────────────────────
 *
 * The half of row 9 that is still open is a choice between two lanes -- shave the
 * EE's own pixel loops, or hand the pixels to the GS.  Until 2026-10-13 that choice
 * was priced as if one side of it were already shut.  The book's row billed a pixel
 * copied RAM to RAM at 14.8 CYCLES -- 50.2 ns -- and its argument was that `blit=`
 * comes out at that same price from 768 px to 613,440 px, three orders of magnitude
 * of size at one price, so the pixel must be paying for the DRAM rather than for the
 * loop.  That reading was never falsifiable: a byte-per-iteration tail is size-flat
 * as well, so flatness cannot tell a bandwidth from a per-iteration cost, and nothing
 * had ever moved the same bytes at two different instruction costs.
 *
 * This phase is that control, and the answer came back the other way -- the loop, and
 * specifically a scan in front of it.  What the old flat row was pricing is the
 * compositor's opacity pass, not its copy: `blit=` bills 39.8 ns/px, of which 15.7 is
 * the copy at its floor and ~24 is a SECOND traversal of the same bytes.  Both halves
 * of that decomposition are now measured rather than subtracted -- `blit scan` 39.86
 * against `copy even` 15.96, and `blit wide`, which makes the same decision two pixels
 * per load, at 36.57.  The book keeps the arithmetic in 10.1.
 *
 * This port's copy used to make exactly that difference by accident.  `btron_row_blit()`
 * took its 8-byte loop only when BOTH pointers were 8-aligned, and a damage rect starts
 * at an arbitrary pixel column, so an odd `left` left every row of that rectangle
 * 4-mod-8 and the whole of it went through the byte loop: same bytes, several times the
 * instructions.  The aarch64 half of that header has always known this column is worth
 * peeling to the shared phase -- its own comment says a present became one bus
 * transaction per byte without the peel -- and the generic half, which is the half the
 * PS2 compiles, now peels too and takes 32 bytes at a time.  So the table below prices a
 * FIXED defect, not a live one: `copy even` and `copy odd` are the shipped header, and
 * the rungs above it are the shapes it replaced, kept so that a landing's price stays
 * auditable against the rows that bought it.
 *
 * So the machine is asked three ways, because the three answers together decide the
 * lane and no one of them does:
 *
 *   copy even vs copy odd      -- if the two differ, the price is instructions and
 *                                the peel is money lying on the table; if they are
 *                                equal, no EE-side shave can help and the GS is the
 *                                only lane left.
 *   copy odd vs copy odd+peel  -- the prospective fix priced before it is landed, in
 *                                the same run as the defect it fixes, so the saving
 *                                is not an estimate smuggled into a commit.
 *   fill vs copy, same rect    -- a copy is one load and one store per pixel and a
 *                                fill is one store.  If the store alone costs what
 *                                the pair costs, accesses are serializing and the
 *                                load is free, which is the bus's signature; if a
 *                                fill is worth about half a copy, both are issue
 *                                slots, which is the loop's.  `fill even` and
 *                                `fill odd` are the control on the control: stores
 *                                are 4-aligned at any pixel column, so the two must
 *                                agree, and if they do not something else is moving
 *                                between the rows.
 *   the size sweep             -- the book's own claim, tested at the phase that can
 *                                never fall into the tail, over the range it quotes
 *                                and out to the canvas's whole 480,000 px, which is
 *                                what `bg=` bills per frame (as a copy of the cached
 *                                background, per desktop.c's
 *                                render_desktop_background_rect(), and what `frame=`
 *                                bills as fill_rec() plates).
 *
 * Copies go through btron_row_blit() one row at a time, the shape
 * btron_row_copy_twopass() calls it in, and fills through fill_rec(), the compositor's
 * hottest leaf.  Both bands are the canvas's own rows -- src above, dst below, the
 * same phase relationship production has -- so the largest disjoint copy is
 * 300 rows x 800 px = 240,000 px, and the sweep stops there and says so.  A
 * checksum is folded out of the destination after each shape and printed, because
 * that is what proves the loop ran on memory the machine can see rather than being
 * deleted as dead, and that `copy odd` moved the bytes `copy even` moved -- and for
 * a copy the same fold of the SOURCE band is printed beside it as chk=, because a row
 * whose destination does not equal its source is not the shape its tag claims.
 *
 * This phase owns nothing the next one needs: it scribbles the canvas the way the
 * region table above it already does, and every phase below repaints.
 *
 * And the machine answered, on 2026-10-13, in a ladder of loop shapes on the same
 * 399x16 band: 96.4 ns/px through the byte tail, 21.2 peeled to the shared 8-byte
 * phase and taken 8 bytes per iteration, 16.9 at 16, 16.1 at 32.  Its aligned twin
 * cost 27.8 at the body width this header used to carry, and it is the same loop as
 * the peeled one 1.31 times the price, because clang inlines the header's body into
 * the compositor's row loop with two `addu`s per iteration instead of the two
 * `addiu`s it compiles when the body stands in a function of its own: 11 instructions
 * per 8 bytes against 9.  Fills, which cannot be misaligned because they store 4-byte
 * pixels, cost 18.34 on a strip and 17.72 over a 12,800 px band; this table's own
 * leaf, with the row pointer incremented instead of recomputed, priced 10.40 four
 * stores deep and 9.14 eight deep.  Read as instruction counts per pixel -- 20 for
 * the tail, 4 at 8 bytes, 3 at 16, 2.4 at 32 -- the first three steps land at the
 * same 4.7 ns an instruction costs here and a fixed 2.4 ns a pixel, and the fourth
 * step is 2.5 ns/px smaller than that model allows.  So the ladder is real and it has
 * a floor: this machine stops charging a copy at less than ~16 ns/px and a fill at
 * less than ~8.7, and the last measured gap between them, 16.1 against 9.5 once both
 * leaves are fixed, is one load per pixel at 6.6 ns -- 1.4 instructions' worth, not
 * the 2x a serializing bus would make it.  Every figure that decided a landing is a
 * different instruction count per pixel at one price per instruction, which is what
 * an issue-bound machine looks like until it stops being one.  The book's "it is the
 * DRAM, not the loop" was falsified at the only price that mattered -- the tail -- and
 * the two leaves were rewritten from these rows.
 *
 * Both landings were then re-measured from the image that carries them, on
 * 2026-10-13 again with PS2_GL=eegs PS2_FP=hw, and the floors held: `copy even` 15.96
 * and `copy odd` 16.17 on the 400x16 band, the sweep flat at 15.65 / 15.68 / 15.69
 * from 12,800 px through 80,000 px to the largest copy this canvas can hold disjointly
 * at 240,000 px, and `fill` at 8.76 / 8.80 / 8.79 over the same sizes out to the whole
 * 480,000 px.  The byte tail that opened the ladder at 96.4 is no longer a row in the
 * table -- the peel shipped, so getting that rung back means un-landing the fix -- and
 * it stays quoted above as the rung whose price decided the header.  The one figure
 * that did NOT carry over is the old production price itself: the compositor's `blit=`
 * column read 30,755 us over 613,440 px -- the `[BENCH] compositor:` whole-canvas row
 * of the build that measured this phase but did not yet carry either leaf, 50.2 ns/px --
 * and the
 * same column of the same walk reads 24,426 us over the same 613,440 px, 39.8 ns/px,
 * in the run that has both leaves landed.  That is the row the book's flat-price
 * argument was built on, moving by 20.6% while its pixel count did not move at all,
 * which is what a loop's signature looks like on a machine that was said to be out of
 * bandwidth.
 *
 * How the top of that ladder was nearly lost, recorded because it is the reason the
 * chk= field exists: the dispatcher in ps2_bench_ram() read `mode >= 2` where it meant
 * `mode == 2`, so the 16- and 32-byte copy rungs fell into the fill branch and printed
 * fill_rec()'s price under a copy tag -- the same 0xf9f6d730 the fill rows print, three
 * rows in a row, in every one of the first four runs.  Nothing in the log said so; the
 * fold disagreed and nobody was reading it.  The predicate is now the mode it names, a
 * copy row folds its source band the same way it folds its destination and the row
 * prints whether the two agree, and the two widths were then re-run interleaved at 256
 * repetitions because the gap being read is 5% while the drift between adjacent rows is
 * about 1%: 16.94 and 16.93 against 16.13 and 16.14, with the repeat pair after the
 * other width has run, so a width that wins only in the first slot shows up as one.
 * 32 bytes went into the header on the strength of that and of `copy 12800px` falling
 * from 16.53 to 15.58 -- the size `bg=` actually bills -- and eight stores went into
 * fill_rec() on 9.14 against 10.40, which fill_rec() then matched: 9.31 on a strip,
 * 8.74 at 12,800 px, 8.76 over the whole 480,000 px canvas.  What the compositor bills
 * for a whole-canvas present is 41,774 us against the 56,948 the same table printed
 * before any of this, and against 44,162 with only the first rung of each landed.
 * These rows stay for the reason they went in: the floors at 16 and 8.7 are now the
 * assumption, and only the machine says when they stop holding. */

/* The 8-byte rung of the fix, kept as the step two below what shipped: peel the row
 * byte-wise to the next 8-byte boundary when the two pointers share a phase -- which
 * they always do here, two row-aligned buffers addressed at the same rect offset --
 * and take 8 bytes per iteration.  It is the middle of the ladder, and the reason the
 * ladder is printed rather than assumed: this rung measures 21.2 ns/px, the header's
 * 16-byte form 16.9 and its shipped 32-byte form 16.1, so the price of a pixel here
 * is a curve that flattens, and where it flattens is a measurement and not a guess. */
static void ps2_bench_row_blit_peeled(void *dst, const void *src, size_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if ((((uintptr_t)d ^ (uintptr_t)s) & 7u) == 0u) {
        size_t peel = (8u - ((uintptr_t)d & 7u)) & 7u;
        if (peel > bytes) peel = bytes;
        for (size_t i = 0; i < peel; i++) d[i] = s[i];
        d += peel; s += peel; bytes -= peel;
    }
    while (bytes >= 8u) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8; s += 8; bytes -= 8u;
    }
    while (bytes--) *d++ = *s++;
}

/* The same peel with the body taken 16 or 32 bytes per iteration, because the
 * first run of this phase (2026-10-13) found that loop shape, not just loop
 * alignment, is what the pixel is charged for: the header's 8-byte loop costs the
 * machine 11 instructions per iteration when clang inlines it into the
 * compositor's row loop and 9 when it sits in a function of its own, and those two
 * forms move identical bytes at 27.7 and 21.2 ns/px.  A width is therefore not
 * something to reason about; it is something to time. */
static void ps2_bench_row_blit_wide(void *dst, const void *src, size_t bytes, int per32)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if ((((uintptr_t)d ^ (uintptr_t)s) & 7u) == 0u) {
        size_t peel = (8u - ((uintptr_t)d & 7u)) & 7u;
        if (peel > bytes) peel = bytes;
        for (size_t i = 0; i < peel; i++) d[i] = s[i];
        d += peel; s += peel; bytes -= peel;
    }
    if (per32) {
        while (bytes >= 32u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            *(uint64_t *)(d + 8u) = *(const uint64_t *)(s + 8u);
            *(uint64_t *)(d + 16u) = *(const uint64_t *)(s + 16u);
            *(uint64_t *)(d + 24u) = *(const uint64_t *)(s + 24u);
            d += 32; s += 32; bytes -= 32u;
        }
    } else {
        while (bytes >= 16u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            *(uint64_t *)(d + 8u) = *(const uint64_t *)(s + 8u);
            d += 16; s += 16; bytes -= 16u;
        }
    }
    while (bytes >= 8u) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8; s += 8; bytes -= 8u;
    }
    while (bytes--) *d++ = *s++;
}

/* fill_rec()'s leaf, with its two known instruction costs taken out: the row
 * address, which it recomputes as y*width+left every row (one mult per row that
 * the row pointer could be incremented instead), and one volatile store per pixel
 * inside a loop that pays a compare and a branch with it.  Same canvas, same
 * volatile stores, four per iteration -- or eight, when `per8` asks the question
 * the 32-byte copy rung asks on the other side: whether the loop's overhead is
 * still being paid at the width that shipped. */
static void ps2_bench_fill_rows(GDEV *screen, int left, int band, int w, int h, int per8)
{
    volatile COLOR *row = (volatile COLOR *)screen->pixels
                          + (size_t)band * (size_t)screen->width + (size_t)left;
    const COLOR fbcol = BTRON_PIXEL_FB(COLOR_LTGRAY);
    const size_t stride = (size_t)screen->width;

    for (int y = 0; y < h; y++, row += stride) {
        int x = 0;
        if (per8) {
            for (; x + 8 <= w; x += 8) {
                row[x] = fbcol; row[x + 1] = fbcol;
                row[x + 2] = fbcol; row[x + 3] = fbcol;
                row[x + 4] = fbcol; row[x + 5] = fbcol;
                row[x + 6] = fbcol; row[x + 7] = fbcol;
            }
        }
        for (; x + 4 <= w; x += 4) {
            row[x] = fbcol; row[x + 1] = fbcol;
            row[x + 2] = fbcol; row[x + 3] = fbcol;
        }
        for (; x < w; x++) row[x] = fbcol;
    }
}

/* The decision wnd.c's client-area walk takes before it copies a row: read every
 * pixel, and bail at the first transparent one, because a transparent pixel must
 * leave whatever is below it on the canvas rather than be copied over it.  This is
 * the shape production's blit= column is in, and it is a SECOND pass over the same
 * bytes the copy is about to make -- which is what the copy rows above do not model,
 * leaving 15.7 ns/px priced against the compositor's 39.9. */
static BOOL ps2_bench_span_scan(const COLOR *src, int span)
{
    for (int cx = 0; cx < span; cx++) {
        if (src[cx] == 0x00000000) return FALSE;
    }
    return TRUE;
}

/* The same decision, two pixels per 64-bit load, with the same phase rule as the
 * copy: peel to an even pixel when the row starts at an odd byte phase and take the
 * pairs after it.  A half of the word is transparent exactly when it is zero, so the
 * test is two 32-bit compares and a load per two pixels -- 1.4 instructions a pixel
 * against the scan above's 3, and no change to what the decision means. */
static BOOL ps2_bench_span_scan_wide(const COLOR *src, int span)
{
    const uint8_t *p = (const uint8_t *)src;
    int n = span;

    if (((uintptr_t)p & 7u) != 0u) {
        if (src[0] == 0x00000000) return FALSE;
        src++;
        n--;
    }
    for (int i = 0; i + 1 < n; i += 2) {
        const uint64_t v = *(const uint64_t *)(src + i);
        if ((uint32_t)v == 0u || (uint32_t)(v >> 32) == 0u) return FALSE;
    }
    if (n & 1) return src[n - 1] != 0x00000000;
    return TRUE;
}

/* The fused shapes, and they are the reason LANE 9's `Shave` cell exists.  What the
 * two rows above price is a walk that reads a band TWICE: `blit scan` reads every pixel
 * to decide, then the shipped header reads the same bytes again to copy them.  Taking
 * 3.3 ns/px off the decision by reading two pixels per load (`blit wide`) leaves that
 * second read entirely in place -- 15.96 of the 39.86 is paid twice over.  A fused pass
 * reads once and spends the same load on the decision and on the store, so its ceiling
 * is the copy floor itself and not an opinion: it cannot cost less than the copy it
 * replaces.
 *
 * Every shape here writes exactly what wnd.c's walk writes: a pixel that is not the
 * transparent sentinel lands on the canvas, and one that is leaves whatever was below
 * it.  That is not a claim, it is the structure -- a group that fails its test falls to
 * the per-pixel fixup over its OWN pixels using the words already loaded, which is the
 * shipped else-branch's rule applied to fewer pixels -- and it is what the host
 * differential gate in verify/tests/ has to prove before any of it ships.
 *
 * The shapes differ only in how many pixels one decision covers, and in whether the
 * decision is a compare per word or one arithmetic test per group.  10.1's whole
 * finding is that a LOOP shape is priced rather than reasoned -- 96.4 ns/px through a
 * byte tail against 16.1 at 32 bytes on identical bytes -- so all of them get measured
 * and the fastest one ships, including if the fastest is the narrowest. */
static void ps2_bench_span_fuse2(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    /* The same phase rule the copy's peel uses: a damage rect starts at an arbitrary
     * pixel column, so the row is usually 4-mod-8 and the pairs come after the peel. */
    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 1 < span; i += 2) {
        const uint64_t v = *(const uint64_t *)(src + i);
        const COLOR a = (COLOR)v, b = (COLOR)(v >> 32);

        if (a != 0x00000000 && b != 0x00000000) {
            *(uint64_t *)(dst + i) = v;
        } else {
            if (a != 0x00000000) dst[i] = a;
            if (b != 0x00000000) dst[i + 1] = b;
        }
    }
    if (i < span && src[i] != 0x00000000) dst[i] = src[i];
}

static void ps2_bench_span_fuse4(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 3 < span; i += 4) {
        const uint64_t v0 = *(const uint64_t *)(src + i);
        const uint64_t v1 = *(const uint64_t *)(src + i + 2);
        const COLOR a = (COLOR)v0, b = (COLOR)(v0 >> 32);
        const COLOR c = (COLOR)v1, d = (COLOR)(v1 >> 32);

        if (a && b && c && d) {
            *(uint64_t *)(dst + i) = v0;
            *(uint64_t *)(dst + i + 2) = v1;
        } else {
            if (a) dst[i] = a;
            if (b) dst[i + 1] = b;
            if (c) dst[i + 2] = c;
            if (d) dst[i + 3] = d;
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* Eight pixels per decision, and the decision is one test per WORD accumulated into a
 * single flag, so the clean path is one branch for the group rather than eight. */
static void ps2_bench_span_fuse8(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 7 < span; i += 8) {
        const uint64_t v0 = *(const uint64_t *)(src + i);
        const uint64_t v1 = *(const uint64_t *)(src + i + 2);
        const uint64_t v2 = *(const uint64_t *)(src + i + 4);
        const uint64_t v3 = *(const uint64_t *)(src + i + 6);
        const COLOR w[8] = {
            (COLOR)v0, (COLOR)(v0 >> 32), (COLOR)v1, (COLOR)(v1 >> 32),
            (COLOR)v2, (COLOR)(v2 >> 32), (COLOR)v3, (COLOR)(v3 >> 32)
        };
        int dirty = 0;

        for (int k = 0; k < 8; k++) dirty |= (w[k] == 0x00000000);
        if (!dirty) {
            *(uint64_t *)(dst + i) = v0;
            *(uint64_t *)(dst + i + 2) = v1;
            *(uint64_t *)(dst + i + 4) = v2;
            *(uint64_t *)(dst + i + 6) = v3;
        } else {
            for (int k = 0; k < 8; k++) {
                if (w[k] != 0x00000000) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* The same group as the rung above, decided without any per-word branch at all: for a
 * 32-bit word, `(w - 1) & ~w` has its top bit set EXACTLY when w is zero, because the
 * bits a subtraction flips 0 -> 1 are the run below w's lowest set bit, which can reach
 * bit 31 only when there is no lowest set bit.  So the test is arithmetic, it is exact
 * rather than conservative, and it is the shape that would let the whole group stay a
 * straight line.  Whether EE scalar costs fewer cycles for it than for eight compares
 * is what the row answers -- not the reasoning above, which is only why the rung exists. */
static void ps2_bench_span_fuse8_bits(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 7 < span; i += 8) {
        const uint64_t v0 = *(const uint64_t *)(src + i);
        const uint64_t v1 = *(const uint64_t *)(src + i + 2);
        const uint64_t v2 = *(const uint64_t *)(src + i + 4);
        const uint64_t v3 = *(const uint64_t *)(src + i + 6);
        const uint32_t w[8] = {
            (uint32_t)v0, (uint32_t)(v0 >> 32), (uint32_t)v1, (uint32_t)(v1 >> 32),
            (uint32_t)v2, (uint32_t)(v2 >> 32), (uint32_t)v3, (uint32_t)(v3 >> 32)
        };
        uint32_t zero = 0u;

        for (int k = 0; k < 8; k++) {
            zero |= ((w[k] - 1u) & ~w[k] & 0x80000000u);
        }
        if (zero == 0u) {
            *(uint64_t *)(dst + i) = v0;
            *(uint64_t *)(dst + i + 2) = v1;
            *(uint64_t *)(dst + i + 4) = v2;
            *(uint64_t *)(dst + i + 6) = v3;
        } else {
            for (int k = 0; k < 8; k++) {
                if (w[k] != 0u) dst[i + k] = (COLOR)w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* The rung that would actually SHIP, and it is here because the bench's two bands share
 * a stride while a real client pixmap does not: `wnd->dev->width` can be odd, so a
 * window's row and the canvas's row at the same damage column are not in the same 8-byte
 * phase, and a 64-bit access valid for one is unaligned for the other.  This shape reads
 * and writes one 32-bit word per pixel with the sentinel test inline, so it needs no
 * phase at all -- 4-byte alignment is what a COLOR array always is.  If it loses to
 * `blit wide` it stays printed here as the reason the pair shapes do not ship. */
static void ps2_bench_span_fuse4w(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + 3 < span; i += 4) {
        const COLOR a = src[i], b = src[i + 1], c = src[i + 2], d = src[i + 3];

        if (a && b && c && d) {
            dst[i] = a;
            dst[i + 1] = b;
            dst[i + 2] = c;
            dst[i + 3] = d;
        } else {
            if (a) dst[i] = a;
            if (b) dst[i + 1] = b;
            if (c) dst[i + 2] = c;
            if (d) dst[i + 3] = d;
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* The dirty-content regime's period, read by every row and set by the row list around
 * a block of rows: 0 leaves a copy row reading the desktop's own pixels, and 8 puts the
 * transparent sentinel into one pixel in eight of the source band for the duration of
 * that row.  It is a period and not a flag because the same rect has to be measured on
 * both contents, or the difference between the two rows is a different rect rather than
 * a different content. */
static uint32_t s_ram_holes_period;

/* The two rungs that ask the question `f8` cannot answer on its own.  `f8` beats
 * `f4w` at the same group size, but it takes the pixels in and out as 64-bit pairs,
 * which is legal only while the window's row and the canvas's row share an 8-byte
 * phase -- and in wnd.c they do not whenever the window's left edge is odd.  These two
 * are the SAME shape with 32-bit accesses, so the only variable between `f8` and `f8w`
 * is the width of the memory operation, and the only variable between `f8w` and
 * `f16w` is how many pixels one decision covers.  If `f8w` matches `f8`, the shipped
 * form is the phase-free one and nothing is paid for the portability. */
static void ps2_bench_span_fuse8w(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + 7 < span; i += 8) {
        const COLOR w[8] = { src[i], src[i + 1], src[i + 2], src[i + 3],
                             src[i + 4], src[i + 5], src[i + 6], src[i + 7] };
        int dirty = 0;

        for (int k = 0; k < 8; k++) dirty |= (w[k] == 0x00000000);
        if (!dirty) {
            for (int k = 0; k < 8; k++) dst[i + k] = w[k];
        } else {
            for (int k = 0; k < 8; k++) {
                if (w[k] != 0x00000000) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

static void ps2_bench_span_fuse16w(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + 15 < span; i += 16) {
        const COLOR w[16] = {
            src[i], src[i + 1], src[i + 2], src[i + 3],
            src[i + 4], src[i + 5], src[i + 6], src[i + 7],
            src[i + 8], src[i + 9], src[i + 10], src[i + 11],
            src[i + 12], src[i + 13], src[i + 14], src[i + 15]
        };
        int dirty = 0;

        for (int k = 0; k < 16; k++) dirty |= (w[k] == 0x00000000);
        if (!dirty) {
            for (int k = 0; k < 16; k++) dst[i + k] = w[k];
        } else {
            for (int k = 0; k < 16; k++) {
                if (w[k] != 0x00000000) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

static void ps2_bench_ram(GDEV *screen, const char *tag, int mode,
                          int left, int w, int h, uint32_t reps)
{
    /* Which of these modes move bytes rather than paint them.  A copy needs a source
     * band as large as its destination and its destination's fold must equal that
     * band's, so the same predicate drives both the fit test below and the chk= the
     * row prints; a fill needs neither.  The fused rungs (9 through 13) are copies in
     * exactly that sense: one read, one write, and the destination must end up holding
     * the source wherever the source is not the sentinel. */
    const int is_copy = (mode == 0 || mode == 1 || mode == 3 || mode == 4 ||
                         mode == 7 || mode == 8 || mode == 9 || mode == 10 ||
                         mode == 11 || mode == 12 || mode == 13 || mode == 14 ||
                         mode == 15 || mode == 16);
    /* The second content regime, and it is a period rather than a flag because the row
     * that needs it is the one that says how many holes it had.  Zero means the band is
     * the desktop's own pixels; non-zero means one pixel in `s_ram_holes_period` of this
     * row's source band has been replaced by the transparent sentinel for the duration
     * of the reps and put back before the fold is taken.  It exists because a fused
     * shape that only wins on an all-opaque row is not a win: the group test that pays
     * off on the clean path is also the test a dirty row pays twice, once in the group
     * and once in its own fixup. */
    const uint32_t holes = s_ram_holes_period;
    COLOR *pix = screen->pixels;
    const int stride = screen->width;
    uint32_t us_sum = 0u, us_max = 0u, sum = 0u;
    const char *chk = "fill";
    const RECT saved_clip = screen->clip;
    const int src_band = 0;
    int dst_band;

    if (!pix || w <= 0 || h <= 0 || reps == 0u) return;
    if (left < 0) left = 0;
    if (left + w > stride) w = stride - left;
    if (w <= 0) return;

    /* Two disjoint bands, src in the top half and dst in the bottom, so a copy
     * reads what a previous frame painted rather than its own just-written bytes --
     * which caps a copy at half the canvas (240,000 px here) and is the reason the
     * sweep's largest rows are fills.  A fill needs no source band, so it can take
     * the whole 800x600. */
    dst_band = screen->height - h;
    if (dst_band < 0 || (is_copy && dst_band < src_band + h)) {
        ps2_kprintf("[BENCH] ram: rect %dx%d does not fit the canvas, skipped\n", w, h);
        return;
    }

    /* Puncture, time, restore.  The save is a per-row gather because the band's rows
     * are `stride` apart, and the restore is not decoration: a source band left
     * swiss-cheesed would silently price every LATER row of this table on content the
     * desktop does not have -- the same class of contamination this book caught in the
     * payload.  The hole is column 3 mod the period, chosen so that the fold's own
     * sampling (every 64th pixel) never lands on one, which is what keeps chk= the tag
     * check it is meant to be in BOTH regimes rather than only in the easy one. */
    COLOR *hole_save = NULL;
    if (holes >= 2u && is_copy) {
        hole_save = (COLOR *)malloc((size_t)w * (size_t)h * sizeof(COLOR));
        if (!hole_save) {
            ps2_kprintf("[BENCH] ram: no room to puncture %s, row skipped\n", tag);
            return;
        }
        for (int y = 0; y < h; y++) {
            COLOR *row = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;

            for (int x = 0; x < w; x++) {
                hole_save[(size_t)y * (size_t)w + (size_t)x] = row[x];
                if ((x % (int)holes) == 3) row[x] = 0x00000000;
            }
        }
    }

    if (mode == 2) {
        /* fill_rec() clips, so the leaf is only the leaf if the clip is the canvas.
         * The copy rungs ignore the clip entirely -- they address the canvas
         * directly, which is also why widening it for them would be noise. */
        RECT full = { 0, 0, screen->width, screen->height };
        screen->clip = full;
    }

    for (uint32_t i = 0; i < reps; i++) {
        const uint32_t t0 = ps2_count_read();

        if (mode == 5 || mode == 6) {
            /* The whole rect in one call, because that is the shape being priced:
             * fill_rec() is entered once per strip, and this is the same strip with
             * its row pointer incremented instead of recomputed.  Mode 6 is that
             * leaf again with eight stores per iteration instead of four. */
            ps2_bench_fill_rows(screen, left, dst_band, w, h, mode == 6);
        } else if (mode == 7 || mode == 8) {
            /* wnd.c's client-area walk, row for row, in the order it walks it: the
             * opacity decision first, then either the wide copy or the pixel test
             * that a transparent row forces.  Mode 7 takes that decision one pixel at
             * a time as wnd.c does today; mode 8 takes it two pixels per load.  The
             * copy under both is the shipped header's, so the difference between the
             * two rows is the scan and nothing else. */
            for (int y = 0; y < h; y++) {
                COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;
                const size_t bytes = (size_t)w * sizeof(COLOR);
                const BOOL opaque = (mode == 8) ? ps2_bench_span_scan_wide(s, w)
                                                : ps2_bench_span_scan(s, w);

                if (opaque) {
                    btron_row_blit(d, s, bytes);
                } else {
                    for (int cx = 0; cx < w; cx++) {
                        if (s[cx] != 0x00000000) d[cx] = s[cx];
                    }
                }
            }
        } else if (mode >= 9 && mode <= 16) {
            /* The fused shapes: no scan pass at all, because the decision and the store
             * read the same load.  Modes 9 to 12 take the pairs the copy takes (the
             * bench's two bands share a stride, so a 64-bit access is legal on both);
             * mode 13 is the one that survives a window whose stride is not a multiple
             * of two pixels, and it is the shape that can ship.  A mode that fell
             * through to the plain copy below would print a copy's number under a fuse
             * tag and the fold would call it `ok`, because a copy IS what it did -- the
             * dispatch is therefore exhaustive by construction here, and the host gate
             * is what says the fused output equals the walk's. */
            for (int y = 0; y < h; y++) {
                COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;

                if (mode == 9)       ps2_bench_span_fuse2(d, s, w);
                else if (mode == 10) ps2_bench_span_fuse4(d, s, w);
                else if (mode == 11) ps2_bench_span_fuse8(d, s, w);
                else if (mode == 12) ps2_bench_span_fuse8_bits(d, s, w);
                else if (mode == 13) ps2_bench_span_fuse4w(d, s, w);
                else if (mode == 14) ps2_bench_span_fuse8w(d, s, w);
                else if (mode == 15) ps2_bench_span_fuse16w(d, s, w);
                else btron_row_copy_skip_zero(d, s, w);
            }
        } else for (int y = 0; y < h; y++) {
            COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;

            /* `mode == 2`, not `mode >= 2`: the first four runs of this phase used a
             * >= here, so the two wide-copy rungs fell into the fill branch and printed
             * fill_rec()'s price under a copy tag, three times, and the row that was
             * supposed to decide the shipped body width never ran.  The dispatch is
             * now what the modes mean, and the checksum below is what catches the next
             * time a branch and a tag disagree. */
            if (mode == 2) {
                const RECT r = { (H)left, (H)(dst_band + y),
                                 (H)(left + w), (H)(dst_band + y + 1) };
                fill_rec(screen, &r, COLOR_LTGRAY);
            } else {
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;
                const size_t bytes = (size_t)w * sizeof(COLOR);

                if (mode == 1)      ps2_bench_row_blit_peeled(d, s, bytes);
                else if (mode == 3) ps2_bench_row_blit_wide(d, s, bytes, 0);
                else if (mode == 4) ps2_bench_row_blit_wide(d, s, bytes, 1);
                else                btron_row_blit(d, s, bytes);
            }
        }
        {
            const uint32_t e = ps2_us_since(t0);
            us_sum += e;
            if (e > us_max) us_max = e;
        }
    }
    screen->clip = saved_clip;
    if (hole_save) {
        for (int y = 0; y < h; y++) {
            COLOR *row = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;

            for (int x = 0; x < w; x++) row[x] = hole_save[(size_t)y * (size_t)w + (size_t)x];
        }
        free(hole_save);
    }

    /* Every 64th pixel of every 4th row of the destination -- a third of a percent
     * of a full-canvas rect, so not what the row is measuring, but the thing that
     * says the stores landed in memory rather than being deleted as dead, and that
     * `copy odd` moved the bytes `copy even` moved.
     *
     * For a copy the same sampling of the source band is the row's own tag check:
     * if the destination's fold equals the source's fold, the branch taken was the
     * copy's, and if it does not the tag is lying.  The first four runs of this
     * phase printed fill prices under copy tags, three rows in a row, and nothing
     * in the log said so -- only the checksum disagreed.  So the disagreement is
     * printed now. */
    {
        uint32_t ref = 0u;

        for (int y = 0; y < h; y += 4) {
            const COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;
            for (int x = 0; x < w; x += 64) sum += (uint32_t)d[x];
        }
        if (is_copy) {
            for (int y = 0; y < h; y += 4) {
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;
                for (int x = 0; x < w; x += 64) ref += (uint32_t)s[x];
            }
        }
        chk = !is_copy ? "fill" : (sum == ref ? "ok" : "MISMATCH");
    }

    /* ns/px with two decimals, in the same %u.%02u idiom the move table uses, and
     * all in 32-bit because this image has no 64-bit divisor: `__udivdi3` is not in
     * ps2_builtins.c, so a `uint64_t / uint64_t` here is a link error rather than an
     * arithmetic step.  The ordering that keeps it 32-bit is to divide the
     * nanoseconds by the pixels first and pay for the two decimals out of the
     * remainder -- ns_sum peaks at one-tenth of the overflow, and the remainder is
     * smaller than the pixel count, which never reaches a million here. */
    {
        const uint32_t px_rep = (uint32_t)w * (uint32_t)h;
        const uint32_t px_total = px_rep * reps;
        const uint32_t ns_sum = us_sum * 1000u;
        const uint32_t ns_px = px_total ? ns_sum / px_total : 0u;
        const uint32_t frac100 = px_total ? ((ns_sum % px_total) * 100u) / px_total : 0u;

        ps2_kprintf("[BENCH] %-13s %4dx%-4d %8upx/rep %6u us/rep max %6u us  %u.%02u ns/px  sum=0x%08x chk=%s\n",
                    tag, w, h, px_rep,
                    (unsigned int)(us_sum / reps), (unsigned int)us_max,
                    (unsigned int)ns_px, (unsigned int)frac100,
                    (unsigned int)sum, chk);
    }
}

/* The shape scan, and it is ONE function because the cached plate's census and a live
 * canvas's census have to be the same arithmetic or the difference between them means
 * nothing.  `want` is passed in rather than read off the image: a poster's colour is decided
 * once, from the plate the compositor owns, and a caller then asks each canvas pixel whether
 * it equals that colour.  Reading `pix[0]` here would make the live canvas answer a different
 * question the moment anything is drawn at its top-left corner.
 *
 * Per row, the first and last pixel that differs from `want` bound that row's art, and only
 * the runs OUTSIDE that box are fillable; consecutive rows with the same bound merge into one
 * rect.  `rects` therefore counts the groups a poster would carry in the general shape,
 * `bands` the simpler one that fills only whole flat rows and leaves every art row to the EE,
 * and the flat area inside an art row's box is what that shape gives back.  `poster_ctl` is
 * the control: the identical scan told to believe `want + 1`, a colour no pixel can be, so a
 * scan that was really counting rows rather than matching pixels would print the same big
 * number for it and one that works collapses to nothing.  Two of the accumulators are ONE
 * fact, and it is said here so a reader does not go looking for a second measurement: a row's
 * posterisable pixels are `w - (x1 - x0 + 1)`, so summing that over the canvas makes the
 * complement of `poster` identical BY CONSTRUCTION to `art_bbox`, the sum of the per-row art
 * boxes.  The pair is kept because the equality is this scan's own arithmetic check -- the two
 * accumulators are filled on opposite arms of the row test, so a broken one shows up as the
 * columns disagreeing rather than as a tautology. */
typedef struct {
    uint32_t poster, poster_ctl;
    uint32_t flat_px, art_bbox_px;
    uint32_t rects, bands, rows_art;
    int witness_row;
} ps2_poster_shape;

static void ps2_bench_poster_scan(const COLOR *pix, COLOR want, ps2_poster_shape *o)
{
    const int w = PS2_SCREEN_WIDTH, h = PS2_SCREEN_HEIGHT;
    const COLOR wrong = (COLOR)((uint32_t)want + 1u);
    int prev_x0 = -2, prev_x1 = -2, prev_flat = -1;
    int y, x;

    o->poster = o->poster_ctl = 0u;
    o->flat_px = o->art_bbox_px = 0u;
    o->rects = o->bands = o->rows_art = 0u;
    o->witness_row = -1;
    for (y = 0; y < h; y++) {
        const COLOR *row = &pix[(size_t)y * w];
        int x0 = -1, x1 = -1, wx0 = -1, wx1 = -1;
        uint32_t nf = 0u, nfw = 0u;

        for (x = 0; x < w; x++) {
            if (row[x] != want) { nf++; if (x0 < 0) x0 = x; x1 = x; }
            if (row[x] != wrong) { nfw++; if (wx0 < 0) wx0 = x; wx1 = x; }
        }
        if (nf == 0u && o->witness_row < 0) o->witness_row = y;
        o->flat_px += (uint32_t)(w - (int)nf);
        if (nf) {
            o->rows_art++;
            o->art_bbox_px += (uint32_t)(x1 - x0 + 1);
            o->poster += (uint32_t)(x0 + (w - 1 - x1));
        } else {
            o->poster += (uint32_t)w;
        }
        o->poster_ctl += nfw ? (uint32_t)(wx0 + (w - 1 - wx1)) : (uint32_t)w;

        /* A new signature is a new rect in the general shape, and a new fully-flat
         * run is a new band in the simple one. */
        if (x0 != prev_x0 || x1 != prev_x1) o->rects++;
        prev_x0 = x0; prev_x1 = x1;
        {
            const int is_flat = (nf == 0u) ? 1 : 0;
            if (is_flat && !prev_flat) o->bands++;
            prev_flat = is_flat;
        }
    }
}

/* LANE 9b's first measurement, and it is not about the GS.  A SPRITE poster carries ONE
 * colour and no picture -- `vram_left=1081344` is 132 of the allocator's 8 KB pages and this
 * canvas's background alone is 1,920,000 bytes -- so the lane's question is asked of the
 * background, not of the primitive: how much of `bg=` is a flat field, and into how many
 * rects does the art break it up.  The answer is taken from the painted plate rather than
 * from a second copy of desktop.c's layout constants, because two encodings of one truth
 * is the defect this port's own laws name.  A GDEV copied from the session's screen but
 * pointed at a scratch array has the same width, height and icon size, so
 * bg_cache_holds() answers yes and render_desktop_background() hands back the cached
 * plate -- the pure background, before any window, deskbar or cursor pixel is in it.  No
 * shared file is modified, and the real canvas is not touched.
 *
 * Then the same arithmetic is asked of the canvas a present would actually upload, because
 * that is the set a caller can collect: whatever the desktop put on the canvas after the
 * background blit -- the pointer's box, the deskbar, a window's body -- has stopped being the
 * flat colour and left the posterisable set on its own, with no rect list and no knowledge of
 * windows needed from the caller.  The gap between the two `posterisable=` columns IS the
 * exclusion set, measured rather than enumerated, which is the answer LANE 9's Gate cell asks
 * the caller to make.  And the census prices its own decision: a caller that learns the flat
 * runs by scanning pays a pass over the canvas, so the row prints that cost in the same
 * ns/px units as the copy floor it is being set against.  ANSWERED 2026-10-14, and the answer is
 * the refutation: the decision eats the saving -- 11,498 us for one compare per pixel against a
 * `bg=` of 7,499 us for the same 480,000 px, and 53,935 us for the scan that would return spans --
 * so no per-pass census can pay, whatever the canvas is showing, and the lane closed as REFUTED
 * rather than as a rect list.  LANE 9's Price cell carries the rows and the reading; this
 * instrument stays so the inequality can be re-taken if the copy floor ever moves. */
static void ps2_bench_poster_price(GDEV *screen)
{
    const int w = PS2_SCREEN_WIDTH, h = PS2_SCREEN_HEIGHT;
    const uint32_t total = (uint32_t)w * (uint32_t)h;
    const uint32_t reps = 16u;            /* the canvas scan's own timing reps */
    ps2_poster_shape bg_shape, live_shape;
    uint32_t saved_us, scan_us = 0u, ns_px, frac100, r;
    uint32_t lean_us = 0u, lean_ns = 0u;
    COLOR *plate;
    H mx = 0, my = 0;

    ps2_kprintf("\n[BENCH] poster price -- how much of `bg=` is one flat colour\n");
    plate = (COLOR *)malloc((size_t)total * sizeof(COLOR));
    if (!plate) {
        ps2_kprintf("[BENCH] poster price: SKIP -- the heap holds no %u KiB plate (largest free %u B)\n",
                    (unsigned int)(total * sizeof(COLOR) / 1024u),
                    (unsigned int)ps2_heap_largest_free());
        return;
    }
    {
        GDEV ref = *screen;
        ref.pixels = plate;
        render_desktop_background(&ref);   /* the cache copy, not a procedural rebuild */
    }

    ps2_bench_poster_scan(plate, plate[0], &bg_shape);

    /* The first row that is entirely one colour is the fill colour's witness: the
     * dot grid starts at y=30, so a plate whose row 0 is uniform is a plate painted
     * by fill_rec() and nothing else has been asked of it. */
    ps2_kprintf("[BENCH] poster price: flat=0x%08x (witness row %d of %d rows) flat=%u px of %u\n",
                (unsigned int)plate[0], bg_shape.witness_row, h,
                (unsigned int)bg_shape.flat_px, (unsigned int)total);

    saved_us = bg_shape.poster * 157u / 10000u;   /* 15.7 ns/px, the copy floor, in µs */
    ps2_kprintf("[BENCH] poster price: posterisable=%u px  ee keeps=%u px  trapped=%u px (inside an art row's box)\n",
                (unsigned int)bg_shape.poster, (unsigned int)(total - bg_shape.poster),
                (unsigned int)(bg_shape.flat_px - bg_shape.poster));
    ps2_kprintf("[BENCH] poster price: rows flat=%u art=%u  rects=%u  bands=%u  art bbox=%u px\n",
                (unsigned int)((uint32_t)h - bg_shape.rows_art), (unsigned int)bg_shape.rows_art,
                (unsigned int)bg_shape.rects, (unsigned int)bg_shape.bands,
                (unsigned int)bg_shape.art_bbox_px);
    ps2_kprintf("[BENCH] poster price: at the 15.7 ns/px copy floor the poster takes %u us off one whole-canvas bg= pass\n",
                (unsigned int)saved_us);
    ps2_kprintf("[BENCH] poster price: control, same scan told to believe 0x%08x -- posterisable=%u px (a broken match would print the same big number)\n",
                (unsigned int)((uint32_t)plate[0] + 1u), (unsigned int)bg_shape.poster_ctl);

    /* The census, on the live canvas and with the PLATE's colour as the truth -- the caller
     * decides the colour once and then only asks what equals it.  Timed over `reps` because
     * one pass of this table is what the machine's own drift, not the result, would decide. */
    {
        const uint32_t t0 = ps2_count_read();
        for (r = 0u; r < reps; r++) ps2_bench_poster_scan(screen->pixels, plate[0], &live_shape);
        scan_us = ps2_us_since(t0) / reps;      /* one full-canvas decision, in µs */
    }
    /* And the cheapest decision a caller could possibly write -- one compare per pixel,
     * no control arm, no per-row box.  It is timed because the refutation this phase is
     * about to make must not rest on my instrument's double match.  `flat_run` is summed
     * over the reps and printed so the loop cannot be folded away. */
    {
        const COLOR *cv0 = (const COLOR *)screen->pixels;
        const COLOR want = plate[0];
        const uint32_t t0 = ps2_count_read();
        uint32_t flat_run = 0u, i;

        for (r = 0u; r < reps; r++) {
            for (i = 0u; i < total; i++) {
                if (cv0[i] == want) flat_run++;
            }
        }
        lean_us = ps2_us_since(t0) / reps;
        lean_ns = (lean_us * 1000u) / total;
        ps2_kprintf("[BENCH] poster census: the cheapest possible decision, one compare per pixel, costs %u us = %u ns/px and it counts %u flat px per pass\n",
                    (unsigned int)lean_us, (unsigned int)lean_ns,
                    (unsigned int)(flat_run / reps));
    }
    get_baremetal_mouse_pos(&mx, &my);
    ps2_kprintf("[BENCH] poster census (live canvas): posterisable=%u px  ee keeps=%u px  trapped=%u px  flat=%u px of %u\n",
                (unsigned int)live_shape.poster, (unsigned int)(total - live_shape.poster),
                (unsigned int)(live_shape.flat_px - live_shape.poster),
                (unsigned int)live_shape.flat_px, (unsigned int)total);
    ps2_kprintf("[BENCH] poster census (live canvas): rows flat=%u art=%u  rects=%u  bands=%u  art bbox=%u px  pointer at (%d,%d)\n",
                (unsigned int)((uint32_t)h - live_shape.rows_art), (unsigned int)live_shape.rows_art,
                (unsigned int)live_shape.rects, (unsigned int)live_shape.bands,
                (unsigned int)live_shape.art_bbox_px, (int)mx, (int)my);
    ps2_kprintf("[BENCH] poster census: the plate offers %u px and the canvas %u px -- %u px is foreground the canvas carries and the cached plate does not, and that is the exclusion set counted rather than enumerated\n",
                (unsigned int)bg_shape.poster, (unsigned int)live_shape.poster,
                (unsigned int)(bg_shape.poster - live_shape.poster));
    ps2_kprintf("[BENCH] poster census: control, same scan told to believe 0x%08x -- posterisable=%u px\n",
                (unsigned int)((uint32_t)plate[0] + 1u), (unsigned int)live_shape.poster_ctl);
    /* Two witnesses that say WHICH canvas this is, because the census's first readings
     * returned a canvas that is almost not the plate at all, and a number that surprising
     * is not quoted until it is identified.  `differs=` is a plain per-pixel inequality
     * against the plate the compositor paints from.  `its own colour=` asks the same shape
     * scan what the canvas's OWN top-left value is worth: if the live canvas carried the
     * same picture in a different byte order -- which would make the plate's colour
     * unmatchable without the canvas being busy -- that shows up as a large posterisable
     * there beside a large `differs=` above it.  Identified 2026-10-14: the canvas's own
     * colour is flat over 14,705 px and the swapped word appears in 0 px, so the big
     * `differs=` is a busy workbench, not a second byte order. */
    {
        const COLOR *cv = (const COLOR *)screen->pixels;
        ps2_poster_shape own;
        uint32_t diff_px = 0u, i;

        for (i = 0u; i < total; i++) {
            if (cv[i] != plate[i]) diff_px++;
        }
        ps2_bench_poster_scan(cv, cv[0], &own);
        ps2_kprintf("[BENCH] poster census: canvas[0]=0x%08x plate[0]=0x%08x  differs=%u px of %u\n",
                    (unsigned int)cv[0], (unsigned int)plate[0],
                    (unsigned int)diff_px, (unsigned int)total);
        /* The byte-order question, in one number: if the canvas is the same picture with
         * R and B exchanged, the R/B-swapped plate word is the canvas's dominant match and
         * `differs=` above is the swap rather than the foreground.  ANSWERED 2026-10-14: the
         * swapped word appears in 0 px, 374,925 px differ in RGB and 170 differ only in alpha,
         * so it is the SCENE and the byte-order law (2.1) is not in question here. */
        {
            const COLOR want = plate[0];
            const COLOR swap = (COLOR)((want & 0xff00ff00u) |
                                       ((want & 0xffu) << 16) | ((want >> 16) & 0xffu));
            uint32_t n_swap = 0u, n_rgb = 0u;

            for (i = 0u; i < total; i++) {
                if (cv[i] == swap) n_swap++;
                if (((uint32_t)cv[i] ^ (uint32_t)plate[i]) & 0x00ffffffu) n_rgb++;
            }
            ps2_kprintf("[BENCH] poster census: the canvas holds the R/B-swapped plate word 0x%08x in %u px -- its centre is 0x%08x where the plate's centre is 0x%08x\n",
                        (unsigned int)swap, (unsigned int)n_swap,
                        (unsigned int)cv[(size_t)(h / 2) * w + w / 2],
                        (unsigned int)plate[(size_t)(h / 2) * w + w / 2]);
            /* The alpha question, in the same shape: `differs=` above counts a pixel as
             * different if ANY of its 32 bits differ, so a canvas that carries the plate's
             * picture with a different alpha byte would look like a canvas covered in
             * foreground.  This is the second decisive reading and it separates the two:
             * pixels whose RED/GREEN/BLUE differ. */
            ps2_kprintf("[BENCH] poster census: pixels whose RGB differs from the plate's: %u of %u -- so of the %u words that differ by all 32 bits, %u differ ONLY in the alpha byte\n",
                        (unsigned int)n_rgb, (unsigned int)total,
                        (unsigned int)diff_px, (unsigned int)(diff_px - n_rgb));
        }
        ps2_kprintf("[BENCH] poster census: the canvas's OWN colour 0x%08x gives posterisable=%u px flat=%u px witness row %d rects=%u -- if this is the big number and differs is one too, the plate and the canvas are the same picture in two orders\n",
                    (unsigned int)cv[0], (unsigned int)own.poster, (unsigned int)own.flat_px,
                    own.witness_row, (unsigned int)own.rects);
    }
    {
        const uint32_t ns_sum = scan_us * 1000u;
        ns_px = ns_sum / total;
        frac100 = ((ns_sum % total) * 100u) / total;
    }
    ps2_kprintf("[BENCH] poster census: deciding costs %u us for the %u px of one canvas = %u.%02u ns/px -- the scan a caller pays to learn which runs are flat -- and the poster takes %u us off bg=, so the net is %s%u us\n",
                (unsigned int)scan_us, (unsigned int)total,
                (unsigned int)ns_px, (unsigned int)frac100, (unsigned int)saved_us,
                saved_us >= scan_us ? "" : "MINUS ",
                (unsigned int)(saved_us >= scan_us ? saved_us - scan_us : scan_us - saved_us));
    free(plate);
}

/* The phase table.  `even`/`odd` are pixel columns, and on this canvas an even
 * column is 8-aligned in bytes while an odd one is 4-mod-8 -- the whole defect, in
 * one character of the tag. */
static void ps2_bench_ram_ops(GDEV *screen)
{
    /* The same column asked of the IMAGE it copies rather than of the leaf that copies
     * it: what a GS poster could take off `bg=`, and the shape it would have to take it
     * in.  This is lane 9b's pre-gate -- the price, before the primitive.
     *
     * It runs FIRST in this phase, and not by preference: every ps2_bench_ram() row below
     * writes into the canvas the census wants to read, and the run that first took the
     * census proved it -- called after them, the live canvas came back `flat=0 px of
     * 480000, rows art=600 rects=1`, which is one uniform colour, and it was the last
     * fill row's own test colour rather than the desktop.  Ahead of the table the canvas
     * still holds the pass the loop last presented, which is the only canvas a present
     * could have uploaded. */
    ps2_kprintf("\n[BENCH] RAM-to-RAM, the loop or the DRAM -- same bytes at two instruction costs\n");
    ps2_bench_poster_price(screen);

    /* One rect, five shapes.  A 400x16 band is the size a real window's damage
     * column costs and the shape both of the compositor's hot leaves use; the odd
     * rows are 399 wide so that the even and odd pairs move a difference of one
     * pixel rather than one pixel's worth of extra claim. */
    ps2_bench_ram(screen, "copy even",     0, 400, 400, 16,  64u);
    ps2_bench_ram(screen, "copy odd",      0, 401, 399, 16,  64u);
    ps2_bench_ram(screen, "copy odd+peel", 1, 401, 399, 16,  64u);
    ps2_bench_ram(screen, "fill even",     2, 400, 400, 16,  64u);
    ps2_bench_ram(screen, "fill odd",      2, 401, 399, 16,  64u);

    /* The prospective fixes, priced against the defects they would replace: 16 and
     * 32 bytes per iteration for the copy, and fill_rec()'s strip with its row
     * pointer incremented instead of recomputed and its stores four-deep and then
     * eight-deep.  Whatever wins here is what goes into fast_blit.h and dp_core.c;
     * whatever loses stays printed here as the reason it did not.
     *
     * The pairs are interleaved and taken at 256 repetitions rather than run once
     * each, because the first pass of this table decided the shipped copy width from
     * a single row per width, and the machine's own drift between adjacent rows is
     * around 1% while the gap being read is around 5%.  `+16b`/`+32b` are the same
     * two shapes measured again after the other one has run, so a width that wins
     * only in the first slot is a position effect, not a result. */
    ps2_bench_ram(screen, "copy even+16", 3, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "copy even+32", 4, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "copy odd+16",  3, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "copy odd+32",  4, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "copy odd+16b", 3, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "copy odd+32b", 4, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "fillunr even", 5, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "fillunr8 even",6, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "fillunr odd",  5, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "fillunr8 odd", 6, 401, 399, 16, 256u);

    /* The compositor's third hot column is neither of the two leaves above: it is
     * wnd.c's client-area walk, and that walk reads every pixel of every row to
     * decide whether the row may be copied at all (src/window/wnd.c:703-719).  The
     * copy rows price 15.7 ns/px and production's blit= column bills around 40, and
     * the gap is this second pass, not the DRAM.  `blit scan` is the walk as it
     * stands; `blit wide` is the same decision two pixels per load -- and BOTH of
     * them still read the band twice, which is what the fused rungs below are for.
     *
     * The ladder is read the way 10.1's width pairs are read: interleaved, and with
     * the shapes taken again in the other order (` b` rows) after every other shape
     * has run, because the drift between adjacent rows here is about 1% and a fused
     * rung that beats `blit wide` only in its own slot is a position effect.  `f4w`
     * is the shape that can ship -- one 32-bit access per pixel, so no shared 8-byte
     * phase between a window's row and the canvas's row is required -- and the
     * pair-width rungs are what say whether that costs anything.
     *
     * Then the same shapes on a band with holes in it, one pixel in eight replaced by
     * the transparent sentinel: the regime where every group test fails and a fused
     * rung pays its test and its fixup.  A rung that wins the clean rows and loses the
     * hole rows is not taken, which is why both blocks are in one phase rather than
     * one of them being a claim. */
    ps2_bench_ram(screen, "blit scan",   7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit wide",   8, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f2",     9, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f4",    10, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8",    11, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8b",   12, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f4w",   13, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8w",   14, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f16w",  15, 400, 400, 16, 256u);
    /* The shipped row, priced next to the bench's lookalike: mode 16 calls the function
     * wnd.c's walk calls, so `blit ship` and `blit f16w` differ only in the source the
     * compiler saw.  If they disagree, the number that ships is the one on `ship`. */
    ps2_bench_ram(screen, "blit ship",  16, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit wide b", 8, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit scan b", 7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8 b",  11, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f4w b", 13, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8w b", 14, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit ship b", 16, 400, 400, 16, 256u);

    /* The odd column phase, where the row starts 4-mod-8 and the pair shapes have to
     * peel one pixel before they can load a pair -- the same defect fast_blit.h's
     * comment names, priced on the fused shapes rather than on the copy. */
    ps2_bench_ram(screen, "blit o scan", 7, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o wide", 8, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f4",  10, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f4w", 13, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f8w", 14, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f16w", 15, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o ship", 16, 401, 399, 16, 256u);

    /* The hole regime, at the same rect and the same repetition count. */
    s_ram_holes_period = 8u;
    ps2_bench_ram(screen, "blit h scan",  7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h wide",  8, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f4",   10, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f8",   11, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f8b",  12, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f4w",  13, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f8w",  14, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f16w", 15, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h scanb",  7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h ship", 16, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f4w b", 13, 400, 400, 16, 256u);
    s_ram_holes_period = 0u;

    /* The sweep, at the phase that can never fall into the tail, over the range the
     * book's flat-price claim covers.  The two shapes are the compositor's own two
     * hot columns: `bg=` is a btron_row_blit() of the cached background over the
     * damage rect (480,000 px for a whole canvas, 9.9 ms measured), and `frame=` is
     * fill_rec() plates (715,360 px, 13.1 ms), so the copy rows can be read against
     * the first and the fill rows against the second.  A copy needs a source band as
     * large as its destination, so the canvas only holds two of them up to 300 rows:
     * 240,000 px is as far as a copy can be taken here, half of what `bg=` bills,
     * and the whole-canvas 480,000 px row is a fill for that reason, not by choice. */
    ps2_kprintf("[BENCH]   -- size sweep, even phase, load+store and store-only\n");
    ps2_bench_ram(screen, "copy 768px",    0, 400,  48,  16, 256u);
    ps2_bench_ram(screen, "copy 12800px",  0,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "copy 12800+32", 4,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800",    7,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800w",   8,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800fw", 13,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800f8w",14,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800f16w",15,  0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800ship",16,  0, 800,  16,  64u);
    ps2_bench_ram(screen, "copy 80000px",  0,   0, 800, 100,   8u);
    ps2_bench_ram(screen, "copy 240000px", 0,   0, 800, 300,   3u);
    ps2_bench_ram(screen, "fill 768px",    2, 400,  48,  16, 256u);
    ps2_bench_ram(screen, "fill 12800px",  2,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "fill 80000px",  2,   0, 800, 100,   8u);
    ps2_bench_ram(screen, "fill 480000px", 2,   0, 800, 600,   2u);
    /* And the same canvas through the unrolled leaf, at the count `bg=` bills, so
     * the fill fix is priced at its real size and not only at a strip, four-deep
     * against eight. */
    ps2_bench_ram(screen, "fillunr 480k",  5,   0, 800, 600,   2u);
    ps2_bench_ram(screen, "fillunr8 480k", 6,   0, 800, 600,   2u);
}

/* One direction's worth of the loop's own pass -- report, spend, dispatch, bands,
 * present -- in that order, so the rows, bands and cost columns mean the same thing
 * here that they mean in a live session's [PSTAT] window.
 *
 * The move alternates sign every pass: a one-way sweep reaches the border in a
 * handful of passes and then measures the border rather than the present, and the
 * `wall` column is what says when that has happened anyway. */
static void ps2_bench_move(GDEV *screen, const char *tag, int dx, int dy, uint32_t passes)
{
    uint32_t area_sum = 0u, area_max = 0u, bands_sum = 0u, bands_max = 0u;
    uint32_t r_sum = 0u, s_sum = 0u, u_sum = 0u, px_sum = 0u;
    uint32_t empty = 0u, wall = 0u;
    const uint32_t t_start = ps2_count_read();
    RENDER_STATS st;
    EVT ev;

    /* Mid-canvas, and one present of wherever that leaves the sprite, so the
     * re-centring's own cost is not charged to the phase. */
    ptr_cal_set(PS2_SCREEN_WIDTH / 2, PS2_SCREEN_HEIGHT / 2);
    while (get_evt(&ev, 0) == E_OK) workbench_process_event(screen, &ev);
    {
        ps2_bandset_t warm = { { { 0, 0, 0, 0 } }, 0, 0 };
        uint32_t r, s, u, px;

        ps2_bands_for_pass(&warm);
        if (warm.n) ps2_paint_bands(screen, &warm, &r, &s, &u, &px);
    }
    btron_render_stats_take(&st);
    s_shown_x = s_mouse_x;
    s_shown_y = s_mouse_y;

    for (uint32_t i = 0; i < passes; i++) {
        ps2_bandset_t set = { { { 0, 0, 0, 0 } }, 0, 0 };
        const int x0 = s_mouse_x, y0 = s_mouse_y;
        const int sign = (i & 1u) ? -1 : 1;
        uint32_t r, s, u, px;

        ptr_cal_send(dx * sign, dy * sign);
        while (get_evt(&ev, 0) == E_OK) workbench_process_event(screen, &ev);
        ps2_bands_for_pass(&set);
        if (!set.n) { empty++; continue; }
        {
            const int mx = s_mouse_x - x0, my = s_mouse_y - y0;
            px_sum += (uint32_t)(mx < 0 ? -mx : mx) + (uint32_t)((my < 0 ? -my : my));
            if (!mx && !my) wall++;
        }
        ps2_paint_bands(screen, &set, &r, &s, &u, &px);
        area_sum += px;  if (px > area_max) area_max = px;
        bands_sum += (uint32_t)set.n;  if ((uint32_t)set.n > bands_max) bands_max = (uint32_t)set.n;
        r_sum += r; s_sum += s; u_sum += u;
        s_shown_x = s_mouse_x;
        s_shown_y = s_mouse_y;
    }

    ps2_kprintf("[BENCH] %-9s move %d,%d  passes=%u moved px/pass=%u.%02u wall=%u empty=%u  present px %u/%u  bands %u.%02u/%u\n",
                tag, dx, dy, (unsigned int)passes,
                (unsigned int)(px_sum / passes), (unsigned int)(px_sum * 100u / passes % 100u),
                (unsigned int)wall, (unsigned int)empty,
                (unsigned int)area_max, (unsigned int)(area_sum / passes),
                (unsigned int)(bands_sum / passes), (unsigned int)(bands_sum * 100u / passes % 100u),
                (unsigned int)bands_max);
    /* passes/s is the number the cursor's frame rate actually is: one pass paints
     * one position, so the present's cost divided into a second is the pointer's
     * refresh, in both directions, from the same machine.
     *
     * The numerator is 1e6 and not a tick count because ps2_us_since() has already spent
     * the divisor on the way out; the 147000 this row carried was ticks-per-millisecond
     * over a microsecond denominator, which priced every direction 6.8x slower than the
     * `us/pass` column beside it.  Staying 32-bit costs nothing here: the phase's own pass
     * count is 40, and the numerator only overflows past 4294 of them.
     *
     * The wall is tested rather than the Count reading because a direction whose every
     * pass found nothing to paint returns inside one tick, and this image divides by
     * hardware: a zero denominator is a `divu` nobody traps, which prints a rate rather
     * than the absence of one. */
    const uint32_t wall_us = ps2_us_since(t_start);
    ps2_kprintf("[BENCH]           us/pass render %u swap %u upload %u total %u  -> %u passes/s\n",
                (unsigned int)(r_sum / passes), (unsigned int)(s_sum / passes),
                (unsigned int)(u_sum / passes),
                (unsigned int)((r_sum + s_sum + u_sum) / passes),
                (unsigned int)(wall_us ? passes * 1000000u / wall_us : 0u));
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u(%u px skip=%u) paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.frame_body_px,
                (unsigned int)st.frame_body_skip_px,
                (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
}

static void ps2_gui_pass(GDEV *screen);

/* Drain the queue and dispatch it, counting what came out by shape.  This is the
 * same pair of calls the session loop makes, laid out so a measurement can see the
 * middle of it: the live host device is still sending reports during a bench run --
 * the 2026-10-09 log has the emulator pushing 0,-127 until the cursor pins itself to
 * the top wall -- so a phase that only asked "did the menu open" could not tell its
 * own injected byte from that traffic.  Counting the edges it dispatched, and printing
 * the position they carried, makes the two distinguishable. */
static int ps2_bench_drain(GDEV *screen, int *moves, int *buttons, int *bx, int *by)
{
    EVT ev;
    int n = 0;

    *moves = *buttons = 0;
    while (n < 512 && get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_MOUSE_MOVE) {
            (*moves)++;
        } else if (ev.type == EV_BUT_DOWN || ev.type == EV_BUT_UP) {
            (*buttons)++;
            *bx = ev.pos.x;  *by = ev.pos.y;
        }
        /* The first thing to come out of a queue is the one that says whether this
         * TU and the queue's agree on what a type is: a number outside EV_NONE..EV_TIMER
         * here, with the counts still zero, is a layout mismatch rather than a lost
         * event, and the two need different fixes. */
        if (n == 0) ps2_kprintf("[BENCH] drain: first out type=%d (EVT is %d bytes, move=%d but=%d,%d)\n",
                                (int)ev.type, (int)sizeof(EVT),
                                (int)EV_MOUSE_MOVE, (int)EV_BUT_DOWN, (int)EV_BUT_UP);
        workbench_process_event(screen, &ev);
        n++;
    }
    return n;
}

/* ── Click phase: is the guest's own event chain alive? ─────────────────────
 *
 * "The cursor moves but clicking does nothing" has two authors, and a live session
 * cannot tell them apart from the inside: the emulator never publishing a button
 * byte, or this port's dispatch being dead.  A moving cursor is not evidence about
 * the second one -- ps2_move_mouse() paints the sprite itself, so the cursor can be
 * perfectly smooth while no event reaches any window.  So this injects a report's
 * worth of buttons with no motion in it at all (ps2_usb_inject_mouse() is the same
 * entry point a report uses) and asks the deskbar whether it opened.
 *
 * The first run, 2026-10-09, printed `menu_open=0` for a click the queue had accepted
 * (`r=0`) with no `[WM] b=` row behind it anywhere in 46 s of log -- while the hover
 * column showed a move *had* been dispatched.  Both cannot be true of one queue, so
 * the phase now measures the queue itself instead of inferring it from a row the pass
 * may or may not have printed. */
static void ps2_bench_click(GDEV *screen)
{
    int moves, buttons, bx = -1, by = -1;
    int dbx = -1, dby = -1;
    int hover_park, hover_click, opened, active, still_open, got_down;

    ps2_kprintf("\n[BENCH] synthetic deskbar click -- button byte only, no motion\n");

    /* The queue alone, with nothing between the two calls: no poll, no pass, no
     * report.  This is the smallest question this port can ask of event.c -- does an
     * event I put in come back out with the fields I put on it -- and the 2026-10-09
     * run earned it by accepting fifteen events (`r=0`) and handing none of them to
     * get_evt for 46 seconds.  A type or xy that comes back wrong is a struct layout
     * the two translation units disagree about; E_OK going in and not E_OK coming
     * straight back is the queue itself. */
    {
        EVT one, out;
        ER in_r, out_r;

        tkl_memset(&one, 0, sizeof(one));
        one.type = EV_BUT_DOWN;
        one.pos.x = 60;  one.pos.y = 12;  one.button = 1;
        in_r = snd_evt(&one);
        out_r = get_evt(&out, 0);
        ps2_kprintf("[BENCH] echo: snd=%d get=%d type=%d xy=%d,%d btn=%d (%d byte EVT, move=%d but=%d,%d)\n",
                    (int)in_r, (int)out_r, (int)out.type, out.pos.x, out.pos.y,
                    (int)out.button, (int)sizeof(EVT),
                    (int)EV_MOUSE_MOVE, (int)EV_BUT_DOWN, (int)EV_BUT_UP);
    }

    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_kprintf("[BENCH] click: backlog drained %d moves %d buttons (last at %d,%d)\n",
                moves, buttons, bx, by);

    /* ［BTRON］ is g_headers[0].rect = {4,2,90,23}; the row is the launcher, so the
     * same coordinates answer the "add Terminal to the launcher" question with a
     * window open on screen.  Park there and dispatch that move: a hover highlight
     * is the click's own hit-test one stage earlier, so the two together say whether
     * the deskbar can see the pointer at all. */
    ptr_cal_set(60, 12);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    hover_park = global_menu_get_hover_header();
    ps2_kprintf("[BENCH] click: parked at 60,12 -- dispatched %d moves, hover_header=%d\n",
                moves, hover_park);

    ps2_usb_inject_mouse(1, 0, 0);           /* left DOWN, zero counts */
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    got_down = buttons;  dbx = bx;  dby = by;
    opened = tracker_is_menu_open();
    active = global_menu_get_active();
    hover_click = global_menu_get_hover_header();
    ps2_gui_pass(screen);                    /* and show it: the menu is an overlay,
                                              * so this is the pass that would paint it */

    ps2_usb_inject_mouse(0, 0, 0);           /* and UP at the same pixel */
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    still_open = tracker_is_menu_open();     /* the menu opens on the press, so it
                                              * must still be up after the release */

    /* Leave the desktop as found: a second edge at the same header is this bar's own
     * toggle, and a bench that ends with a menu over the icon grid is measuring the
     * next phase's screen, not this one's. */
    ps2_usb_inject_mouse(1, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(0, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);

    ps2_kprintf("[BENCH] click: down_dispatched=%d at %d,%d active=%d hover %d->%d menu_open=%d still=%d\n",
                got_down, dbx, dby, active, hover_park, hover_click, opened, still_open);
    ps2_kprintf("[BENCH] click: %s\n",
                opened ? "guest chain OK -- a button byte does open the launcher, so a live click is lost before this port sees it"
                       : (got_down ? "DISPATCHED BUT DEAD -- the queue gave the deskbar a button edge and nothing opened"
                                   : "NOT IN THE QUEUE -- the edge was accepted by snd_evt and never came back out"));
}

/* The sprite's own nine leading pixels, straight out of cur_mask[] and cur_outline[] in
 * desktop.c: row 0 white,black; row 1 white,white,black; row 2 white,white,white,black.
 * A single white-then-black pair is not enough to call it the cursor -- window text is
 * exactly that pattern, and the first version of this probe reported a hit at 468,106
 * where no cursor is.  Nine pixels over three rows in that order is nothing else this
 * desktop composes.  COLOR_WHITE and COLOR_BLACK are both fixed points of
 * BTRON_PIXEL_FB(), so this test reads a canvas word as it lies. */
static BOOL ps2_cursor_sig_at(const uint32_t *p, int x, int y)
{
    const uint32_t W = (uint32_t)COLOR_WHITE, B = (uint32_t)COLOR_BLACK;

    if (x + 4 >= PS2_SCREEN_WIDTH || y + 3 >= PS2_SCREEN_HEIGHT) return FALSE;

    const uint32_t *r0 = p + (uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x;
    const uint32_t *r1 = r0 + PS2_SCREEN_WIDTH;
    const uint32_t *r2 = r1 + PS2_SCREEN_WIDTH;

    return r0[0] == W && r0[1] == B &&
           r1[0] == W && r1[1] == W && r1[2] == B &&
           r2[0] == W && r2[1] == W && r2[2] == W && r2[3] == B;
}

/* Every hit of that signature on a surface, up to `want`, as x,y pairs.  A whole
 * canvas walk is 480,000 tests; the bench has no frame budget, a live pass does not. */
static int ps2_cursor_scan(const uint32_t *p, int *out, int want)
{
    int n = 0;

    for (int y = 0; y < PS2_SCREEN_HEIGHT && n < want; y++) {
        for (int x = 0; x < PS2_SCREEN_WIDTH && n < want; x++) {
            if (!ps2_cursor_sig_at(p, x, y)) continue;
            out[2 * n] = x;  out[2 * n + 1] = y;
            n++;
        }
    }
    return n;
}

/* ── Cursor phase: does the sprite reach the screen memory at all? ───────────
 *
 * "The mouse is not even visible on move" has three authors on this target: the sprite
 * is never drawn into s_desktop_backbuffer, it is drawn but the pass's present region
 * misses it, or it reaches the GS frame buffer and the display setup does not show it.
 * The guest can separate the first two by itself, because both surfaces are plain RDRAM
 * it owns and both are readable after the pass -- which is why this phase asks where the
 * sprite is, rather than whether the user can see it.
 *
 * The author it found on 2026-10-09 was none of those three and is not in this file:
 * the banded append is a five-argument call into desktop.c, the image was linked from
 * half -march=mips3 and half -march=mips2 objects, and those two pass argument five in
 * different places ($t0 versus the stack), so the sprite's canvas height arrived as
 * garbage and every row of the draw failed its own clip.  A whole-canvas pass still
 * showed the pointer, because that branch draws it from inside desktop.c, where the call
 * stays within one ISA.  The per-region pre=/post= pair is what distinguishes that
 * failure from a present that misses: post=0 with the append flagged as run is the
 * writer not writing, and the verdict says so by name.  The link is one calling
 * convention now (see the ABI note in the Makefile), which is what the post=1 rows in a
 * current run say. */
static void ps2_bench_cursor(GDEV *screen)
{
    static const int park[2][2] = { { 300, 320 }, { 420, 320 } };
    const uint32_t *bb = (const uint32_t *)s_desktop_backbuffer;
    const uint32_t *fb = ps2_gs_get_framebuffer();
    int moves, buttons, px = -1, py = -1;
    int hb[8];
    int bare_bb = 0, elsewhere = 0, append_failed = 0;

    ps2_kprintf("\n[BENCH] cursor presence -- is the sprite in the canvas?\n");
    /* One canvas now, so the two addresses are the same number and the row is the
     * boot-time proof of it: the DMA and the compositor share one array, and a sprite
     * that is in one is in the other.  The question this probe used to answer with a
     * second surface -- "drawn but never presented" -- has moved to the region rows
     * below, which say whether the present ever went where the sprite was drawn. */
    ps2_kprintf("[BENCH] cursor: surfaces bb=0x%x fb=0x%x\n",
                (unsigned)(uintptr_t)bb, (unsigned)(uintptr_t)fb);

    for (int k = 0; k < 2; k++) {
        const int x = park[k][0], y = park[k][1];
        H bmx = 0, bmy = 0;

        /* Drain before parking as well as after: the queue and this port's report
         * accumulator both still hold the earlier phases' motion, and workbench_process_event
         * rewrites desktop.c's position from every event it pops.  Parking into that
         * backlog is how the first run of this probe came to paint at 326,180 while
         * testing at the 300,320 it had asked for. */
        ps2_bench_drain(screen, &moves, &buttons, &px, &py);
        ptr_cal_set(x, y);
        s_paint_calls = 0u;
        ps2_gui_pass(screen);
        const uint32_t bands = s_paint_calls;

        /* Tested at desktop.c's position, because that is the only coordinate either
         * render branch uses: workbench_render() and the banded branch both ask
         * get_baremetal_mouse_pos() for the sprite's top-left.  s_mouse (what the band
         * math swept) and s_shown (what the port believes was last presented) are
         * printed next to it, so a disagreement between the three is readable without
         * another build -- and a sprite that is genuinely absent at the drawn position
         * cannot be excused by one. */
        get_baremetal_mouse_pos(&bmx, &bmy);
        bare_bb = ps2_cursor_sig_at(bb, (int)bmx, (int)bmy);

        const int nb = bare_bb ? 0 : ps2_cursor_scan(bb, hb, 4);
        elsewhere += (!bare_bb && nb > 0);

        ps2_kprintf("[BENCH] cursor: park %d,%d drawn=%d,%d mouse=%d,%d shown=%d,%d in_bb=%d | sig at drawn: bb=%d\n",
                    x, y, (int)bmx, (int)bmy, s_mouse_x, s_mouse_y, s_shown_x, s_shown_y,
                    g_cursor_in_backbuffer, bare_bb);
        /* A whole-canvas walk is only worth its cost when the sprite is not where the
         * port says it drew it: then the row names the places it is instead. */
        if (!bare_bb) {
            ps2_kprintf("[BENCH] cursor: canvas hits bb=%d(%d,%d %d,%d %d,%d %d,%d)\n",
                        nb, hb[0], hb[1], hb[2], hb[3], hb[4], hb[5], hb[6], hb[7]);
        }
        /* What the pass actually covered.  If its union does not contain the sprite's
         * own box at the position that was drawn, the sprite is not missing because the
         * composite forgot to draw it -- the present never went there.  If it does and
         * the signature is still absent, the draw itself is the thing not happening.
         * (The union is a bounding box, so with more than one region it is generous;
         * the region count is printed beside it for exactly that reason.) */
        const int covers = bands > 0u &&
                           s_paint_union[0] <= (int)bmx &&
                           s_paint_union[1] <= (int)bmy &&
                           s_paint_union[2] >= (int)bmx + 16 &&
                           s_paint_union[3] >= (int)bmy + 16;
        ps2_kprintf("[BENCH] cursor: pass made %u region(s), union %d,%d..%d,%d covers sprite %s\n",
                    (unsigned int)bands,
                    s_paint_union[0], s_paint_union[1], s_paint_union[2], s_paint_union[3],
                    covers ? "YES" : "no");
        /* One row per region of this pass.  `sprite=` is the position the cursor draw
         * used inside that region (2 = the whole-canvas composite, which draws it too),
         * so a sprite that is missing from the surfaces has a named region that failed
         * to draw it rather than an inference from the pass's total. */
        for (unsigned i = 0; i < bands && i < PS2_PAINT_TRACE_MAX; i++) {
            ps2_kprintf("[BENCH] cursor:  r%u %d,%d..%d,%d full=%d sprite=%d at %d,%d\n",
                        i,
                        s_paint_rect[i][0], s_paint_rect[i][1],
                        s_paint_rect[i][2], s_paint_rect[i][3],
                        s_paint_full[i], s_paint_appended[i],
                        s_paint_appended_at[i][0], s_paint_appended_at[i][1]);
            ps2_kprintf("[BENCH] cursor:     pixels=0x%x | sig pre=%d post=%d\n",
                        s_paint_pix[i], s_paint_pre[i], s_paint_post[i]);
            if (s_paint_appended[i] == 1 && s_paint_post[i] == 0) append_failed = 1;
        }

        /* What is actually there, as a picture rather than a hex dump: W, B or . over
         * the sprite's own 16x16 box.  The shape is unmistakable, and its absence is
         * too -- which is the point of reading the grid instead of more numbers. */
        if (!bare_bb) {
            for (int r = 0; r < 16; r++) {
                char line[20];
                for (int c = 0; c < 16; c++) {
                    const uint32_t v = bb[(uint32_t)(bmy + r) * PS2_SCREEN_WIDTH + (uint32_t)(bmx + c)];
                    line[c] = (v == (uint32_t)COLOR_WHITE) ? 'W'
                            : (v == (uint32_t)COLOR_BLACK) ? 'B' : '.';
                }
                line[16] = '\0';
                ps2_kprintf("[BENCH] cursor bb  %2d %s\n", r, line);
            }
        }
    }

    ps2_kprintf("[BENCH] cursor: %s\n",
                bare_bb
                    ? "sprite is at the drawn position in the canvas -- the guest paints the words the GIF is given, so an invisible cursor is downstream of this port's paint"
                    : (append_failed ? "the append ran and wrote nothing -- a five-argument call into desktop.c whose fifth argument arrived corrupted, which is what a mixed-MIPS-ABI link does (see the ABI note in the Makefile)"
                                     : (elsewhere ? "sprite is on the canvas but not at the position the append read back -- the two coordinate sources disagree"
                                                  : "sprite is nowhere on the canvas -- it is not being drawn")));
}

/* ── Launch phase: does a launcher row open the application it names? ────────
 *
 * The ［BTRON］ start menu used to be only a window switcher plus four power items;
 * it now carries 端末 (Terminal) and 横断メディアメニュー (XMB) rows too.  A row is
 * pure arithmetic -- tracker_handle_mouse_down() turns a y into an index and calls
 * that item's opener -- so the question worth asking on the target is whether the
 * window the label promises actually appears in the window table.
 *
 * Rows are found by type rather than by index because the list above them is the
 * live window list: a hardcoded row number would measure whatever layout the
 * earlier phases happened to leave behind.
 *
 * Only Terminal is clicked through to completion.  open_xmb_window() is idempotent
 * through a static handle, so a window this phase opened and left standing would
 * make the app phase's own call return early, start no task, and print a false
 * "NO task-driven frame" -- the XMB row is therefore proven to its dispatch (the
 * pointer lands on it, the hover index follows, xmb.c is in the image) and the menu
 * is then dismissed.  gterm is safe to click: it is event-driven, with no task body
 * to run inline on the pass this phase is standing inside. */

/* Windows in the table, and how many of them carry `needle` in the title. */
static int ps2_bench_windows(int *named_out, const char *needle)
{
    const WND *w = get_wnd_list();
    int total = 0, named = 0;

    while (w) {
        total++;
        if (tkl_strstr(w->title, needle)) named++;
        w = w->next;
    }
    *named_out = named;
    return total;
}

/* The launcher, opened the way a hand opens it: park on ［BTRON］ and press. */
static BOOL ps2_bench_launcher_open(GDEV *screen)
{
    int moves, buttons, bx = -1, by = -1;

    ptr_cal_set(60, 12);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(1, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(0, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    return tracker_is_menu_open();
}

/* Park the pointer on the centre of the first row of this type and dispatch the
 * motion, so the deskbar's own hover says whether the pixel landed on the row. */
static BOOL ps2_bench_row(GDEV *screen, TRACKER_CMD_TYPE type,
                          int *row, int *hover, int *rx, int *ry)
{
    const TRACKER *t = tracker_get_state();
    int moves, buttons, bx = -1, by = -1;
    int idx = -1;

    for (H i = 0; i < t->item_count; i++) {
        if (t->items[i].type == type) { idx = i; break; }
    }
    *row = idx;
    if (idx < 0) return FALSE;

    /* The same arithmetic tracker_handle_mouse_down() applies: rows begin 3 px below
     * menu_rect.top and are TRACKER_ITEM_HEIGHT tall. */
    *rx = t->menu_rect.left + 20;
    *ry = t->menu_rect.top + 3 + idx * TRACKER_ITEM_HEIGHT + TRACKER_ITEM_HEIGHT / 2;

    ptr_cal_set(*rx, *ry);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    *hover = (int)tracker_get_state()->hover_index;
    return TRUE;
}

static void ps2_bench_press(GDEV *screen)
{
    int moves, buttons, bx = -1, by = -1;

    ps2_usb_inject_mouse(1, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(0, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);
}

/* ── Launcher ink: which row bands have anything drawn in them? ──────────────
 *
 * "xmb is not added to start launch" is a claim about the screen, and the canvas is
 * now the one array the GS is fed from, so a band with ink in it is a band the screen
 * can show -- what a band blank here means is that the row was never drawn, and a
 * band that is blank while the item table has a label for it names the label, not the
 * present, as the thing the user cannot find.  Whether the present *covered* a drawn
 * band is a different question and the paint trace answers it: the region rows in
 * ps2_bench_cursor() report the union the pass uploaded.
 *
 * The pointer is parked far outside the menu first.  Motion alone does not dismiss
 * the launcher (only a press does, tracker_handle_mouse_down), and a hover-filled
 * NAVY row would report ink for its fill rather than for its glyphs. */
static void ps2_bench_menu_ink(GDEV *screen, int picture_row_a, int picture_row_b)
{
    const TRACKER *t = tracker_get_state();
    const uint32_t *bb = (const uint32_t *)s_desktop_backbuffer;
    int moves, buttons, bx = -1, by = -1;
    int blank = 0;

    if (!tracker_is_menu_open()) {
        ps2_kprintf("[BENCH] menu ink: launcher is closed, nothing to read\n");
        return;
    }

    ptr_cal_set(PS2_SCREEN_WIDTH - 40, PS2_SCREEN_HEIGHT - 40);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);

    const int x0 = t->menu_rect.left + 5;
    const int x1 = t->menu_rect.right - 2;

    ps2_kprintf("[BENCH] menu ink: menu %d,%d..%d,%d, %d row(s), band x %d..%d, hover=%d\n",
                t->menu_rect.left, t->menu_rect.top, t->menu_rect.right, t->menu_rect.bottom,
                (int)t->item_count, x0, x1, (int)t->hover_index);

    for (H i = 0; i < t->item_count; i++) {
        const int y0 = t->menu_rect.top + 3 + i * TRACKER_ITEM_HEIGHT;
        int ink_bb = 0;

        for (int y = y0; y < y0 + TRACKER_ITEM_HEIGHT && y < PS2_SCREEN_HEIGHT; y++) {
            for (int x = x0; x < x1 && x < PS2_SCREEN_WIDTH; x++) {
                const uint32_t b = bb[(uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x];
                if (b != (uint32_t)BTRON_PIXEL_FB(COLOR_WHITE)) ink_bb++;
            }
        }
        ps2_kprintf("[BENCH] menu ink: r%-2d type=%-2d y=%-3d bb_ink=%-5d\n",
                    i, (int)t->items[i].type, y0, ink_bb);
        if (t->items[i].type != TRACKER_CMD_SEPARATOR && i != t->hover_index && ink_bb == 0) blank++;

        if (i == picture_row_a || i == picture_row_b) {
            for (int r = 0; r < TRACKER_ITEM_HEIGHT; r += 2) {
                char line[128];
                int n = 0;

                for (int x = x0; x < x1 && n < 120; x += 4) {
                    int lit = 0;
                    for (int sx = 0; sx < 4 && !lit; sx++)
                        for (int sy = 0; sy < 2 && !lit; sy++) {
                            const int px = x + sx, py = y0 + r + sy;
                            if (px >= PS2_SCREEN_WIDTH || py >= PS2_SCREEN_HEIGHT) continue;
                            if (bb[(uint32_t)py * PS2_SCREEN_WIDTH + (uint32_t)px] != (uint32_t)COLOR_WHITE)
                                lit = 1;
                        }
                    line[n++] = lit ? '#' : '.';
                }
                line[n] = '\0';
                ps2_kprintf("[BENCH] menu ink  r%-2d %2d %s\n", i, r, line);
            }
        }
    }

    ps2_kprintf("[BENCH] menu ink: %s\n",
                blank > 0
                    ? "a labelled row is blank in the compositor's own canvas -- the item exists but its label never reaches the screen, which is what \"not added to the launcher\" looks like from outside"
                    : "every labelled row carries ink in the canvas, and the canvas is what the GIF is given");
    ps2_kprintf("[BENCH] menu ink: blank_rows=%d\n", blank);
}

/* ── Key route: is the launcher operable with no pointer at all? ─────────────
 *
 * On this target "can't run xmb from menu" and "the mouse is not usable to a human"
 * are one complaint: the row is in the menu -- the phase above reads it out of the
 * item table, draws it and presents it -- and the only remaining way to it is Start,
 * arrows, Cross.  So drive the pad itself rather than the tracker's API: a press
 * report, a release report, and the queued events dispatched between them, which is
 * the same chain a hand moves.  The button word is active-low, hence the inversion.
 *
 * Activation is then demonstrated on the Terminal row, not on the XMB row, because
 * open_xmb_window() does not return while its window is open -- that one is the app
 * phase's job, with a frame budget and an injected Escape.  What is proved here is
 * that Cross acts on the *selected* row, which is the thing a pointer-less user needs.
 */
static void ps2_bench_pad(GDEV *screen, uint16_t mask)
{
    int moves, buttons, bx = -1, by = -1;

    ps2_pad_set_state((uint16_t)~mask, 128, 128, 128, 128);
    ps2_pad_poll();
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_pad_set_state(0xFFFFu, 128, 128, 128, 128);
    ps2_pad_poll();
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);
}

static int ps2_bench_row_of(const TRACKER *t, TRACKER_CMD_TYPE type)
{
    for (H i = 0; i < t->item_count; i++)
        if (t->items[i].type == type) return (int)i;
    return -1;
}

static void ps2_bench_launcher_key_route(GDEV *screen)
{
    const TRACKER *t;
    int hover0, hops = 0, want_row, term_row;
    UW launched0, launched1;
    H px0, py0, px1, py1;

    ps2_kprintf("\n[BENCH] launch by key -- Start, arrows, Cross, pointer not used\n");
    get_baremetal_mouse_pos(&px0, &py0);
    ps2_bench_pad(screen, PAD_START);
    if (!tracker_is_menu_open()) {
        ps2_kprintf("[BENCH] key: Start did not open the launcher, route unmeasured\n");
        return;
    }

    /* The indices are read from the table the open just rebuilt, not carried over
     * from the mouse route: the live-window rows ahead of the application rows change
     * when a window opens, so a number taken before gterm arrived is a different row. */
    t = tracker_get_state();
    want_row = ps2_bench_row_of(t, TRACKER_CMD_XMB);
    term_row = ps2_bench_row_of(t, TRACKER_CMD_TERMINAL);
    hover0 = (int)t->hover_index;
    while (want_row >= 0 && (int)t->hover_index < want_row &&
           hops <= (int)t->item_count + 1) {
        ps2_bench_pad(screen, PAD_DOWN);
        hops++;
        t = tracker_get_state();
    }
    ps2_kprintf("[BENCH] key: hover %d->%d in %d DOWN hops, want row %d, selected='%s'\n",
                hover0, (int)t->hover_index, hops, want_row,
                (t->hover_index >= 0 && t->hover_index < t->item_count)
                    ? t->items[t->hover_index].label : "(none)");
    /* Which surface held the key: workbench_process_event() asks the deskbar first, and
     * global_menu_is_open() answers true for the launcher as well, so a dropdown-less
     * deskbar that claims the arrow leaves the launcher's selection where it was.  The
     * hop count alone cannot separate "arrow not injected" from "arrow swallowed". */
    ps2_kprintf("[BENCH] key: launcher=%d deskbar_open=%d dropdown=%d\n",
                (int)tracker_is_menu_open(), (int)global_menu_is_open(),
                global_menu_get_active());
    ps2_kprintf("[BENCH] key: %s\n",
                (int)t->hover_index == want_row
                    ? "the XMB row is reachable by arrow alone"
                    : "arrow did not land on the XMB row -- the selection is the defect, not the row");

    while (term_row >= 0 && (int)t->hover_index > term_row &&
           hops <= (int)t->item_count * 3) {
        ps2_bench_pad(screen, PAD_UP);
        hops++;
        t = tracker_get_state();
    }
    launched0 = t->launch_count;
    /* The row Cross is about to act on, recorded before the press: the activation closes
     * the menu and hover goes to -1, so reading the table afterwards cannot say which
     * row was chosen.  The UP hops above parked the selection back on the Terminal row
     * deliberately -- XMB's opener does not return while its window is open, so proving
     * the click on the blocking row would hang this phase instead of measuring it. */
    int cross_row = (int)t->hover_index;
    const char *cross_label = (cross_row >= 0 && cross_row < t->item_count)
                                ? t->items[cross_row].label : "(none)";
    ps2_bench_pad(screen, PAD_CROSS);
    launched1 = tracker_get_state()->launch_count;
    get_baremetal_mouse_pos(&px1, &py1);
    ps2_kprintf("[BENCH] key: Cross on row %d '%s' launched %u->%u menu=%d pointer %d,%d->%d,%d\n",
                cross_row, cross_label,
                (unsigned int)launched0, (unsigned int)launched1,
                (int)tracker_is_menu_open(), (int)px0, (int)py0, (int)px1, (int)py1);
    ps2_kprintf("[BENCH] key: %s\n",
                launched1 > launched0
                    ? "Cross activated the highlighted row -- a menu is operable with no pointer"
                    : "Cross activated nothing");
    ps2_kprintf("[BENCH] key: %s\n",
                (px0 == px1 && py0 == py1)
                    ? "the arrows moved the selection only; the pointer stayed put, so nothing recomputed the highlight from a pixel"
                    : "the arrows also displaced the pointer, which re-hovers a row by pixel");
}

static void ps2_bench_launch(GDEV *screen)
{
    int row, hover, rx, ry;
    int named0 = 0, named1 = 0, total0, total1;

    ps2_kprintf("\n[BENCH] launcher rows -- does a row open the application it names?\n");

    if (!ps2_bench_launcher_open(screen)) {
        ps2_kprintf("[BENCH] launch: launcher would not open on a button byte, rows unmeasured\n");
        return;
    }

    total0 = ps2_bench_windows(&named0, "Terminal");
    if (!ps2_bench_row(screen, TRACKER_CMD_TERMINAL, &row, &hover, &rx, &ry)) {
        ps2_kprintf("[BENCH] launch: no 端末 (Terminal) row in the launcher\n");
        return;
    }
    ps2_bench_press(screen);
    total1 = ps2_bench_windows(&named1, "Terminal");
    ps2_kprintf("[BENCH] launch: terminal row=%d xy=%d,%d hover=%d wins %d->%d terminal %d->%d menu=%d\n",
                row, rx, ry, hover, total0, total1, named0, named1, (int)tracker_is_menu_open());
    ps2_kprintf("[BENCH] launch: %s\n",
                named1 > named0 ? "Terminal row opens gterm -- the launcher launches"
                                : "row landed and dispatched, no gterm window in the table");

    if (ps2_bench_launcher_open(screen)) {
        int xmb_hover, xmb_row = -1, xr, yr;
        if (ps2_bench_row(screen, TRACKER_CMD_XMB, &xmb_row, &xmb_hover, &xr, &yr)) {
            ps2_kprintf("[BENCH] launch: xmb row=%d xy=%d,%d hover=%d opener=%s\n",
                        xmb_row, xr, yr, xmb_hover,
                        &open_xmb_window ? "linked" : "absent");
        } else {
            ps2_kprintf("[BENCH] launch: no 横断メディアメニュー (XMB) row in the launcher\n");
        }
        /* Dismiss and reopen before reading the bands: a fresh tracker_open_menu()
         * leaves hover on row 0 only, so an ink count is ink for the label and not for
         * a NAVY fill on the row the pointer was last parked on. */
        ptr_cal_set(400, 450);
        ps2_bench_press(screen);
        if (ps2_bench_launcher_open(screen)) {
            ps2_bench_menu_ink(screen, row, xmb_row);
            ptr_cal_set(400, 450);            /* outside the menu: dismiss, as a user would */
            ps2_bench_press(screen);
        }
    }
    ps2_bench_launcher_key_route(screen);
    ps2_kprintf("[BENCH] launch: menu left %d\n", (int)tracker_is_menu_open());
}

/* The closing row prints once, by whichever of the two gets there first: the app
 * phase's frame hook, when the app outlives its frame budget, or the end of
 * ps2_bench_run().  It sits outside the app guard below because that guard decides who
 * may print the row, not whether the table needs one -- with the app phase compiled
 * out, the end of the run is the only printer and still has to know nobody beat it. */
static int s_bench_sentinel;

#if BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6
/* ── Stage-1 census rung: the GS read-back with no hand and no window ────────
 *
 * The read was written to answer one question without a screen to look at -- does the
 * GS rasterise an SPRITE into the page that is displayed -- and the answer is a
 * console row, so the only thing a hand was still doing was typing `gsread`.  This
 * types it.  PCSX2 writes the emulated EE console to stdout under -nogui, and the
 * harness waits for the sentinel on that stream, so the census rides the same headless
 * path the present-cost table does: no window, no keyboard, no protocol for anyone to
 * follow, and the rows come back to be read rather than photographed.
 *
 * It replaces the desktop run rather than joining it, and that is not a convenience:
 * `ps2_bench_run()` executes with the GUI up, and inside a GUI session the desktop
 * presents every frame, so its uploads erase the blocks between the draw and the read
 * and its ring traffic trips the read's idle precondition.  The control strip would
 * report UNTRUSTED and the verdict rows would mean nothing.  A census run therefore
 * boots, draws, reads, prints, and then sits in the Stage-1 shell loop -- the guest
 * cannot shut PCSX2 down, so the harness stops it at the sentinel, exactly as it does
 * for the table.
 *
 * BENCH_GS=6 takes the same branch as 1 rather than a GUI one, for the same reason written twice
 * above: rung 3a's whole-page reads are refused while the desktop is presenting, and its plate is a
 * canvas upload, which the desktop's own upload would erase between the plate and the read.  It is a
 * value the census rung answers and nothing else does. */
#if BTRON_PS2_BENCH_GS >= 6
static void ps2_bench_page16(void);    /* the rung itself, defined with its own phase below */
#endif
#if BTRON_PS2_BENCH_GS >= 7
static void ps2_bench_flip(void);      /* rung 4: the two-page rotation, same headless path */
#endif
static void ps2_bench_gs_census(void)
{
    ps2_kprintf("\n[BENCH] Stage 1 GS census rung -- no desktop, no hand, no window\n");
    ps2_gsread_probe();
    /* rung 7 rides the same headless path: it is the one census that turns ABE on, and ABE is the
     * stage every earlier rung bypassed, so the ALPHA register has never been measured without a
     * hand.  It clears and repaints its own boxes, so it must run AFTER gsread's census, not before. */
    ps2_gsalpha_probe();
    /* rung 8 rides the same path AFTER rung 7 (both repaint navy, so neither sees the other's boxes):
     * rung 7 cleared ALPHA and this clears the last stage the flat sprite does not exercise -- whether a
     * textured fragment samples its uploaded page at all.  It is the rung that names what is left between
     * "a flat sprite lays its colour" and "2,705 textured triangles lay none". */
    ps2_gstex_probe();
#if BTRON_PS2_BENCH_GS >= 6
    /* Last, because it is the only rung here that changes the page the others read: it plates a
     * square, uploads the canvas into a PSMCT16 page, reads it back twice, and restores CT32 with the
     * plate painted out.  Run before the probes it would leave a dial moved; run after them it leaves
     * the page, the packer and the canvas as it found them, which is what the sentinel's log needs to
     * mean the same thing as every census rung before it. */
    ps2_bench_page16();
#endif
#if BTRON_PS2_BENCH_GS >= 7
    /* After rung 3a, which leaves the page CT32 and the canvas clean: rung 4 needs a 16-bit page
     * to fit two of them, dials it itself, and restores both on the way out. */
    ps2_bench_flip();
#endif
    /* Before anything downstream could print a second one, and marked as spent for the
     * same reason the app phase's frame hook marks it: the harness stops the emulator
     * on this row, so two copies would tell it the run ended in two places. */
    if (!s_bench_sentinel) {
        ps2_kprintf("\n[BENCH] run complete\n");
        s_bench_sentinel = 1;
    }
}
#endif /* BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6 */

#if BTRON_PS2_BENCH_APP
/* ── App phase: is the common GL downstack actually driving frames here? ─────
 *
 * Everything above this line measures this port's present in isolation, with no
 * window in it.  The OpenGL enablement (TinyGL, egl_surface, xmb.c) is the first
 * code in this build that runs a float sine and a z-buffer on the Emotion Engine,
 * and the only way to know it works without a hand on a mouse is to open the app and
 * count the frames the task shim gives it: xmb's body is
 * `while (s_wnd) { inval_wnd(s_wnd); dly_tsk(s_frame_ms); }`, so a pass here exists
 * only because dly_tsk() made one, and the `paint=` column is only nonzero because
 * the compositor ran xb_paint() and something drew into its surface.
 *
 * The phase ends the way a user would: one Escape at depth 1, which is xmb.c's own
 * go-back, and xb_destroy() drops the window and clears the handle the body loops on.
 * Injecting that key from inside the pass is what lets the app be opened and closed
 * by the machine, on a target with no second stack. */
static uint32_t s_bench_app_left;   /* frames owed until the injected Escape */
static uint32_t s_bench_app_made;
static uint32_t s_bench_app_t0;     /* folded µs clock at the open, for the frame heartbeat */
static uint32_t s_bench_app_over;   /* frames made past the budget's Escape, 0 = not yet spent */
static int s_bench_app_owns_end;    /* only the phase that blocks in the frame loop may end the run */

/* Which phase is allowed to END the run from the frame hook.  The grace-frame sentinel exists for
 * ps2_bench_app(), the one phase that blocks in the frame loop and would otherwise never return;
 * arming the same counters from a bounded phase lets it fire early, and the 2026-10-19 LANE 24 run
 * showed this happening: ps2_bench_launch_xmb_by_key() spends its budget inside ps2_bench_pad(), so
 * `over` walks past 2 while the launched app's own body still drives passes, the sentinel rises, and
 * ps2_bench_app() -- the only phase that opens XMB through the task shim, and the one the census is
 * about -- never runs.  Every bench log from 2026-10-16 on reads the same way: no `opened through the
 * task shim` row at all.  Ownership is a name rather than an ordering accident, so the flag says who
 * may end the run and only that phase sets it. */

extern WND *open_xmb_window(void);
extern int  xmb_depth(void);        /* the depth xb_go_back() is standing at */

/* "xmb still black" measured instead of reported: xb_paint() renders through the GL
 * backend into the window's own device pixels, so a black bar means the rasterizer
 * wrote nothing there.  open_xmb_window() is idempotent through its live handle, so
 * asking it for the window mid-loop costs nothing but a top_wnd().
 *
 * The two columns are read differently because the canvas is not in COLOR order here
 * (include/btron/types.h, BTRON_PIXEL_FB).  black= is a comparison against
 * COLOR_BLACK, whose 0xFF000000 exchanges two zero fields, so it counts the same pixels
 * in either order.  first_colour= is not: it is printed for a human to identify, so it
 * is passed back out through the macro and reads as the colour it is -- a red pixel
 * prints 0xFFFF0000 on every target, where the raw word on the PS2 is 0xFF0000FF. */
static void ps2_bench_xmb_surface(const char *when)
{
    const WND *w = open_xmb_window();
    const GDEV *dev = w ? w->dev : NULL;

    if (!dev || !dev->pixels) {
        ps2_kprintf("[BENCH] xmb surface %s: no window device to read\n", when);
        return;
    }

    unsigned int black = 0, other = 0, first = 0;
    int x0 = -1, x1 = -1, y0 = -1, y1 = -1;
    for (int y = 0; y < dev->height; y++) {
        for (int x = 0; x < dev->width; x++) {
            const COLOR v = BTRON_PIXEL_FB(dev->pixels[(uint32_t)y * (uint32_t)dev->width + (uint32_t)x]);
            if (v == (uint32_t)COLOR_BLACK) { black++; continue; }
            other++;
            if (!first) first = v;
            if (x0 < 0 || x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y0 < 0 || y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    ps2_kprintf("[BENCH] xmb surface %s: %dx%d black=%u other=%u first_colour=0x%08x box=%d,%d..%d,%d\n",
                when, dev->width, dev->height, black, other, first, x0, y0, x1, y1);
}

/* LANE 12's census, and the one measurement the frame's biggest term cannot be argued
 * without.  Under PS2_GL=eegs the application's pixels never enter the canvas:
 * `xmb surface frame 3: black=384000` says the XMB window's own device is empty, every
 * `[XMBT] raster` row says `fill 0 us` for an app stage (the EE sends primitives and the
 * GS paints them), and row 9a refuses to blit that empty plate over the desktop.  The
 * compositor still rebuilds all 480,000 px of the canvas each pass and the GIF still
 * re-sends all of them -- 28,044 us of bg+frame+blit and 4,745 us of upload in the run
 * that printed those rows.  Either those bytes change every frame and the rebuild is
 * honest work, or they do not and a third of the frame is copying a picture that is
 * already there.  No column in this log tells the two readings apart, so this one reads
 * the canvas.
 *
 * The instrument is one FNV-1a hash per row, compared against the previous sample's
 * array.  A hash answers "which rows differ", not "how many words differ", and rows is
 * the unit that matters here: the present's band set is made of rows, so a changed-row
 * count maps directly onto the bands a damage-limited transfer would owe.  Equality is
 * taken on the RAW word, with no BTRON_PIXEL_FB() exchange, because byte order has no
 * bearing on whether a word moved.
 *
 * Two confounders are excluded by construction rather than by reading.  (i) The console
 * cannot change the canvas under inspection: ps2_console_putc() emits GS text only while
 * `!s_gui_active` (this file's dual-output emitter), and a GUI pass has it set.  (ii) The
 * sample is taken at the app hook, which runs BEFORE the dispatch that paints, so what is
 * read is the canvas as the previous pass left it -- two consecutive samples are exactly
 * one frame apart, and the third is the control that separates the wave from a
 * once-a-second clock row.  The instrument's own cost is printed because it reads the
 * whole canvas and therefore lengthens the pass it runs in: compare a pair of these rows
 * with each other, never with an uninstrumented pass.
 */
static uint32_t s_census_prev[PS2_SCREEN_HEIGHT];
static int      s_census_have;

static void ps2_bench_canvas_census(const GDEV *screen, unsigned int pass)
{
    unsigned int changed = 0, y, x, lo = 0, hi = 0, w, h;
    uint32_t fp = 0;                    /* xor of the row hashes: the canvas fingerprint */
    const uint32_t t0 = ps2_count_read();

    if (!screen || !screen->pixels) {
        ps2_kprintf("[BENCH] canvas census pass %u: no canvas to read\n", pass);
        return;
    }
    w = (unsigned int)screen->width;
    h = (unsigned int)screen->height;
    if (w == 0u || h == 0u) {
        ps2_kprintf("[BENCH] canvas census pass %u: device is %ux%u\n", pass, w, h);
        return;
    }
    if (h > PS2_SCREEN_HEIGHT) {        /* the array is sized for this port's device */
        ps2_kprintf("[BENCH] canvas census pass %u: device is %u rows, the array holds %u\n",
                    pass, h, (unsigned int)PS2_SCREEN_HEIGHT);
        return;
    }
    for (y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)screen->pixels + (uint32_t)y * w;
        uint32_t hv = 2166136261u;
        for (x = 0; x < w; x++)
            hv = (hv ^ row[x]) * 16777619u;
        fp ^= hv;
        if (s_census_have && hv != s_census_prev[y]) {
            if (!changed) lo = y;
            hi = y;
            changed++;
        }
        s_census_prev[y] = hv;
    }
    s_census_have = 1;
    /* `px<=` is an UPPER bound on the damage, and it is the bound a damage-limited
     * present would have to pay for; a lower bound needs a second pass over the same
     * bytes for a number no decision here turns on. */
    ps2_kprintf("[BENCH] canvas census pass %u: rows=%u changed=%u span=%d..%d "
                "px<=%u of %u fp=%08x cost=%u us\n",
                pass, h, changed,
                changed ? (int)lo : -1, changed ? (int)hi : -1,
                changed * w, w * h, (unsigned int)fp,
                (unsigned int)ps2_us_since(t0));
}

/* Row 7's verdict, and the row that settles what no log line could: `black=384000` with no
 * `[XMBT] raster` line says the rasteriser covered nothing, but four different things produce
 * that reading -- a face whose screen area is zero, a winding that is culled, a volume the
 * geometry never got inside, and a clipper that threw the remainder away.  `tgl_face_stats()`
 * (src/gl/tinygl/clip.c) separates them at the one funnel every face passes, and the second row
 * prints the state those exits are decided by: the viewport's own scale and translation, the
 * three corners of the model-projection matrix an ortho sets, and the three flags.  The floats
 * are printed as bit patterns because the console has no float formatter to trust; an unapplied
 * glOrtho shows as m00=0x3f800000 (1.0f, i.e. still the identity) instead of the fraction a
 * 768-pixel-wide surface divides by.
 *
 * Cumulative since the pipeline was installed, not per frame, so the row reads against the
 * frames the census has already seen.
 *
 * The three rows say a face died and where, but not why: `out=5406` with a correctly applied ortho
 * has two readings that need different fixes -- the projected coordinates are genuinely outside the
 * volume, which makes the transform the thing to audit, or they are inside it and `gl_clipcode()`
 * said so anyway, which makes the comparisons the thing.  `tgl_face_dump_copy()` hands over the
 * first faces as they stood at that funnel: the three input coordinates, the four the transform
 * returned, the clip-code OR the funnel saw, the viewport mapping stored beside them, and the
 * vertex's own clip code.  A row is only worth printing while it is short, so four faces and one
 * line each. */
extern void tgl_face_stats(unsigned *out, int reset);
extern void tgl_face_dump_copy(unsigned *out, unsigned *n_out);

#define PS2_BENCH_FACE_DUMP_W 12

static void ps2_bench_face_stats(const char *when)
{
    unsigned f[20], d[4 * PS2_BENCH_FACE_DUMP_W], n, i;

    tgl_face_stats(f, 0);
    ps2_kprintf("[BENCH] faces %s: total=%u degen=%u cull=%u out=%u partial=%u drawn=%u "
                "clipor=0x%x\n",
                when, f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
    ps2_kprintf("[BENCH] faces %s: vp=%ux%u scaleX=0x%08x transX=0x%08x scaleY=0x%08x "
                "transY=0x%08x\n", when, f[7], f[8], f[9], f[10], f[11], f[12]);
    ps2_kprintf("[BENCH] faces %s: m00=0x%08x m55=0x%08x m15=0x%08x poly=%u cull=%u "
                "front=%u noW=%u\n", when, f[13], f[14], f[15], f[16], f[17], f[18], f[19]);

    tgl_face_dump_copy(d, &n);
    for (i = 0; i < n; i++)
        ps2_kprintf("[BENCH] face %u %s: coord=%08x/%08x/%08x pc=%08x/%08x/%08x W=%08x "
                    "co=%x zp=%d,%d,%d cc=%x\n",
                    i, when,
                    d[i * PS2_BENCH_FACE_DUMP_W + 0], d[i * PS2_BENCH_FACE_DUMP_W + 1],
                    d[i * PS2_BENCH_FACE_DUMP_W + 2], d[i * PS2_BENCH_FACE_DUMP_W + 3],
                    d[i * PS2_BENCH_FACE_DUMP_W + 4], d[i * PS2_BENCH_FACE_DUMP_W + 5],
                    d[i * PS2_BENCH_FACE_DUMP_W + 6], d[i * PS2_BENCH_FACE_DUMP_W + 7],
                    (int)d[i * PS2_BENCH_FACE_DUMP_W + 8], (int)d[i * PS2_BENCH_FACE_DUMP_W + 9],
                    (int)d[i * PS2_BENCH_FACE_DUMP_W + 10], d[i * PS2_BENCH_FACE_DUMP_W + 11]);
}

/* Row 5b's own decisive row, and the reason it exists: every other 5b assertion is about words
 * (a host suite) or about linkage (an image row), and neither can tell me that a plane's bytes
 * actually crossed on this machine.  registered=0 would mean the upload seam is dead -- compiled,
 * linked, called by nothing -- and 5c would then be emitting a stream that samples pages holding
 * whatever the last upload left there.  pending= means a plane the bus never took, and
 * vram_left= is the allocator's own budget read back, which is the number the two shipped sheets
 * are predicted to leave: 1,081,344. */
static void ps2_bench_gs_planes(void)
{
    uint32_t i;

    ps2_kprintf("[BENCH] gs planes: registered=%u pending=%u vram_left=%u\n",
                (unsigned int)gs_tex_count(), (unsigned int)gs_tex_pending(),
                (unsigned int)gs_tex_vram_left());
    for (i = 0; i < gs_tex_count(); i++) {
        const gs_tex_t *t = gs_tex_at(i);

        ps2_kprintf("[BENCH] gs plane %u: upload %ux%u into %ux%u at byte %u TBA %u qwc %u%s\n",
                    (unsigned int)i, (unsigned int)t->px_w, (unsigned int)t->px_h,
                    (unsigned int)t->pad_w, (unsigned int)t->pad_h,
                    (unsigned int)t->byte, (unsigned int)t->tba, (unsigned int)t->qwc,
                    t->pending ? " PENDING" : "");
    }
}

/* How many app frames the bench phase asks for before it injects its own Escape.
 * Sixty is the right number for a table and the wrong number for a feedback loop:
 * at the frame cost this app has, 60 frames is 24 minutes of emulator and every
 * harness that waits for the closing sentinel gets killed at frame 3.  A loop that
 * wants the steady-state per-frame cost only needs the third one. */
#ifndef BTRON_PS2_BENCH_APP_FRAMES
#define BTRON_PS2_BENCH_APP_FRAMES 60u
#endif

/* Frames the launcher-by-key CYCLE phase runs before it injects its own Escape -- and therefore the
 * budget that ends a whole bench run, since this phase is the one that prints the closing sentinel
 * (`ps2_bench_app()`'s longer ask only begins after it).  Three is right for that phase's own
 * question, "did the row open and draw", and useless for LANE 22's, which is a RATE: how many passes
 * refused the app, against how many priced the refusal.  `BENCH_LAUNCH_FRAMES=40` is the dial
 * #165's measurement turns; the default keeps every existing run's rows and cadence identical. */
#ifndef BTRON_PS2_BENCH_LAUNCH_FRAMES
#define BTRON_PS2_BENCH_LAUNCH_FRAMES 3u
#endif

/* LANE 24's two dials, both off by default so every existing run keeps its rows and its timings:
 * `BENCH_TEX=1` arms the page/plane/canvas cell census, `BENCH_ORDER=1` opens `lilcu` before `xmb`.
 * The census is the measurement; the order is the variable.  Neither means anything without
 * `BENCH_APP=1`, because the windows they ask about only the app phase opens. */
#ifndef BTRON_PS2_BENCH_TEX
#define BTRON_PS2_BENCH_TEX 0
#endif
#ifndef BTRON_PS2_BENCH_ORDER
#define BTRON_PS2_BENCH_ORDER 0u
#endif
#if BTRON_PS2_BENCH_TEX || BTRON_PS2_BENCH_ORDER
static void ps2_bench_lilcu_warm(GDEV *screen);

/* The warm sub-phase rides on the app phase's own counters on purpose: the Escape the frame hook
 * injects when they run out is the only way this port closes a window it did not open, and whether
 * the demo takes it is a fact worth printing rather than one to hide behind a second mechanism.
 * It also means the XMB-labelled rows in that hook would fire over a demo phase, so they check
 * this.  With both dials down the name expands to a constant 0, which is why it lives here rather
 * than inside the guarded block below -- the hook is compiled by `BENCH_APP`, not by `BENCH_TEX`. */
static int s_bench_tex_warm;
#else
#define s_bench_tex_warm 0
#endif

static void ps2_bench_app(GDEV *screen, uint32_t frames)
{
    RENDER_STATS st;
    const uint32_t t_start = btron_render_perf_us();  /* folded: one frame exceeds a Count wrap */
    WND *w;

#if BTRON_PS2_BENCH_TEX || BTRON_PS2_BENCH_ORDER
    /* First of everything, ahead of the stats take below: the A/B's other arm is a whole app run, and
     * its frames must not be charged to the XMB's compositor row. */
    if (BTRON_PS2_BENCH_ORDER == 1u) ps2_bench_lilcu_warm(screen);
#else
    (void)screen;      /* only LANE 24's arms drive a pass loop from here */
#endif
    ps2_kprintf("\n[BENCH] xmb opened through the task shim -- %u frames asked of dly_tsk()\n",
                (unsigned int)frames);
    btron_render_stats_take(&st);       /* the region table's counters are not this row's */
    s_bench_app_active = 1;
    /* Six cycles.  The first of them is the app's first painted frame and is expected to
     * carry whatever the GL backend does once (texture uploads, a lazily built table);
     * the rows after it are the period the user would feel.  The profiler costs nothing
     * until this is non-zero, and one branch per boundary after that. */
    s_cycle.left = 6u;
    s_bench_app_left = frames;
    s_bench_app_made = 0u;
    s_bench_app_over = 0u;
    s_bench_app_owns_end = 1;       /* this phase's budget is the run's end; see its declaration */
    s_bench_app_t0 = t_start;
    /* Blocks until the injected Escape has closed the window: the frame loop is the
     * body's from here, not the session loop's.  The two heap rows are the pool's
     * state either side of open_xmb_window(), largest hole included -- a GL surface
     * needs one contiguous block, so "bytes free" alone can be an innocent answer. */
    const char *pool_bad = ps2_heap_check();
    ps2_kprintf("[BENCH] heap before xmb: used=%u of %u kB, largest hole %u kB, stack=%u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u), ps2_stack_probe_depth(),
                pool_bad ? pool_bad : "sound");
    if (pool_bad) ps2_heap_dump();
    w = open_xmb_window();
    s_bench_app_active = 0;
    s_bench_app_left = 0u;
    /* The counters belong to no phase again.  Left set, the launch phase that runs after this one
     * would wear the app phase's identity, and LANE 24's rows are gated on that identity. */
    s_bench_app_owns_end = 0;
    pool_bad = ps2_heap_check();
    ps2_kprintf("[BENCH] heap after  xmb: used=%u of %u kB, largest hole %u kB (peak %u kB), stack=%u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                (unsigned int)(s_heap_peak / 1024u),
                ps2_stack_probe_depth(),
                pool_bad ? pool_bad : "sound");
    if (pool_bad) ps2_heap_dump();

    {
        /* passes * 1e6 / us.  The folded clock is already in microseconds, so a second
         * of them is the whole numerator and no tick constant appears here -- which is
         * the form that survives a long phase, where the tick version overflowed the
         * 32-bit product at 60 frames. */
        const uint32_t us = btron_render_perf_us() - t_start;
        /* `<=` because the fold loses a Count wrap for every 2^32/tpu of blocked time
         * between its calls -- 14.6 s at the rate this machine's plane measures, 29.1 s at
         * the documented one -- and a frame of this app is longer than that: the elapsed
         * column is short, so the derived rate is an upper bound, not a measurement. */
        ps2_kprintf("[BENCH] xmb: open returned %s, %u passes in %u ms folded -> <= %u passes/s, window still %s\n",
                    w ? "with a window" : "with no window",
                    (unsigned int)s_bench_app_made,
                    (unsigned int)(us / 1000u),
                    (unsigned int)(us ? s_bench_app_made * 1000000u / us : 0u),
                    get_top_wnd() ? "up" : "closed");
    }
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u(%u px skip=%u) paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.frame_body_px,
                (unsigned int)st.frame_body_skip_px,
                (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
    /* Zero passes is the failure this phase exists to catch: either the window never
     * opened, or a body that asks for a frame got nothing.  A nonzero `paint=` with
     * zero passes is impossible, so the two rows together say which of the two it was.
     * 0 frames is not a comparison, so the row is labelled rather than inferred. */
    ps2_kprintf("[BENCH] xmb: %s\n", s_bench_app_made ? "task-driven frames present"
                                                      : "NO task-driven frame -- dly_tsk() gave the body nothing");
}

/* The user's own action, measured: Start, walk to the XMB row, Cross.
 *
 * The phase above opens the app by calling open_xmb_window() directly, which proves the
 * GL downstack draws but says nothing about the launcher row a hand presses.  This
 * presses it, so the surface rows below belong to this phase.  The frame budget is armed
 * around the press because the row's opener does not return while its window is up --
 * the injected Escape is what lets the run finish -- and the pool is read on both sides,
 * since "no window" and "out of memory" reach the user as one complaint.
 *
 * It runs here rather than from the launcher phase because that phase is last and opens
 * a second terminal first; a pool that has already refused one 2,631 kB request is not a
 * fair place to ask whether the row works. */
static void ps2_bench_launch_xmb_by_key(GDEV *screen)
{
    const TRACKER *t;
    int xmb_row, hops = 0;
    UW launched0;

    ps2_kprintf("\n[BENCH] launch XMB by key -- the row the user presses, activated\n");

    if (!tracker_is_menu_open()) ps2_bench_pad(screen, PAD_START);
    t = tracker_get_state();
    xmb_row = ps2_bench_row_of(t, TRACKER_CMD_XMB);
    if (!tracker_is_menu_open() || xmb_row < 0) {
        ps2_kprintf("[BENCH] xmb by key: launcher=%d xmb_row=%d -- there is no row to press\n",
                    (int)tracker_is_menu_open(), xmb_row);
        return;
    }
    while ((int)t->hover_index < xmb_row && hops <= xmb_row + 2) {
        ps2_bench_pad(screen, PAD_DOWN);
        hops++;
        t = tracker_get_state();
    }
    ps2_kprintf("[BENCH] xmb by key: %d DOWN hops put the highlight on row %d, launcher=%d\n",
                hops, (int)t->hover_index, (int)tracker_is_menu_open());
    if ((int)t->hover_index != xmb_row || !tracker_is_menu_open()) {
        ps2_kprintf("[BENCH] xmb by key: the highlight never reached the XMB row, Cross skipped\n");
        return;
    }

    launched0 = t->launch_count;
    /* The counter is this phase's own: ps2_gui_pass() raises it once per task-driven
     * pass and samples the surface at 3, so arming a budget of 3 both prints the ink row
     * and injects the Escape that ends the body. */
    s_bench_app_t0 = btron_render_perf_us();
    s_bench_app_made = 0u;
    s_bench_app_over = 0u;
    s_bench_app_active = 1;
    s_cycle.left = 3u;
    s_bench_app_left = 3u;
    ps2_kprintf("[BENCH] xmb by key: heap before the press -- used=%u of %u kB, largest hole %u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                ps2_heap_check() ? ps2_heap_check() : "sound");
    ps2_bench_pad(screen, PAD_CROSS);
    s_bench_app_active = 0;
    s_bench_app_left = 0u;
    ps2_kprintf("[BENCH] xmb by key: Cross on row %d launched %u->%u, %u task-driven frames, top window %s\n",
                xmb_row, (unsigned int)launched0,
                (unsigned int)tracker_get_state()->launch_count,
                (unsigned int)s_bench_app_made,
                get_top_wnd() ? "up" : "closed");
    ps2_kprintf("[BENCH] xmb by key: heap after -- used=%u of %u kB, largest hole %u kB (peak %u kB), pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                (unsigned int)(s_heap_peak / 1024u),
                ps2_heap_check() ? ps2_heap_check() : "sound");
    ps2_kprintf("[BENCH] xmb by key: %s\n",
                s_bench_app_made
                    ? "the launcher's XMB row drove the app through its own task loop -- the row works"
                    : "the row was activated but the app never got a frame -- this is what \"can't run xmb from menu\" is");
}

/* ── The g_term verb, on a pool nobody has spent yet ────────────────────────
 *
 * "Row is there, picks nothing" is a claim about a third route, and neither phase
 * above can answer it: the deskbar row and this port's direct call both reach
 * open_xmb_window() themselves.  What the user types is `xmb` at the g_term prompt,
 * which is gterm.c:860 -> shell_execute_cmd() -> clu.c's one app table ->
 * open_xmb_window(), and the chain has three answers that reach the screen as shell
 * text -- "Not linked into this build", "Failed to open window surface", "Window
 * opened on the Workbench desktop" -- plus a fourth the user reports: nothing at
 * all.  Only a call through shell_execute_cmd() with an out_fn that echoes can tell
 * those four apart, which is what this phase is.
 *
 * It runs ahead of every pixel phase for one reason, and the 2026-10-15 eegs run is
 * the reason: by the time the click and cursor phases have finished this image has
 * spent 4,054 kB more of the 11 MB pool than a booted desktop has, the largest hole
 * is then 1,359 kB, and the XMB's client pixmap asks 1,500 kB -- so both app phases
 * printed `[GL] ERROR: xmb: egl_create_window_surface failed` while the production
 * image of the same dials ran the XMB at 19.4 ms/frame on a 5,413 kB hole.  The
 * harness had starved its own pool before asking the app to open, and a verdict of
 * "the row works" printed beside that failure measured the dispatch, not the app.
 * Measured at boot, this phase sees the same pool the user's machine has.
 *
 * The census is the row that makes the difference visible instead of inferred: every
 * used block big enough to matter, named by the window that owns it where the window
 * table can say so, and every hole the pool is holding.
 *
 * Three cycles rather than one call, because the complaint is not "the first open fails"
 * -- the 2026-10-15 production log shows a first open at 19.4 ms/frame -- it is "I open it
 * and after a while it picks nothing", and that is a statement about closes.  One open
 * prices the open; the second and third show whether the bytes the first borrowed came
 * back, and on a first-fit pool a hole that shrinks below the ask is the exact moment the
 * verb starts answering with nothing.  The measured answer, before this ledger existed, was
 * 1,088 kB kept per cycle (the 512x512 icon plane and the 128x128 font plane), which is why
 * the pool census runs at boot and after the last close rather than once. */

/* The client pixmap a GL window is about to ask for, computed the way the app
 * computes it (xmb.c's own sizing) and the way the window manager cuts it (opn_wnd():
 * a caption of WND_TITLE_HEIGHT and 4 px of border per side for a titled, bordered
 * window).  Not a constant copied from the OOM row -- it moves with the desktop size,
 * which is why it is derived here. */
static size_t ps2_bench_gl_surface_ask(void)
{
    BTRON_DESKTOP *d = get_btron_desktop();
    int w = 960, h = 600;

    if (d && d->width > 160 && d->height > 160) {
        w = d->width - 24;
        h = d->height - 64;
        if (w > 1440) w = 1440;
        if (h > 900)  h = 900;
    }
    return (size_t)(w - 8) * (size_t)(h - WND_TITLE_HEIGHT - 8) * sizeof(COLOR);
}

static void ps2_bench_pool_census(const char *tag)
{
    unsigned n = 0, big = 0, free_n = 0;
    size_t hole = 0;

    for (const ps2_blk *b = s_heap_list; b; b = b->next, n++) {
        if (b->used) continue;
        free_n++;
        if (b->size > hole) hole = b->size;
    }
    ps2_kprintf("[BENCH] pool %s: %u block(s), %u free, used=%u kB of %u kB, largest hole %u kB, a GL window asks %u kB\n",
                tag, n, free_n, (unsigned int)(s_heap_used / 1024u),
                (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(hole / 1024u),
                (unsigned int)(ps2_bench_gl_surface_ask() / 1024u));

    /* One row per block at or above the pool's own log threshold, so the names of the
     * big holders are in the log rather than a total.  `unmatched` is not a defect: it
     * is a buffer some subsystem asked for that no window owns -- the compositor's
     * cache, a texture page, a task stack. */
    for (const ps2_blk *b = s_heap_list; b; b = b->next) {
        const WND *w;
        const char *who = "unmatched";

        if (b->size < PS2_HEAP_LOGLIM) continue;
        big++;
        if (!b->used) {
            ps2_kprintf("[BENCH] pool %s: free %5u kB at 0x%x\n", tag,
                        (unsigned int)(b->size / 1024u),
                        (unsigned int)(uintptr_t)(b + 1));
            continue;
        }
        for (w = get_wnd_list(); w; w = w->next) {
            if (w->dev && (const void *)w->dev->pixels == (const void *)(b + 1)) {
                who = w->title[0] ? w->title : "(untitled)";
                break;
            }
        }
        ps2_kprintf("[BENCH] pool %s: used %5u kB at 0x%x  holds '%s'\n", tag,
                    (unsigned int)(b->size / 1024u),
                    (unsigned int)(uintptr_t)(b + 1), who);
    }
    ps2_kprintf("[BENCH] pool %s: %u block(s) at or above %u kB\n", tag, big,
                (unsigned int)(PS2_HEAP_LOGLIM / 1024u));
}

/* What the terminal would have shown, on the console instead, with the three answers
 * clu_launch_app() can give sorted into flags as they go by. */
static int s_bench_shell_lines;
static int s_bench_shell_not_linked;
static int s_bench_shell_no_surface;
static int s_bench_shell_opened;

static void ps2_bench_shell_out(const char *line, COLOR col, void *user_data)
{
    (void)col; (void)user_data;

    s_bench_shell_lines++;
    if (tkl_strstr(line, "Not linked"))       s_bench_shell_not_linked++;
    if (tkl_strstr(line, "Failed to open"))   s_bench_shell_no_surface++;
    if (tkl_strstr(line, "Window opened"))    s_bench_shell_opened++;
    ps2_kprintf("[BENCH] shell> %s\n", line ? line : "(null line)");
}

/* The window this route is supposed to have opened, by title rather than by handle --
 * the handle is the app's own static and is NULL on every path that failed. */
static WND *ps2_bench_wnd_named(const char *needle)
{
    for (WND *w = get_wnd_list(); w; w = w->next)
        if (tkl_strstr(w->title, needle)) return w;
    return NULL;
}

/* ── Texture Atlas VRAM Residency & Sampler Telemetry ────────────────────────
 *
 * Verifies texture atlas residency in GS VRAM and measures rendering fidelity
 * against software-rasterized baseline expectations.
 *
 * Diagnostic Architecture:
 * 1. Plane Residency in VRAM (`ps2_gs_plane_read`):
 *    Reads back the allocated texture plane directly from GS VRAM to confirm
 *    that baked atlas texels transferred successfully across DMAC channel 2.
 * 2. Paired Page-versus-Canvas Census:
 *    Compares VRAM front buffer contents with the CPU canvas backbuffer across
 *    identical screen bounding boxes to differentiate upstream application errors
 *    from hardware sampling defects.
 * 3. Cell Uniformity Metric (`GSBOX_CELL`):
 *    Partitions icon grid bounds into 64x64 pixel cells. Cells exhibiting 100%
 *    monochrome fill indicate fallback untextured quads or missing texture bindings.
 *
 * Bus synchronization and alignment invariants:
 * - VRAM readback waits for the GIF DMA ring to idle before initiating Local->Host transfers.
 * - Coordinates are floored to an 8-pixel grid to satisfy CT32 / CT16 burst alignment. */
#if BTRON_PS2_BENCH_TEX || BTRON_PS2_BENCH_ORDER

/* Frames of the demo to ask before the harness injects its Escape. */
#define GSBOX_WARM_FRAMES 4u

#if BTRON_PS2_BENCH_TEX
#define GSBOX_CELL     64u   /* one cell per icon cell at the atlas's own scale */
#define GSBOX_BAND     32u   /* rows per read: 800*32*4 = 102,400 B = 6,400 qw, well under DMAC's cap */
#define GSBOX_TOP       8u   /* solid colours kept; a 9th evicts the rarest and is counted in `rare` */
#define GSBOX_MAXCELLS  128u /* 12 x 8 over the client box, 8 x 8 over a 512-px plane */
#define GSBOX_READ_CAP    8u /* readings past frame 3: each one is ~1.4 MB of Local->Host DMA and the
                              * ticks come every ~3 frames, so uncapped the census would price the run
                              * rather than the picture.  Eight covers two seconds of the phase. */

typedef struct {
    uint32_t x0, y0, w, h;             /* the floored census box, in source px */
    uint32_t cell, shift;              /* px per cell, and its log2 -- always a power of two */
    uint32_t cols, rows, cells;
    uint32_t px, sent;                 /* px delivered, px the read never filled */
    uint32_t solid, near, rare;        /* 100 % cells, >= 95 % cells, solid cells dropped from the table */
    uint32_t cell_n[GSBOX_MAXCELLS], cell_same[GSBOX_MAXCELLS], cell_first[GSBOX_MAXCELLS];
    uint32_t sol_col[GSBOX_TOP], sol_n[GSBOX_TOP];
    gs_read_bus_err err;
    int have;                          /* the box held at least one whole cell */
} gsbox_t;

/* Fields one at a time, as gsread_page_census()'s note says: this TU is compiled -mhard-float and a
 * struct assignment here has put sdc1/ldc1 -- illegal on the R5900 -- into the image before. */
static void gsbox_begin(gsbox_t *o, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
    uint32_t cell = GSBOX_CELL;
    uint32_t i;

    o->x0 = x0; o->y0 = y0;
    o->px = 0u; o->sent = 0u; o->solid = 0u; o->near = 0u; o->rare = 0u;
    o->err = GS_READ_BUS_OK;
    o->have = 0;

    /* Widen the cell until the grid fits its own array, so a 1024-px plane asks for 128 cells of 8 x 8
     * rather than 256 of 4 x 4 and a silent truncation. */
    for (;;) {
        if (((w / cell) * (h / cell)) <= GSBOX_MAXCELLS || cell >= 256u) { o->cell = cell; break; }
        cell <<= 1u;
    }
    o->shift = 0u;
    while ((1u << (int)o->shift) < o->cell) o->shift++;
    o->w = (w / o->cell) * o->cell;
    o->h = (h / o->cell) * o->cell;
    o->cols = o->w / o->cell;
    o->rows = o->h / o->cell;
    o->cells = o->cols * o->rows;
    if (o->cells == 0u || o->cells > GSBOX_MAXCELLS) return;

    for (i = 0u; i < GSBOX_MAXCELLS; i++) {
        o->cell_n[i] = 0u; o->cell_same[i] = 0u; o->cell_first[i] = 0u;
    }
    for (i = 0u; i < GSBOX_TOP; i++) { o->sol_col[i] = 0u; o->sol_n[i] = 0u; }
    o->have = 1;
}

/* Fold one band into the census.  `buf` is the band exactly as the read delivered it: `o->w` px per
 * line, `rows` lines, starting at source line `y_start`.  The second half of that is not a detail -- a
 * Local->Host download carries no row pitch beyond its own rectangle, so a stride of the SCREEN width
 * would read the next row's start as this row's tail and call the difference texture. */
static void gsbox_feed(gsbox_t *o, const uint32_t *buf, uint32_t y_start, uint32_t rows)
{
    uint32_t r, cx;

    if (!o->have) return;
    for (r = 0u; r < rows; r++) {
        const uint32_t y = y_start + r;
        uint32_t cy;

        if (y < o->y0 || y >= o->y0 + o->h) continue;
        cy = (y - o->y0) >> o->shift;
        for (cx = 0u; cx < o->cols; cx++) {
            const uint32_t ci = cy * o->cols + cx;
            const uint32_t *p = buf + (size_t)r * o->w + (cx << o->shift);
            uint32_t i, n = o->cell_n[ci], same = o->cell_same[ci], first = o->cell_first[ci];

            for (i = 0u; i < o->cell; i++) {
                const uint32_t v = p[i];

                if (v == GSREAD_SENT) { o->sent++; continue; }
                if (n == 0u) first = v;             /* the cell's own top-left, wherever it arrived */
                o->px++;
                n++;
                if (v == first) same++;
            }
            o->cell_n[ci] = n; o->cell_same[ci] = same; o->cell_first[ci] = first;
        }
    }
}

/* Derive the two verdict columns.  Separate from the walk so it runs once per box, and so a box whose
 * cells were only half read says nothing instead of saying `solid`. */
static void gsbox_finish(gsbox_t *o)
{
    uint32_t ci, k;
    const uint32_t full = o->cell * o->cell;

    if (!o->have) return;
    for (ci = 0u; ci < o->cells; ci++) {
        const uint32_t n = o->cell_n[ci];
        const uint32_t same = o->cell_same[ci];

        if (n < full) continue;
        if (same == n) {
            uint32_t hit = GSBOX_TOP, empt = GSBOX_TOP;

            o->solid++;
            for (k = 0u; k < GSBOX_TOP; k++) {
                if (o->sol_n[k] && o->sol_col[k] == o->cell_first[ci]) { hit = k; break; }
                if (empt == GSBOX_TOP && o->sol_n[k] == 0u) empt = k;
            }
            if (hit != GSBOX_TOP) o->sol_n[hit]++;
            else if (empt != GSBOX_TOP) { o->sol_col[empt] = o->cell_first[ci]; o->sol_n[empt] = 1u; }
            else {
                uint32_t worst = 0u;

                for (k = 1u; k < GSBOX_TOP; k++) if (o->sol_n[k] < o->sol_n[worst]) worst = k;
                o->rare += o->sol_n[worst];
                o->sol_n[worst] = 1u;
                o->sol_col[worst] = o->cell_first[ci];
            }
        } else if (same * 20u >= n * 19u) {
            o->near++;                              /* >= 95 % one colour: a flat quad a neighbour's edge
                                                     * intrudes on, which still is not an icon */
        }
    }
}

static void gsbox_row(const gsbox_t *o, const char *tag)
{
    uint32_t k;

    if (!o->have) {
        ps2_kprintf("[GSBOX] %s: NO CENSUS -- the box holds no whole %u px cell\n",
                    tag, (unsigned int)GSBOX_CELL);
        return;
    }
    ps2_kprintf("[GSBOX] %s: box=%u,%u %ux%u cell=%u cols=%u rows=%u px=%u sent=%u solid=%u p95=%u "
                "rare=%u err=%s\n",
                tag, (unsigned int)o->x0, (unsigned int)o->y0, (unsigned int)o->w, (unsigned int)o->h,
                (unsigned int)o->cell, (unsigned int)o->cols, (unsigned int)o->rows,
                (unsigned int)o->px, (unsigned int)o->sent, (unsigned int)o->solid,
                (unsigned int)o->near, (unsigned int)o->rare, gsread_bus_name(o->err));
    ps2_kprintf("[GSBOX] %s flat colours:", tag);
    if (!o->solid) { ps2_kprintf(" none\n"); return; }
    for (k = 0u; k < GSBOX_TOP; k++)
        if (o->sol_n[k]) ps2_kprintf(" 0x%08x=%u", (unsigned int)o->sol_col[k],
                                     (unsigned int)o->sol_n[k]);
    ps2_kprintf("%s\n", o->rare ? " (+evicted)" : "");
}

/* Walk a rectangle of the DISPLAY PAGE, in bands. */
static void gsbox_page(gsbox_t *o, uint32_t *buf, uint32_t width, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h)
{
    uint32_t y_start;

    if (x + w > width) w = width - x;              /* the page's own pitch law, before the builder's */
    gsbox_begin(o, x, y, w, h);
    if (!o->have) return;
    for (y_start = o->y0; y_start < o->y0 + o->h; y_start += GSBOX_BAND) {
        const uint32_t rows = (o->y0 + o->h - y_start < GSBOX_BAND) ? (o->y0 + o->h - y_start)
                                                                    : GSBOX_BAND;
        uint32_t i;

        for (i = 0u; i < o->w * rows; i++) buf[i] = GSREAD_SENT;
        (void)ps2_gs_vram_read((int)o->x0, (int)y_start, (int)o->w, (int)rows, buf);
        o->err = ps2_gs_vram_read_err();
        if (o->err != GS_READ_BUS_OK) {
            ps2_kprintf("[GSBOX] page read y=%u FAILED: %s", (unsigned int)y_start,
                        gsread_bus_name(o->err));
            if (o->err == GS_READ_BUS_E_BUILD)
                ps2_kprintf(" (law: %s)", gsread_law_name(gs_read_last_err()));
            ps2_kprintf("\n");
            return;
        }
        gsbox_feed(o, buf, y_start, rows);
    }
    gsbox_finish(o);
}

/* Walk a rectangle of a registered PLANE, in bands -- the same recipe on the other page. */
static void gsbox_plane(gsbox_t *o, uint32_t *buf, uint32_t index, uint32_t w, uint32_t h)
{
    const gs_tex_t *t = gs_tex_at(index);
    uint32_t y_start;

    if (t == 0) { o->have = 0; return; }
    if (w > t->pad_w) w = t->pad_w;
    if (h > t->pad_h) h = t->pad_h;
    gsbox_begin(o, 0u, 0u, w, h);
    if (!o->have) return;
    for (y_start = 0u; y_start < o->h; y_start += GSBOX_BAND) {
        const uint32_t rows = (o->h - y_start < GSBOX_BAND) ? (o->h - y_start) : GSBOX_BAND;
        uint32_t i;

        for (i = 0u; i < o->w * rows; i++) buf[i] = GSREAD_SENT;
        /* The band's own top line, in the PLANE's coordinates: the rectangle the builder gets is
         * (0, y_start) inside a buffer whose row pitch is the plane's TBW, which is the whole point
         * of reading a plane rather than the page. */
        (void)ps2_gs_plane_read(index, 0, (int)y_start, (int)o->w, (int)rows, buf);
        o->err = ps2_gs_vram_read_err();
        if (o->err != GS_READ_BUS_OK) {
            ps2_kprintf("[GSBOX] plane %u read y=%u FAILED: %s", (unsigned int)index,
                        (unsigned int)y_start, gsread_bus_name(o->err));
            if (o->err == GS_READ_BUS_E_BUILD)
                ps2_kprintf(" (law: %s)", gsread_law_name(gs_read_last_err()));
            ps2_kprintf("\n");
            return;
        }
        gsbox_feed(o, buf, y_start, rows);
    }
    gsbox_finish(o);
}

/* One reading's cell detail, kept so the NEXT reading can be diffed against it instead of
 * re-thresholded.  The arrays cover the whole box because the operator's 2026-10-18 fact is a
 * sentence about ONE event in the middle of a phase -- "Icons show inititially fine but get corrupted
 * after FPS ticker changed" -- and a single frame's flat-cell count cannot answer that.  The number
 * that does is `went_flat`: cells that were art at the first reading and are one colour at the second.
 * A box is 84 cells, so the whole reading is kept rather than a count. */
typedef struct {
    uint32_t first[GSBOX_MAXCELLS], n[GSBOX_MAXCELLS], same[GSBOX_MAXCELLS];
    uint32_t cells, solid, cell;
    int have;
} gsprev_t;

/* Field by field, never a struct copy: this TU is -mhard-float, and a copied struct here has put
 * sdc1/ldc1 -- illegal on the R5900 -- into the image before. */
static void gsprev_store(gsprev_t *p, const gsbox_t *b)
{
    uint32_t i;

    p->cells = b->cells; p->solid = b->solid; p->cell = b->cell; p->have = b->have;
    if (!b->have) return;
    for (i = 0u; i < b->cells && i < GSBOX_MAXCELLS; i++) {
        p->first[i] = b->cell_first[i];
        p->n[i] = b->cell_n[i];
        p->same[i] = b->cell_same[i];
    }
}

/* The pair row, returning the count its verdict turns on.  `came_back` is the opposite motion, so
 * `went_flat=0 came_back=0` says the event between the two readings did not touch this box, and a
 * `came_back` with no `went_flat` is a page that healed -- the reading "gets corrupted" has to lose
 * for a later-present story to be the true one. */
static uint32_t gsbox_diff(const gsbox_t *now, const gsprev_t *was, const char *what,
                           const char *tag, uint32_t frame)
{
    uint32_t i, changed = 0u, went = 0u, back = 0u, ex = 0u;
    const uint32_t full = now->cell * now->cell;

    if (was->cells != now->cells || was->cell != now->cell) {
        ps2_kprintf("[GSBOX] frame %u %s %s diff: UNAVAILABLE -- the box moved (was %u cells of %u px, "
                    "now %u of %u)\n", (unsigned int)frame, tag, what, (unsigned int)was->cells,
                    (unsigned int)was->cell, (unsigned int)now->cells, (unsigned int)now->cell);
        return 0u;
    }
    for (i = 0u; i < now->cells; i++) {
        const uint32_t n = now->cell_n[i];
        const int was_solid = (was->n[i] == full && was->same[i] == was->n[i]);
        const int is_solid  = (n == full && now->cell_same[i] == n);

        if (was->first[i] != now->cell_first[i]) changed++;
        if (is_solid && !was_solid) { if (!went) ex = i; went++; }
        else if (was_solid && !is_solid) back++;
    }
    ps2_kprintf("[GSBOX] frame %u %s %s diff: solid %u -> %u went_flat=%u came_back=%u recoloured=%u\n",
                (unsigned int)frame, tag, what, (unsigned int)was->solid, (unsigned int)now->solid,
                (unsigned int)went, (unsigned int)back, (unsigned int)changed);
    if (went)
        ps2_kprintf("[GSBOX] frame %u %s %s first cell that went flat: px %u,%u (cell %u of %u cols) "
                    "was 0x%08x now 0x%08x\n",
                    (unsigned int)frame, tag, what,
                    (unsigned int)(now->x0 + (ex / now->cols) * now->cell),
                    (unsigned int)(now->y0 + (ex % now->cols) * now->cell), (unsigned int)ex,
                    (unsigned int)now->cols, (unsigned int)was->first[ex],
                    (unsigned int)now->cell_first[ex]);
    return went;
}

/* The arm of the A/B that reads the screen, and the rows that say which cause the pair names.
 * `tag` labels WHICH reading this is -- "first" at frame 3, "after tick" once the FPS readout has
 * renamed the window -- because the operator's fact moved the measurement from one frame to the event
 * between two: the readout is written by src/gl/egl_surface.c's fps_overlay_tick() through
 * wnd_set_title(), and wnd.c's own comment says what that damages -- "Repaint the tab only; the
 * client image is untouched".  A tab-only damage that corrupts client-area icons is therefore a
 * present-path event, and the page/plane/canvas pair across it is the whole question. */
static void ps2_bench_tex_census(uint32_t frame, const char *order, const char *tag)
{
    static gsbox_t s_bx_page, s_bx_canvas, s_bx_pl[GS_TEX_MAX];
    static gsprev_t s_prev_page, s_prev_canvas;
    /* The registry, as the previous reading left it.  Asked for because of the rule ps2_gs_tex.h
     * states: the plane POINTER is the identity, and "same pointer, *different* geometry is the C heap
     * recycling a freed plane's address, so the entry is re-planned -- it takes a fresh span and
     * abandons the old one".  A re-plan is exactly what a damage-driven repaint can cause, and it is
     * invisible unless the span is remembered: the sampler's TEX0 would still name the old page. */
    static const void *s_prev_plane[GS_TEX_MAX];
    static uint32_t s_prev_byte[GS_TEX_MAX], s_prev_tbw[GS_TEX_MAX], s_prev_qwc[GS_TEX_MAX];
    static uint32_t s_prev_padw[GS_TEX_MAX], s_prev_padh[GS_TEX_MAX], s_prev_npl;
    static uint32_t s_prev_vram_left;
    static int s_prev_valid;
    const uint32_t width = (uint32_t)PS2_SCREEN_WIDTH;
    const uint32_t height = (uint32_t)PS2_SCREEN_HEIGHT;
    const uint32_t npl = gs_tex_count();
    const WND *win = ps2_bench_wnd_named("Cross Media Bar");
    const uint32_t *canvas = ps2_gs_get_framebuffer();
    uint32_t *buf;
    uint32_t i, x0 = 0u, y0 = 0u, bw = 0u, bh = 0u, agree = 0u;
    uint32_t page_went = 0u, canvas_went = 0u;
    const char *verdict;

    /* Asked for up front, once, for the reason in this file's own note: Imalloc() prints a [HEAP] row
     * past its threshold, and a console repaint is an upload of the page this census is about to read.
     * 800 px x 32 rows is the widest band any of the three boxes asks for. */
    buf = (uint32_t *)Imalloc((size_t)width * GSBOX_BAND * sizeof(uint32_t));
    if (buf == 0) {
        ps2_kprintf("[GSBOX] frame %u order=%s %s: NO BUFFER -- the census is skipped, not failed\n",
                    (unsigned int)frame, order, tag);
        return;
    }

    /* READ FIRST, PRINT LAST.  The old shape of this function printed its header and its plane rows
     * between reads, and that is not a style choice -- a console write repaints the shell window, the
     * repaint uploads the display page, and a page read after a print has measured the print.  The
     * planes are immune to that confound (the present path writes the display page, not a texture
     * page), but the display box is exactly the thing the operator says changes, so all three boxes
     * are walked and folded before one row of this reading is emitted. */
    for (i = 0u; i < npl && i < GS_TEX_MAX; i++) {
        const gs_tex_t *t = gs_tex_at(i);
        uint32_t pw = t ? t->pad_w : 0u, ph = t ? t->pad_h : 0u;

        if (pw > 512u) pw = 512u;         /* the first 512 px is the claim; a whole 1024-px sheet would
                                           * cost the run another megabyte of Local->Host DMA */
        if (ph > 512u) ph = 512u;
        gsbox_plane(&s_bx_pl[i], buf, i, pw, ph);
    }

    if (win != 0) {
        /* The client box, floored onto the 8-px grid: multiples of 4 are what a CT32 page needs and
         * multiples of 8 what a CT16 one needs, and 8 satisfies whichever LANE 21 dial is up. */
        x0 = (uint32_t)win->client.left & ~7u;
        y0 = (uint32_t)win->client.top & ~7u;
        bw = (uint32_t)(win->client.right - win->client.left) & ~7u;
        bh = (uint32_t)(win->client.bottom - win->client.top) & ~7u;

        gsbox_page(&s_bx_page, buf, width, x0, y0, bw, bh);

        /* The same box out of the desktop's own pixmap, copied band by band into the same buffer so
         * the identical walk and the identical cell law answer the identical question.  This is a host
         * -memory read, not a bus read, so it disturbs nothing -- and it is the control that says
         * whether a flat cell the page gained was art the app never had in the first place. */
        gsbox_begin(&s_bx_canvas, x0, y0, bw, bh);
        if (s_bx_canvas.have) {
            uint32_t y;

            if (x0 + s_bx_canvas.w > width)  s_bx_canvas.have = 0;
            if (y0 + s_bx_canvas.h > height) s_bx_canvas.have = 0;
            for (y = 0u; s_bx_canvas.have && y < s_bx_canvas.h; y += GSBOX_BAND) {
                const uint32_t rows = (s_bx_canvas.h - y < GSBOX_BAND) ? (s_bx_canvas.h - y) : GSBOX_BAND;
                uint32_t r, c, *fill = buf;

                for (r = 0u; r < rows; r++)
                    for (c = 0u; c < s_bx_canvas.w; c++)
                        *fill++ = canvas[(y0 + y + r) * width + (x0 + c)];
                gsbox_feed(&s_bx_canvas, buf, y0 + y, rows);
            }
            if (s_bx_canvas.have) gsbox_finish(&s_bx_canvas);
        }
    }
    Ifree(buf);

    /* ── print phase: every read of this reading is done, so a row here costs the NEXT reading, not
     * this one. ─────────────────────────────────────────────────────────────────────────────── */
    ps2_kprintf("[GSBOX] frame %u order=%s %s: planes=%u pending=%u vram_left=%u title=\"%s\"\n",
                (unsigned int)frame, order, tag, (unsigned int)npl, (unsigned int)gs_tex_pending(),
                (unsigned int)gs_tex_vram_left(), win ? win->title : "(no xmb window)");

    for (i = 0u; i < npl && i < GS_TEX_MAX; i++) {
        const gs_tex_t *t = gs_tex_at(i);

        if (!s_bx_pl[i].have) { gsbox_row(&s_bx_pl[i], "plane"); continue; }
        ps2_kprintf("[GSBOX] plane %u: byte=%u tbw=%u tba=%u pad=%ux%u texels=%ux%u qwc=%u plane=0x%x%s\n",
                    (unsigned int)i, (unsigned int)t->byte, (unsigned int)t->tbw,
                    (unsigned int)t->tba, (unsigned int)t->pad_w, (unsigned int)t->pad_h,
                    (unsigned int)t->px_w, (unsigned int)t->px_h, (unsigned int)t->qwc,
                    (unsigned int)(uintptr_t)t->plane, t->pending ? " PENDING" : "");
        gsbox_row(&s_bx_pl[i], "plane page");

        /* The registry diff, and the reason the pointer is in the row above: a re-plan is the only way
         * a session-global registry can answer a later frame differently from an earlier one without
         * any code in the app having been touched.  SAME is the answer that clears it. */
        if (s_prev_valid && i < s_prev_npl) {
            const int moved = s_prev_plane[i] != t->plane || s_prev_byte[i] != t->byte ||
                              s_prev_tbw[i] != t->tbw || s_prev_qwc[i] != t->qwc ||
                              s_prev_padw[i] != t->pad_w || s_prev_padh[i] != t->pad_h;

            if (!moved) {
                ps2_kprintf("[GSBOX] frame %u plane %u registry: SAME\n", (unsigned int)frame,
                            (unsigned int)i);
            } else {
                ps2_kprintf("[GSBOX] frame %u plane %u registry: CHANGED -- plane 0x%x -> 0x%x, byte %u "
                            "-> %u, tbw %u -> %u, qwc %u -> %u, pad %ux%u -> %ux%u\n",
                            (unsigned int)frame, (unsigned int)i,
                            (unsigned int)(uintptr_t)s_prev_plane[i], (unsigned int)(uintptr_t)t->plane,
                            (unsigned int)s_prev_byte[i], (unsigned int)t->byte,
                            (unsigned int)s_prev_tbw[i], (unsigned int)t->tbw,
                            (unsigned int)s_prev_qwc[i], (unsigned int)t->qwc,
                            (unsigned int)s_prev_padw[i], (unsigned int)t->pad_w,
                            (unsigned int)s_prev_padh[i], (unsigned int)t->pad_h);
                if (s_prev_byte[i] != t->byte)
                    ps2_kprintf("[GSBOX] frame %u plane %u registry: THE SPAN MOVED -- the old page is "
                                "abandoned and a sampler still holding the old TEX0 reads it\n",
                                (unsigned int)frame, (unsigned int)i);
            }
        }
    }
    if (s_prev_valid && (s_prev_npl != npl || s_prev_vram_left != gs_tex_vram_left()))
        ps2_kprintf("[GSBOX] frame %u %s registry: planes %u -> %u, vram_left %u -> %u\n",
                    (unsigned int)frame, tag, (unsigned int)s_prev_npl, (unsigned int)npl,
                    (unsigned int)s_prev_vram_left, (unsigned int)gs_tex_vram_left());

    for (i = 0u; i < npl && i < GS_TEX_MAX; i++) {
        const gs_tex_t *t = gs_tex_at(i);

        if (t == 0) continue;
        s_prev_plane[i] = t->plane;
        s_prev_byte[i] = t->byte; s_prev_tbw[i] = t->tbw; s_prev_qwc[i] = t->qwc;
        s_prev_padw[i] = t->pad_w; s_prev_padh[i] = t->pad_h;
    }
    s_prev_npl = npl;
    s_prev_vram_left = gs_tex_vram_left();
    s_prev_valid = 1;

    if (win == 0) {
        ps2_kprintf("[GSBOX] frame %u order=%s %s: NO XMB WINDOW -- the box census is skipped\n",
                    (unsigned int)frame, order, tag);
        return;
    }
    gsbox_row(&s_bx_page, "page");
    gsbox_row(&s_bx_canvas, "canvas");

    if (s_bx_page.have && s_bx_canvas.have &&
        s_bx_page.cell == s_bx_canvas.cell && s_bx_page.cells == s_bx_canvas.cells) {
        for (i = 0u; i < s_bx_page.cells; i++)
            if (s_bx_page.cell_first[i] == s_bx_canvas.cell_first[i] &&
                s_bx_page.cell_n[i] == s_bx_canvas.cell_n[i]) agree++;
    }

    /* The second reading of the pair, cell by cell.  `s_prev_valid` is what makes the first call a
     * control rather than a diff against zeroes, and the two numbers are read together: the page is
     * what the eye gets, the canvas is what the app painted, and only a page that went flat while its
     * canvas did not is a present-path fault. */
    if (s_prev_valid) {
        page_went = gsbox_diff(&s_bx_page, &s_prev_page, "page", tag, frame);
        canvas_went = gsbox_diff(&s_bx_canvas, &s_prev_canvas, "canvas", tag, frame);
        ps2_kprintf("[GSBOX] frame %u %s tick verdict: %s\n", (unsigned int)frame, tag,
                    page_went && !canvas_went
                        ? "THE TICK PUT FLAT CELLS ON THE PAGE THAT ITS CANVAS DOES NOT HAVE -- the "
                          "damage-driven present wrote them, not the paint: read the 20y plate cut"
                        : page_went && canvas_went
                            ? "THE CANVAS WENT FLAT TOO -- TinyGL lost the art in its own bake at the "
                              "tick, which is app/context state, not the GS stream"
                            : "THE TICK MOVED NOTHING FLAT -- the corruption is not this event at this "
                              "box: change the box or change the trigger before blaming the present");
    }

    gsprev_store(&s_prev_page, &s_bx_page);
    gsprev_store(&s_prev_canvas, &s_bx_canvas);

    /* The paired verdict, in the order that rules the harness out first.  `solid` counts are compared,
     * never thresholded: what matters is whether the page holds flat cells the canvas did not have. */
    if (!s_bx_page.have || !s_bx_canvas.have) {
        verdict = "MEASURED NOTHING -- one of the two boxes could not be read; see the FAILED rows";
    } else if (s_bx_page.err != GS_READ_BUS_OK) {
        verdict = "PAGE READ FAILED -- the row above names the bus error, and nothing here is evidence";
    } else if (s_bx_page.solid > s_bx_canvas.solid) {
        verdict = "PAGE HAS MORE FLAT CELLS THAN THE CANVAS -- the paint had art the page lost: the "
                  "stream or the sampler, not the bake";
    } else if (s_bx_page.solid < s_bx_canvas.solid) {
        verdict = "PAGE HAS FEWER FLAT CELLS THAN THE CANVAS -- the page disagrees with its own canvas in "
                  "the app's favour; read agree= before believing either";
    } else if (s_bx_canvas.solid == 0u) {
        verdict = "NEITHER BOX IS FLAT -- no rectangle to measure at this frame: this run is a control";
    } else {
        verdict = "BOTH BOXES FLAT IN THE SAME CELLS -- the rectangles are the app's own paint: TinyGL did "
                  "not sample the atlas either, which is the shared context, not the GS";
    }
    ps2_kprintf("[GSBOX] frame %u order=%s %s paired: page_solid=%u canvas_solid=%u cells_agree=%u/%u "
                "VERDICT: %s\n",
                (unsigned int)frame, order, tag, (unsigned int)s_bx_page.solid,
                (unsigned int)s_bx_canvas.solid, (unsigned int)agree,
                (unsigned int)s_bx_page.cells, verdict);
}
#endif /* BTRON_PS2_BENCH_TEX */

/* Pre-warm sub-phase: executes `lilcu` demo through shell prior to `xmb` launch
 * to verify multi-application VRAM residency and state transitions. */
static void ps2_bench_lilcu_warm(GDEV *screen)
{
    int guard;
    uint32_t made;

    ps2_kprintf("\n[BENCH] order=lilcu-first: `lilcu` through the shell, then `xmb`\n");
    s_bench_shell_lines = 0;
    s_bench_tex_warm = 1;
    s_bench_app_active = 1;
    s_bench_app_made = 0u;
    s_bench_app_over = 0u;
    s_bench_app_left = GSBOX_WARM_FRAMES;
    shell_execute_cmd("lilcu", ps2_bench_shell_out, NULL, NULL);
    for (guard = 0; guard < (int)GSBOX_WARM_FRAMES + 6; guard++) {
        if (!s_bench_app_left) break;
        if (!ps2_bench_wnd_named("Lil Cu")) break;
        ps2_gui_pass(screen);
    }
    made = s_bench_app_made;
    s_bench_app_active = 0;
    s_bench_app_left = 0u;
    s_bench_app_over = 0u;
    s_bench_app_made = 0u;
    s_bench_tex_warm = 0;
    ps2_kprintf("[BENCH] order=lilcu-first: demo echoed %d line(s), %u pass(es), its window now %s, "
                "xmb window %s, planes=%u vram_left=%u\n",
                s_bench_shell_lines, (unsigned int)made,
                ps2_bench_wnd_named("Lil Cu") ? "UP" : "closed",
                ps2_bench_wnd_named("Cross Media Bar") ? "up already" : "not yet",
                (unsigned int)gs_tex_count(), (unsigned int)gs_tex_vram_left());
#if BTRON_PS2_BENCH_TEX
    ps2_bench_gs_planes();
#endif
}
#endif /* BTRON_PS2_BENCH_TEX || BTRON_PS2_BENCH_ORDER */

#if BTRON_PS2_BENCH_GS >= 2
/* ── Live Stream VRAM Framebuffer Telemetry Census ───────────────────────────
 *
 * Direct VRAM audit of live presentation frames during active desktop execution.
 * Evaluates whether hardware-accelerated streams correctly rasterize into the
 * visible framebuffer during multi-window compositing.
 *
 * Telemetry Architecture:
 * 1. Differential Comparison (VRAM vs CPU Backbuffer):
 *    Reads back the front framebuffer and compares pixel words against the CPU
 *    backbuffer canvas. Differentiates software desktop elements from hardware
 *    accelerated primitives.
 * 2. Text Console Filtering:
 *    Console glyphs and text background pixels are identified via `GSLIVE_INK`
 *    and `GSLIVE_BG` to prevent console debug messages from registering as false-positive
 *    hardware primitive ink.
 * 3. Spatial Classification:
 *    - In-window ink: Confirms stream rasterization inside the target window client rect.
 *    - Out-of-window ink: Detects viewport offset or scissor misconfigurations.
 *    - Zero ink: Identifies pipeline stall, environment reject, or buffer clobber.
 * 4. Coordinate Placement:
 *    Diagnostic sampling strips are positioned in rows 32..47 (within the window's
 *    title bar boundary) to avoid desktop test swatches (rows 560..599) and taskbar
 *    regions while retaining clean background canvas differentials. */
#define GSLIVE_X0       32      /* the control's own box: 64 x 16 = 1,024 px, in the rung-12s strip */
#define GSLIVE_Y0       32      /* the XMB's title bar: chrome whose words are not corner words, and  */
#define GSLIVE_X1       96
#define GSLIVE_Y1       48      /* no cover rect -- so no present re-lays it over this census's ink */
#define GSLIVE_PX       ((uint32_t)(GSLIVE_X1 - GSLIVE_X0) * (uint32_t)(GSLIVE_Y1 - GSLIVE_Y0))
#define GSLIVE_INK      BTRON_PIXEL_FB(0xFFE0E0E0u)  /* ps2_kprintf's pair, in the order the canvas */
#define GSLIVE_BG       BTRON_PIXEL_FB(0xFF121B29u)  /* holds it: the macro ps2_gs_text_putc() calls
                                                      * is the only spelling that can be compared
                                                      * against a page read back.  Hand-swapping the
                                                      * bytes here instead would split the console
                                                      * class on a colour the console never writes. */

/* LANE 19 rung 12a -- the shading pair's geometry.  Rung 12a chose rows 560..575 because they were the
 * rung-6 magenta box's rows and `ctrl=1024` looked like a certification of the strip; it was not one, and
 * rung 12s named what actually lives there (the Colour Test Bar above).  The rows now come from GSLIVE_SH_Y0
 * and the pre-draw read of 12s is the certification 12a wanted: it tallies every box window BEFORE the first
 * send, so a window that already holds a corner word says so and the rung that reads it is skipped, not
 * believed.  The x ranges keep clear of the magenta box (32..95) and of each other, and both boxes are
 * 128 x 16 so the triangle inside each one covers half of it -- which is why the rows print the box's px
 * count beside the colour tallies. */
#define GSLIVE_SH_Y0       32
#define GSLIVE_SH_Y1       48
/* Rung 12s keeps reading the strip 12a..12r used, over the same six x ranges, in the same pre-draw pass --
 * not as a band but as a control on the finding itself.  If the block is the Colour Test Bar then the page
 * holds the corner words BECAUSE the canvas holds them, at the same positions, and `match` equals `tot`;
 * a corner word the page holds where the canvas holds something else is a writer this file does not own and
 * would still be unnamed after this rung.  Reading it costs one more 16-row VRAM read and settles the claim
 * with a measurement instead of with an offline reading of desktop.c's source. */
#define GSLIVE_OLD_BAND_Y0 560u   /* rows 560..575: the Colour Test Bar's top half, 12s's control band */
#define GSLIVE_SH_A_X0     112   /* IIP=1: the three corner colours ask the GS to interpolate them */
#define GSLIVE_SH_A_X1     240
#define GSLIVE_SH_B_X0     256   /* IIP=0: the same three colours with flat shading, the control */
#define GSLIVE_SH_B_X1     384
#define GSLIVE_SH_QWC_MAX  16u   /* one tag pair plus three vertices at two quadwords = 7 per group */

/* LANE 19 rung 12c/12d/12e -- the hand-built shapes, in the boxes right of rung 12a's pair and in the
 * same certified rows.  A, B, C, D and E keep the x ranges they were measured with on 2026-10-18, so a
 * later run's rows read directly against that run's; C, D and E are now POSITIVE CONTROLS (all three
 * measured as interpolating, to the pixel, on three censuses of one run -- see the 12c/12d comment
 * below for the shapes and the numbers).  F is rung 12e's one new question, in the 64 px the band has
 * left: 64x16 holds a 512-px triangle and the gradient test is a QUARTER of a box's painted px, so a
 * narrow box still has hundreds of interior px -- enough that "flat" and "gradient" cannot be confused
 * for a size artefact. */
#define GSLIVE_SH_N        6u
#define GSLIVE_SH_C_X0     400   /* ps2sdk's own Gouraud cube: AD(PRIM,RGBAQ) then a {RGBAQ,XYZ2} chain */
#define GSLIVE_SH_C_X1     520
#define GSLIVE_SH_D_X0     536   /* C's chain, PRIM from the tag instead of from the AD write */
#define GSLIVE_SH_D_X1     624
#define GSLIVE_SH_D_ASK_W  24    /* rung 12n narrowed D to this; 12s retired the narrowing (see below) */
/* Rung 12o moved D's ask 16 rows BELOW the band, into rows 576..591, to see whether the band's block
 * followed the ask.  It did not, and rung 12s names why: rows 560..599 are the Colour Test Bar, so BOTH of
 * 12o's readings -- "the block stood" and "384 red px filled the moved rect" -- were the bar's own swatch 5
 * (red, x500..599, rows 560..599), not this census's ink and not a foreign GS writer.  The dial's question
 * is therefore answered and its rows are gone: D is asked its whole window in the band again, which makes
 * `out=` identically zero, and 12o's and 12n's rows self-gate to SKIP. */
#define GSLIVE_SH_E_X0     632   /* C's chain plus an ST slot between the colour and the kick */
#define GSLIVE_SH_E_X1     720
#define GSLIVE_SH_F_X0     728   /* E's three-register chain with the recorder's OWN tag: PRE=1, no AD */
#define GSLIVE_SH_F_X1     792

/* LANE 19 rung 12j -- the SPILL window: the rows around the band that no box is read out of.
 *
 * 12i answered "the variable is the frame's ORDER", but the numbers underneath that answer never added up
 * as a shading answer.  Box D, the frame's first triangle, came back `painted=1408` -- its WHOLE rect --
 * split red=1024 blue=384, and 1024 px in an 88x16 box is a 64-wide column at full height, which is not a
 * shape a triangle makes.  Box B came back `painted=1920` of a 2,048-px rect in TWO colours (red 975,
 * blue 945), which is two triangles' coverage in one box where one packet went out.  A box that holds a
 * rectangle and two flat colours is not a box whose gradient failed; it is a box holding geometry that
 * came from somewhere else, and the 12i grid would then be reading WHERE the transfers land rather than
 * HOW they shade.
 *
 * So the same read now records WHERE each corner colour is, per box (a bbox and a count say whether the
 * colour fills its box or a stripe of it), and it records the corner colours OUTSIDE every box, in the
 * rows above and below the band -- the gaps between the boxes (96..111, 241..255, 385..399, 521..535,
 * 625..631, 721..727) and the band's neighbours, which nothing else in this census draws.  A triangle
 * assembled from stale vertex history leaves its colour there, and a triangle that merely failed to
 * interpolate leaves nothing: `spill=0` is therefore the row that makes 12i's answer mean shading, and a
 * non-zero spill is the row that makes it mean geometry.
 *
 * The window is keyed on the three corner WORDS, exactly, and on the pixel differing from the canvas.  The
 * claim this rung was built on -- that a UI pixel cannot enter the tally because "the desktop's own colours
 * are not bit-equal to GSQUAD_RED/GREEN/BLUE" -- is FALSE, and rung 12s proved it: dp.h spells COLOR_RED as
 * 0xFFFF0000 and COLOR_BLUE as 0xFF0000FF, which are GSQUAD_BLUE and GSQUAD_RED in the other reading of the
 * same word, and `desktop_render()` fills the bottom 40 rows of the page with a bar of eight 100-px
 * swatches that uses both.  That is why the spill window now sits in the strip above the client rather than
 * across the bar, and why every pre-draw count of 12s reports what the CANVAS holds at the same positions:
 * a corner word the canvas also holds is chrome, and chrome must never be printable as ink. */
/* The spill window is 16 rows above the band and 8 below it.  Its top reaches into the panel's own rows
 * 16..28, which the present re-lays as chrome -- chrome the desktop draws in gold, grey and navy, none of
 * which is a corner word, so it can only ever land in the `equal-canvas` column, never in R/G/B. */
#define GSLIVE_SP_Y0       16
#define GSLIVE_SP_Y1       56
#define GSLIVE_BB_NONE     0xFFFFFFFFu

/* LANE 19 rung 12k -- a PICTURE of the band, because rung 12j's aggregates were being argued about.
 *
 * 12j settled that the census's corner colours are not where its triangles are: box D holds two
 * full-height colour COLUMNS (red x536-599, blue x600-623, together its whole 88x16 rect), box B holds a
 * blue rect of exactly 63x15 laid over a red region of 51% coverage, and 240 red px fill the 16x15 GAP
 * between boxes B and C, which no box in this census draws.  A bounding box and a count cannot say what
 * shape makes a column, and three readings of those numbers were on the table in one conversation.  So
 * the band is now printed as a picture: one line per row, one character per four pixels of x, for every
 * box, with each cell holding the SET of classes that landed in it.
 *
 * The classes are the three categories rung 12a's grading already uses -- a corner colour, the
 * interpolated field (`other`) and the canvas value the pixel was read out of -- and a cell holds the
 * SET of classes that landed in it, not its majority.  Uppercase is a cell holding only corner colours,
 * lowercase the same set once it shares the cell with field ink, because a cell that saw red AND the
 * field is an edge of a ramp while a cell that saw only red is a flat span, and telling those two apart
 * is the question this rung exists to answer. */
#define GSLIVE_PIC_SHIFT   2u                        /* 4 px per column */
#define GSLIVE_PIC_COLS    (128u >> GSLIVE_PIC_SHIFT) /* the widest box is 128 px (A and B) */
#define GSLIVE_PIC_ROWS    (GSLIVE_SH_Y1 - GSLIVE_SH_Y0)
#define GSLIVE_PIC_R       1u
#define GSLIVE_PIC_G       2u
#define GSLIVE_PIC_B       4u
#define GSLIVE_PIC_FIELD   8u   /* painted, and neither a corner colour nor the canvas value */
#define GSLIVE_PIC_CANVAS 16u   /* the pixel still holds the canvas it was read against */

/* corner-colour + field bits -> character.  Index is the low four class bits, so every combination names
 * itself: 0-7 are whole corner colours (Y=R+G, M=R+B, C=G+B, W=all three) and 8-15 the same sets once
 * sharing a cell with field ink (`f` is field alone).  The canvas bit is deliberately NOT in this table:
 * it is consulted only when a cell saw nothing but the canvas, so an untouched cell reads `.` and a
 * column past this box's width reads a space, while a cell that mixes ink and canvas reads as its ink --
 * 12j's per-box `canvas=` count is what says how much of a box was never touched. */
static const char s_pic_chr[16] = {
    /* 0x0 no class seen (only reachable as `.` or a space, via the canvas bit below) */
    /* 0x1..0x7 whole corner colours, alone and in pairs */
    ' ', 'R', 'G', 'Y', 'B', 'M', 'C', 'W',
    /* 0x8 field alone; 0x9..0xf a corner colour sharing its 4-px cell with field ink:
     * r=R+f g=G+f y=R+G+f b=B+f m=R+B+f c=G+B+f w=all three+f */
    'f', 'r', 'g', 'y', 'b', 'm', 'c', 'w'
};

/* One band picture, in the order the boxes are drawn.  6 x 16 x 32 bytes = 3 kB of image; rung 10's two
 * tile grids already cost 30 kB, and the alternative -- a 4-px-per-column mask of the whole 800-px page --
 * is 16 kB the pool does not have. */
static uint8_t s_band_pic[GSLIVE_SH_N][GSLIVE_PIC_ROWS][GSLIVE_PIC_COLS];

/* Fold one pixel into a {minx, maxx, miny, maxy} box held in page coordinates, an inclusive max, and
 * GSLIVE_BB_NONE until the box has seen a pixel -- the empty case has to be distinguishable from the box
 * at (0,0), which is where the console's own corner colour lives. */
static void gslive_bbox(uint32_t bb[4], uint32_t x, uint32_t y)
{
    if (bb[0] == GSLIVE_BB_NONE || x < bb[0]) bb[0] = x;
    if (bb[1] == GSLIVE_BB_NONE || x > bb[1]) bb[1] = x;
    if (bb[2] == GSLIVE_BB_NONE || y < bb[2]) bb[2] = y;
    if (bb[3] == GSLIVE_BB_NONE || y > bb[3]) bb[3] = y;
}

/* Which condition let a canvas rect out INSIDE a live layer's client -- rung 20x's name for the
 * producer, spelled into the eraser list so a rect does not have to be recognised by its size. */
static const char *ps2_bench_why_name(uint8_t why)
{
    switch (why) {
    case PS2_WHY_STREAM:   return "/before-stream";
    case PS2_WHY_CUT:      return "/cut";
    case PS2_WHY_NO_LAYER: return "/no-layer";
    case PS2_WHY_NO_RECT:  return "/no-rect";
    case PS2_WHY_RELAY:    return "/staged-none";
    case PS2_WHY_COVER:    return "/cover";
    case PS2_WHY_PLATED:   return "/plated-repaint";
    case PS2_WHY_NOCUT:    return "/canvas-lays-over-layer";
    default:               return "/?";
    }
}

/* Does this overlay's rect -- after the present's own clip to the layer's client, which is what the
 * ring recorded -- name the cover rect the census is holding?  The comparison is the intersection
 * and not the raw rect because ps2_upload_cover_in_layer() clips before it sends. */
static int ps2_bench_cover_is(const RECT *r, const int *clip_rect, int cx0, int cy0, int cx1, int cy1)
{
    const int x0 = r->left > cx0 ? r->left : cx0;
    const int y0 = r->top  > cy0 ? r->top  : cy0;
    const int x1 = r->right < cx1 ? r->right : cx1;
    const int y1 = r->bottom < cy1 ? r->bottom : cy1;

    return x1 > x0 && y1 > y0 &&
           x0 == clip_rect[0] && y0 == clip_rect[1] &&
           x1 == clip_rect[2] && y1 == clip_rect[3];
}

/* LANE 19 rung 10 -- the page as a PICTURE, in the log.
 *
 * Why a picture and not another count: rung 6 can print `in_win=383,933 of 384,000 px differ from
 * the canvas`, and on 2026-10-17 the operator looked at that same window and saw dots on gray.  Both
 * statements were about the same rectangle and only one of them was about the picture.  What the
 * census has never printed is WHICH colour is WHERE -- and its own histogram came close (`ink:
 * 0xfe9d4c01 n=356,087 box=16,52..782,550`) without being readable as an image: 356,087 px of ONE
 * colour inside the client rect is 93% of the window flat, and that IS the empty window, told as a
 * number nobody looked at.
 *
 * So this keeps the whole page -- the 800x600 rung 3 already reads back over Local->Host -- and
 * reduces it to a grid of 16x16 tiles, each holding its majority colour, printed twice: P for the GS
 * page as read, C for the EE canvas the compositor painted.  50 columns by 38 rows, one character per
 * tile, the characters indexing a legend of the colours themselves.  `scripts/ps2_page_pic.py` turns
 * those blocks back into two PNGs, which is where the page can be compared with the operator's
 * photograph instead of argued with.
 *
 * The reduction is stated, not hidden: the majority is Boyer-Moore's candidate -- exact for a tile the
 * picture fills with one colour, an approximation for a tile that straddles an edge -- a colour the
 * 32-slot legend cannot hold prints as `.` and is counted in `other=`, and a tile whose pixels all
 * came back SENT prints as a space.  A busy screen therefore says it was flattened rather than being
 * silently flattened.  Costs: 30 kB of BSS for the two grids, one compare per pixel over the 480,000
 * the read already walks, and about 2 kB of log per picture. */
#define GSPIK_TILE  16u
#define GSPIK_COLS  ((uint32_t)PS2_SCREEN_WIDTH / GSPIK_TILE)   /* 50 across an 800 px page */
#define GSPIK_ROWS  (((uint32_t)PS2_SCREEN_HEIGHT + GSPIK_TILE - 1u) / GSPIK_TILE)  /* 38 for 600 */
#define GSPIK_PAL   32u
#define GSPIK_CHARS "0123456789abcdefghijklmnopqrstuv"          /* the 32 legend characters */

typedef struct {
    uint32_t v[GSPIK_ROWS][GSPIK_COLS];   /* the tile's majority candidate, as the page holds it */
    uint32_t n[GSPIK_ROWS][GSPIK_COLS];   /* its weight; 0 until the tile's first pixel arrives */
} gspeak_t;

static gspeak_t s_spk_page, s_spk_canvas;
static uint32_t s_spk_pal[GSPIK_PAL], s_spk_pal_n;
static uint32_t s_spk_other_p, s_spk_other_c;   /* tiles whose colour found no legend slot */
static uint32_t s_spk_blank_p, s_spk_blank_c;   /* tiles with nothing read: SENT pixels */
static uint32_t s_spk_sent;                     /* px the read did not deliver, whole page */
static uint32_t s_spk_printed;                  /* pictures per run: two, then no more */

static void gspeak_add(gspeak_t *g, uint32_t x, uint32_t y, uint32_t v)
{
    const uint32_t r = y / GSPIK_TILE;
    const uint32_t c = x / GSPIK_TILE;

    if (r >= GSPIK_ROWS || c >= GSPIK_COLS) return;
    if (g->n[r][c] == 0u || g->v[r][c] == v) {
        g->v[r][c] = v;
        g->n[r][c]++;
    } else {
        g->n[r][c]--;
    }
}

static void gspeak_reset(void)
{
    uint32_t r, c;

    for (r = 0u; r < GSPIK_ROWS; r++)
        for (c = 0u; c < GSPIK_COLS; c++) {
            s_spk_page.n[r][c] = 0u;
            s_spk_canvas.n[r][c] = 0u;
        }
    s_spk_pal_n = 0u;
    s_spk_other_p = s_spk_other_c = 0u;
    s_spk_blank_p = s_spk_blank_c = 0u;
    s_spk_sent = 0u;
}

/* The legend's index for a colour, taking a slot if this is the first tile that carries it.  -1 means
 * the legend is full: the tile prints as `.` and the caller's `other=` says how many did, so a page
 * richer than 32 tile colours is reported as flattened rather than being quietly one. */
static int gspeak_pal(uint32_t v)
{
    uint32_t i;

    for (i = 0u; i < s_spk_pal_n; i++)
        if (s_spk_pal[i] == v) return (int)i;
    if (s_spk_pal_n < GSPIK_PAL) {
        s_spk_pal[s_spk_pal_n] = v;
        return (int)s_spk_pal_n++;
    }
    return -1;
}

/* One grid, as rows of legend characters.  The legend must already be closed -- gspeak_dump() walks
 * both grids first -- so nothing here can add a colour and leave an earlier row's character meaning
 * something else. */
static void gspeak_print(const char *tag, const gspeak_t *g, uint32_t blanks)
{
    char row[GSPIK_COLS + 2];
    uint32_t r, c;

    ps2_kprintf("[GSDUMP] %s: %u tile(s) had no pixel read back (printed ' ')\n",
                tag, (unsigned int)blanks);
    for (r = 0u; r < GSPIK_ROWS; r++) {
        for (c = 0u; c < GSPIK_COLS; c++) {
            int idx;

            if (g->n[r][c] == 0u) { row[c] = ' '; continue; }
            idx = gspeak_pal(g->v[r][c]);
            row[c] = idx < 0 ? '.' : GSPIK_CHARS[idx];
        }
        row[GSPIK_COLS] = '\0';
        ps2_kprintf("[GSDUMP] %s %u %s\n", tag, (unsigned int)r, row);
    }
}

static void gspeak_dump(void)
{
    uint32_t r, c, i;

    ps2_kprintf("[GSDUMP] the page as a picture: %ux%u px per tile, %ux%u grid.  "
                "P = the GS page as read back, C = the EE canvas the compositor painted.  "
                "One character per tile = its majority colour; scripts/ps2_page_pic.py renders the "
                "P and C blocks as PNGs and as text.  "
                "WORDS ARE 0xAABBGGRR, NOT 0xAARRGGBB: the GS stores a CT32 pixel with r in the low "
                "byte and the canvas holds the same order (BTRON_PIXEL_FB, include/btron/types.h), "
                "so decoding these as ARGB exchanges red and blue and turns the XMB's deep blue into "
                "a brown field -- the wrong-diagnosis trap this row now names out loud.\n",
                (unsigned int)GSPIK_TILE, (unsigned int)GSPIK_TILE,
                (unsigned int)GSPIK_COLS, (unsigned int)GSPIK_ROWS);
    ps2_kprintf("[GSDUMP] coverage: %u px of the page came back SENT (the read did not deliver them)\n",
                (unsigned int)s_spk_sent);

    /* Pass one closes the legend over both grids, P first, and tallies what it could not hold. */
    for (r = 0u; r < GSPIK_ROWS; r++) {
        for (c = 0u; c < GSPIK_COLS; c++) {
            if (s_spk_page.n[r][c] != 0u) {
                if (gspeak_pal(s_spk_page.v[r][c]) < 0) s_spk_other_p++;
            } else s_spk_blank_p++;
            if (s_spk_canvas.n[r][c] != 0u) {
                if (gspeak_pal(s_spk_canvas.v[r][c]) < 0) s_spk_other_c++;
            } else s_spk_blank_c++;
        }
    }
    for (i = 0u; i < s_spk_pal_n; i += 8u) {
        uint32_t j;

        ps2_kprintf("[GSDUMP] pal %c=%08x", GSPIK_CHARS[(int)i], (unsigned int)s_spk_pal[i]);
        for (j = i + 1u; j < s_spk_pal_n && j < i + 8u; j++)
            ps2_kprintf(" %c=%08x", GSPIK_CHARS[(int)j], (unsigned int)s_spk_pal[j]);
        ps2_kprintf("\n");
    }
    ps2_kprintf("[GSDUMP] other: P=%u C=%u tile(s) printed `.` (the legend holds %u colour(s))\n",
                (unsigned int)s_spk_other_p, (unsigned int)s_spk_other_c,
                (unsigned int)s_spk_pal_n);

    /* Pass two prints, and the characters it writes index the legend the lines above just named. */
    gspeak_print("P", &s_spk_page, s_spk_blank_p);
    gspeak_print("C", &s_spk_canvas, s_spk_blank_c);
}

/* LANE 20 rung 21: one rect of the page at ONE PIXEL PER PIXEL, because rung 10's grid cannot show
 * what an icon is doing wrong.
 *
 * Rung 10 flattens the page to a character per 16x16 tile and its legend holds 32 colours.  On the
 * eegs backend the page carries hundreds -- the honest run of 20y/20z printed `other: P=1278` of the
 * 1,900 tiles, i.e. the picture of the very backend this lane exists to look at came back a field of
 * legend-overflow magenta.  And a count cannot answer the operator's actual question: a half-texel
 * bleed off an atlas cell, one flat texel where a picture should be, an edge one pixel late and a
 * missing alpha ramp are all the SAME number of "ink px inside the client".  Only the pixels can say
 * which of the four it is.
 *
 * So this prints the raw words of one small square -- by default the 96x96 at the CENTRE of the GS
 * layer's client, which is where the XMB puts its selected icon -- eight hex digits per pixel, and
 * scripts/ps2_page_pix.py turns them back into a PNG at 1:1 and prints the same grid as text.  The
 * rect is named in its own header row rather than assumed, so a dump that missed the icon says so.
 *
 * Cost: 96x96 px is 73 kB of log, at most two dumps per run, on rung 10's own gate. */
#define GSPIX_SIDE      96u     /* the square, in page pixels */
#define GSPIX_PER_LINE  24u     /* 24 words + the y/x prefix is ~210 chars, the house line width */

static uint32_t s_gspix_printed;

/* The page rows a rung-21 map is about to print, read into the caller's band scratch at exactly the
 * geometry the census's own counts use: x = 0, the full screen width, at most GSREAD_BAND_ROWS rows
 * per call, and the row that comes back at index 0 is the `y0` argument.
 *
 * This exists because all three maps used to index the census's `buf` by ABSOLUTE page y while
 * `buf` is a BAND buffer -- GSREAD_BAND_ROWS rows of the one band most recently read, which after
 * the census's read loop is rows 450..599.  A map walking the client at y=52..552 therefore read
 * 3.7x past the end of a 480 kB allocation, and the 11,714 "cells of the word 0x00000000" that
 * rungs 21c, 23 and 24 were argued on are that memory's own zeros, not the page's.  Every coverage
 * claim those rungs restated is re-opened by this line, and rung 26's correctly-indexed box reads
 * are the measurement it replaces. */
static int gspeak_rows(uint32_t *scratch, uint32_t cap_px, uint32_t width, int y0, int rows)
{
    uint32_t i;

    if (rows <= 0 || (uint64_t)(uint32_t)rows * width > (uint64_t)cap_px) return -1;

    for (i = 0u; i < width * (uint32_t)rows; i++) scratch[i] = GSREAD_SENT;
    if (ps2_gs_vram_read(0, y0, (int)width, rows, scratch) <= 0) return -1;
    return ps2_gs_vram_read_err() == GS_READ_BUS_OK ? 0 : -1;
}

static void gspeak_pixels(uint32_t *buf, uint32_t cap_px, uint32_t width,
                          int cx0, int cy0, int cx1, int cy1)
{
    const int side = (int)GSPIX_SIDE;
    const int x0 = (cx0 + cx1 - side) / 2;
    const int y0 = (cy0 + cy1 - side) / 2;
    int y, x;

    /* Refuse rather than clamp: a rect that runs off the client would print desktop chrome beside the
     * window's own pixels and leave the reader to work out which is which. */
    if (x0 < cx0 || y0 < cy0 || x0 + side > cx1 || y0 + side > cy1) {
        ps2_kprintf("[GSPX] SKIP: the client %d,%d..%d,%d is smaller than the %u px square, so there "
                    "is no centered rect to name\n", cx0, cy0, cx1, cy1, (unsigned int)GSPIX_SIDE);
        return;
    }
    if (gspeak_rows(buf, cap_px, width, y0, side) < 0) {
        ps2_kprintf("[GSPX] SKIP: the square's %d rows at y=%d did not come back, so the words below "
                    "would not be the page's\n", side, y0);
        return;
    }

    ps2_kprintf("[GSPX] rect=%d,%d..%d,%d %dx%d px, %u words per line, 8 hex digits per px.  "
                "WORDS ARE 0xAABBGGRR, the order [GSDUMP]'s palette prints.  A word equal to "
                "0x%08x is GSREAD_SENT: the read did not deliver that pixel.\n",
                x0, y0, x0 + side, y0 + side, side, side,
                (unsigned int)GSPIX_PER_LINE, (unsigned int)GSREAD_SENT);
    for (y = y0; y < y0 + side; y++) {
        const uint32_t *row = buf + (size_t)(y - y0) * (size_t)width;

        for (x = x0; x < x0 + side; x += (int)GSPIX_PER_LINE) {
            uint32_t i;

            ps2_kprintf("[GSPX] y=%-4d x=%-4d", y, x);
            for (i = 0u; i < GSPIX_PER_LINE; i++)
                ps2_kprintf(" %08x", (unsigned int)row[(size_t)x + (size_t)i]);
            ps2_kprintf("\n");
        }
    }
}

/* LANE 20 rung 21b: the same page as a map over the WHOLE client, because the square above answers
 * "what does one icon look like" and cannot answer "where does the stream's ink stop".
 *
 * The first run of `gspeak_pixels()` is why this exists: the 96x96 at the client's centre came back
 * 6,869 px of the word 0x00000000 -- a pixel with no colour and no alpha, i.e. VRAM nobody wrote --
 * and 938 px of one near-white word, in horizontal dashes one and two pixels tall.  That is not an
 * icon and not a gradient, and a 96 px square cannot say what it is: the question is now how big the
 * unpainted region is and what shape its edge has, which is a whole-client question.
 *
 * So this walks the client at 4 px per character and prints THREE maps of it, because one word has
 * three separate questions in it: `gspeak_lum_map`'s original ramp says HOW BRIGHT a cell is (`k`
 * when all 16 of its pixels are the zero word, `?` when any is the read's own SENT sentinel, else a
 * ten-step luminance ramp); the ALPHA map says WHO painted it; and the HUE map plus a word histogram
 * says WHICH CHAIN's colour it is.  A ramp rather than the rung 10 legend because a ramp cannot
 * saturate -- the eegs page holds hundreds of distinct words and a 32-slot colour legend overflows at
 * 1,278 of its 1,900 tiles, which is how rung 10's picture came back one flat magenta.
 *
 * Each of the three maps now reads the page itself, in blocks of at most `GSREAD_BAND_ROWS` rows,
 * into the caller's band buffer and with buffer indices relative to the block it just read.  It used
 * to walk `buf` by ABSOLUTE page y, on the belief that the census had left a whole page there; the
 * census leaves one 150-row band, so every row below the fourth was the heap past the end of a
 * 480 kB allocation.  That is the whole of the "11,714 cells of the zero word" the maps reported --
 * `k` filled the bottom of the client uniformly, across all 192 columns, and stopped within a cell of
 * page y=150, which is the buffer's own end and not a feature of the page.  Costs: 192 chars x 125
 * rows x 3 maps = 72 kB of log, and 12 Local<-Host reads, once per run. */
#define GSPIX_MAP_STEP  4u                      /* page px per character, both axes */
#define GSPIX_MAP_COLS  196u                    /* a 784 px client at 4 px/char is 196 chars */

static uint32_t s_gspix_mapped;

/* The alpha byte, which is the whole of what the second map below is for.  Both the stream's ink
 * and the compositor's plate carry a distinctive one: the gradient and the ribbon are built from
 * vertices whose RGBA is 0xfe______ (the XMB's own `sf`-scaled alpha), a canvas upload writes 0xff,
 * and the skipped body plate under a GL layer is the zero word.  So the alpha alone says WHO painted
 * a cell, which the luminance cannot when the ink and the plate are both dark. */
#define GSPIX_ALPHA(v)  ((uint32_t)((v) >> 24))

/* The three rung-21 maps of the GS layer's client, read in `GSREAD_BAND_ROWS` blocks. */
static void gspeak_page_map(uint32_t *buf, uint32_t cap_px, uint32_t width,
                            int cx0, int cy0, int cx1, int cy1)
{
    static const char ramp[] = " .:-=+*#%@";    /* dark to bright; index 0 is a real space */
    static char line[GSPIX_MAP_COLS + 1];
    int x, y, c, chunk;

    ps2_kprintf("[GSPIXM] client=%d,%d..%d,%d at %u px per char: `k` = all 16 px are the zero word "
                "(nobody wrote this VRAM), `?` = a pixel the read did not deliver, else a 10-step "
                "luminance ramp of the cell.  Rows are page y, columns start at x=%d.  Each %u-row "
                "block below is its own Local<-Host read.\n",
                cx0, cy0, cx1, cy1, (unsigned int)GSPIX_MAP_STEP, cx0,
                (unsigned int)GSREAD_BAND_ROWS);

    for (chunk = cy0; chunk < cy1; chunk += (int)GSREAD_BAND_ROWS) {
        const int rows = (cy1 - chunk < (int)GSREAD_BAND_ROWS) ? cy1 - chunk
                                                              : (int)GSREAD_BAND_ROWS;

        if (gspeak_rows(buf, cap_px, width, chunk, rows) < 0) {
            ps2_kprintf("[GSPIXM] SKIP at y=%d: the block did not come back, so the ramp below it "
                        "would be the heap, not the page\n", chunk);
            break;
        }

        for (y = chunk; y < chunk + rows; y += (int)GSPIX_MAP_STEP) {
            c = 0;
            for (x = cx0; x + (int)GSPIX_MAP_STEP <= cx1 && c < (int)GSPIX_MAP_COLS;
                 x += (int)GSPIX_MAP_STEP) {
                uint32_t lum = 0u, n = 0u;
                int all_zero = 1, any_sent = 0, dy, dx;

                for (dy = 0; dy < (int)GSPIX_MAP_STEP; dy++) {
                    const uint32_t *row = buf + (size_t)(y + dy - chunk) * (size_t)width;

                    for (dx = 0; dx < (int)GSPIX_MAP_STEP; dx++) {
                        const uint32_t v = row[(size_t)x + (size_t)dx];

                        if (v == GSREAD_SENT) any_sent = 1;
                        if (v != 0u) all_zero = 0;
                        /* 0xAABBGGRR, the order the page holds: the ITU weights, integer. */
                        lum += ((v & 0xFFu) * 30u + ((v >> 8) & 0xFFu) * 59u +
                                ((v >> 16) & 0xFFu) * 11u) / 100u;
                        n++;
                    }
                }
                if (any_sent) line[c++] = '?';
                else if (all_zero) line[c++] = 'k';
                else line[c++] = ramp[(int)((n ? lum / n : 0u) * 9u / 255u)];
            }
            line[c] = '\0';
            ps2_kprintf("[GSPIXM] y=%-4d %s\n", y, line);
        }
    }

    /* Rung 21c: the same client, classified by WHO painted it rather than by how bright it is.
     *
     * What this was argued on is gone.  The first version of both maps indexed the census's band
     * buffer by ABSOLUTE page y, so everything past the fourth band was heap, and the claim the alpha
     * map then "proved" -- that the stream covers only about a quarter of its own client and the rest
     * is the zero word -- is that heap's zeros.  Rung 26, which reads 64x16 boxes at the block-local
     * geometry a band buffer forces, found stream ink (A=0xfe) in every box across the client on 14
     * consecutive frames, and the census's own correctly-indexed counts agree with it: `survival:
     * census #1 client ink=380861 canvas=3139 of 384000 px`.  So the open question is no longer
     * WHETHER the page is inked -- it is 99% inked -- but what colour that ink is, because what the
     * operator sees is a grey wave where the XMB's blue gradient should be.
     *
     * The alpha still separates the painters, and that is still worth a map: the gradient's own
     * vertices arrive with A=0xfe and nothing else on this path writes that byte, a canvas upload
     * writes 0xff, and a skipped body plate is the zero word.  `e` cells are the stream's ink; `k`
     * cells are nobody's.  A map that is `e` where the ribbon and gradient were recorded says the GS
     * drew; one that is `f` there says a canvas upload landed on top of it after the stream. */
    ps2_kprintf("[GSPIXA] same client by ALPHA owner: `k` zero word (plate: nobody wrote it), "
                "`e` A=0xfe (the stream's own ink), `f` A=0xff (a canvas or opaque upload), "
                "`0` A=0x00 with colour, `m` mixed alpha in the cell, `o` other, `?` undelivered.\n");

    {
        uint32_t n_k = 0u, n_e = 0u, n_f = 0u, n_0 = 0u, n_m = 0u, n_o = 0u, n_q = 0u, n_cells = 0u;
        uint32_t bad = 0u;

        for (chunk = cy0; chunk < cy1; chunk += (int)GSREAD_BAND_ROWS) {
            const int rows = (cy1 - chunk < (int)GSREAD_BAND_ROWS) ? cy1 - chunk
                                                                  : (int)GSREAD_BAND_ROWS;

            if (gspeak_rows(buf, cap_px, width, chunk, rows) < 0) {
                bad++;
                ps2_kprintf("[GSPIXA] SKIP at y=%d: the block did not come back\n", chunk);
                break;
            }

            for (y = chunk; y < chunk + rows; y += (int)GSPIX_MAP_STEP) {
                uint32_t a_fe = 0u, a_ff = 0u, a_00 = 0u, a_other = 0u;
                char cls;
                c = 0;

                for (x = cx0; x + (int)GSPIX_MAP_STEP <= cx1 && c < (int)GSPIX_MAP_COLS;
                     x += (int)GSPIX_MAP_STEP) {
                    int all_zero = 1, any_sent = 0, dy, dx;

                    a_fe = a_ff = a_00 = a_other = 0u;
                    for (dy = 0; dy < (int)GSPIX_MAP_STEP; dy++) {
                        const uint32_t *row = buf + (size_t)(y + dy - chunk) * (size_t)width;

                        for (dx = 0; dx < (int)GSPIX_MAP_STEP; dx++) {
                            const uint32_t v = row[(size_t)x + (size_t)dx];

                            if (v == GSREAD_SENT) any_sent = 1;
                            if (v != 0u) {
                                all_zero = 0;
                                if (GSPIX_ALPHA(v) == 0xfeu) a_fe++;
                                else if (GSPIX_ALPHA(v) == 0xffu) a_ff++;
                                else if (GSPIX_ALPHA(v) == 0x00u) a_00++;
                                else a_other++;
                            }
                        }
                    }
                    n_cells++;
                    if (any_sent) cls = '?';
                    else if (all_zero) cls = 'k';
                    else if (a_other + a_00 > GSPIX_MAP_STEP * GSPIX_MAP_STEP / 2u) cls = 'o';
                    else if (a_other != 0u || a_00 != 0u || (a_fe != 0u && a_ff != 0u)) cls = 'm';
                    else if (a_fe) cls = 'e';
                    else cls = 'f';

                    if (a_00) n_0++;
                    line[c++] = cls;
                    if (cls == '?') n_q++;
                    else if (cls == 'k') n_k++;
                    else if (cls == 'e') n_e++;
                    else if (cls == 'f') n_f++;
                    else if (cls == 'm') n_m++;
                    else n_o++;
                }
                line[c] = '\0';
                ps2_kprintf("[GSPIXA] y=%-4d %s\n", y, line);
            }
        }

        ps2_kprintf("[GSPIXA] cells=%u: k(zero)=%u e(A=0xfe stream ink)=%u f(A=0xff upload)=%u "
                    "m=%u o=%u cells-holding-A=0x00=%u ?(sent)=%u blocks-unread=%u\n",
                    n_cells, n_k, n_e, n_f, n_m, n_o, n_0, n_q, bad);
    }

    /* Rung 21d: the same client by HUE, and the 12 words it is actually made of.
     *
     * The alpha map above cannot finish the job because the alpha byte is shared: chain 0's
     * `eegs_clear` writes 0xff000000 and a canvas upload writes 0xffRRGGBB, so its `f` says "an
     * opaque write" and cannot say which.  What it CAN no longer say is anything about coverage --
     * see the note above the alpha map for why its `k` count was the heap and not the page, and why
     * rung 23's identical 11,714 under the cut proves only that the same heap was still there.
     *
     * Hue is the map that answers the question the operator is actually asking.  The XMB's chains are
     * colour-coded by what they are: the gradient is blue (0x014c9d-ish), the clear is black, the
     * ribbon is dark grey, the icons and labels are near-white.  So "the wave is grey, not blue" is
     * a sentence this map can read directly: a client that is `B` where the gradient was recorded and
     * `D`/`G` elsewhere says the gradient's own words never reached the page, and a client that is
     * `B` but still looks grey says the page is right and the DISPLAY is not.  `M` marks a cell that
     * holds two classes, which is a real edge rather than a fog.  The histogram samples one pixel per
     * cell (24,000 of the 384,000) and prints the dozen words that recur most, so a band's owner can
     * be read as a word and then looked up in the [GSCHAIN] table instead of guessed at. */
    ps2_kprintf("[GSPXH] same client by HUE: `k` zero word, `D` dark (all channels < 24), "
                "`W` near-white (all >= 180), `B` blue-dominant, `R` red-dominant, `G` grey, "
                "`O` other, `M` a cell holding two of them, `?` undelivered.\n");

    {
        static const char hue_letters[] = "DWBRGO";    /* bit 1<<i of a pixel's class, in order */
        static uint32_t hist_w[256], hist_n[256];
        uint32_t n_cells2 = 0u, n_ovf = 0u, bad = 0u;
        int i;

        for (i = 0; i < 256; i++) { hist_w[i] = 0xFFFFFFFFu; hist_n[i] = 0u; }

        for (chunk = cy0; chunk < cy1; chunk += (int)GSREAD_BAND_ROWS) {
            const int rows = (cy1 - chunk < (int)GSREAD_BAND_ROWS) ? cy1 - chunk
                                                                  : (int)GSREAD_BAND_ROWS;

            if (gspeak_rows(buf, cap_px, width, chunk, rows) < 0) {
                bad++;
                ps2_kprintf("[GSPXH] SKIP at y=%d: the block did not come back\n", chunk);
                break;
            }

            for (y = chunk; y < chunk + rows; y += (int)GSPIX_MAP_STEP) {
                char cls;

                c = 0;
                for (x = cx0; x + (int)GSPIX_MAP_STEP <= cx1 && c < (int)GSPIX_MAP_COLS;
                     x += (int)GSPIX_MAP_STEP) {
                    uint32_t cell_bits = 0u;
                    int cell_zero = 1, cell_sent = 0, dy, dx;

                    for (dy = 0; dy < (int)GSPIX_MAP_STEP; dy++) {
                        const uint32_t *row = buf + (size_t)(y + dy - chunk) * (size_t)width;

                        for (dx = 0; dx < (int)GSPIX_MAP_STEP; dx++) {
                            const uint32_t v = row[(size_t)x + (size_t)dx];
                            const uint32_t r = v & 0xFFu, g = (v >> 8) & 0xFFu, b = (v >> 16) & 0xFFu;
                            const uint32_t hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
                            const uint32_t lo = r < g ? (r < b ? r : b) : (g < b ? g : b);

                            if (v == GSREAD_SENT) { cell_sent = 1; continue; }
                            if (v == 0u) continue;
                            cell_zero = 0;

                            if (hi < 24u) cell_bits |= 1u;                            /* D dark */
                            else if (lo >= 180u) cell_bits |= 2u;                      /* W near-white */
                            else if (b >= r + 24u && b >= g + 24u) cell_bits |= 4u;    /* B blue */
                            else if (r >= g + 24u && r >= b + 24u) cell_bits |= 8u;    /* R red */
                            else if (hi - lo < 16u) cell_bits |= 16u;                  /* G grey */
                            else cell_bits |= 32u;                                     /* O other */
                        }
                    }

                    if (cell_sent) cls = '?';
                    else if (cell_zero) cls = 'k';
                    else if ((cell_bits & (cell_bits - 1u)) != 0u) cls = 'M';
                    else {
                        uint32_t bit = cell_bits, k = 0u;

                        while ((bit & 1u) == 0u) { bit >>= 1; k++; }
                        cls = hue_letters[k];
                    }
                    line[c++] = cls;
                    n_cells2++;

                    /* One sampled word per cell, for the histogram. */
                    if (!cell_zero && !cell_sent) {
                        const uint32_t v = buf[(size_t)(y + (int)GSPIX_MAP_STEP / 2 - chunk)
                                                     * (size_t)width +
                                               (size_t)x + (int)GSPIX_MAP_STEP / 2];
                        const uint32_t h = (v * 2654435761u) >> 24u;   /* 8-bit slot */
                        int probe;

                        for (probe = 0; probe < 8; probe++) {
                            const uint32_t s = (h + (uint32_t)probe) & 255u;

                            if (hist_w[s] == v) { hist_n[s]++; break; }
                            if (hist_w[s] == 0xFFFFFFFFu) { hist_w[s] = v; hist_n[s] = 1u; break; }
                        }
                        if (probe == 8) n_ovf++;   /* eight collisions: this sample is not in the table */
                    }
                }
                line[c] = '\0';
                ps2_kprintf("[GSPXH] y=%-4d %s\n", y, line);
            }
        }

        ps2_kprintf("[GSPXH] cells=%u of 16 px = %u px sampled at 1 word per cell; "
                    "table-overflow=%u blocks-unread=%u\n",
                    n_cells2, n_cells2 * GSPIX_MAP_STEP * GSPIX_MAP_STEP, n_ovf, bad);

        /* The dozen most frequent words, by repeated max selection over the 256 slots: no sort,
         * no allocation, and the table is small enough that twelve passes are nothing. */
        ps2_kprintf("[GSPXH] the client's most frequent words (0xAABBGGRR):\n");
        for (i = 0; i < 12; i++) {
            uint32_t best = 0u, bi = 0xFFFFFFFFu;
            int k;

            for (k = 0; k < 256; k++) {
                if (hist_w[k] == 0xFFFFFFFFu) continue;
                if (hist_n[k] > best) { best = hist_n[k]; bi = (uint32_t)k; }
            }
            if (bi == 0xFFFFFFFFu || best == 0u) break;
            ps2_kprintf("[GSPXH]   #%02d 0x%08x  %u cell-sample(s)\n", i + 1, hist_w[bi], best);
            hist_n[bi] = 0u;
        }
    }
}

/* LANE 19 rung 12a -- does THIS GS interpolate the three colours THIS STREAM hands it?
 *
 * Everything below the question has been answered by measurement.  The recorded group's shape draws:
 * rung 5's dense two-quadword REGLIST filled its box to the pixel, and rung 6's census counts
 * hundreds of thousands of client pixels that differ from their canvas.  The colours are not all the
 * same at the top of the pipeline either: `colchg=` counts changes in the vertex stream.  What the
 * operator sees is one flat plate where the XMB's four-corner gradient should be, and the flat word
 * that census named (0xfe9d4c01) is exactly the FIRST vertex of `xb_draw_bg()`'s quad scaled by its
 * alpha -- which is what a GS with IIP off, or with only one RGBAQ in its registers, paints.  The one
 * register between those two facts is the interpolation flag, and no rung so far has varied it
 * deliberately: rung 11 found `iip=24/25` at the PRIM declarations, which says the word was ASKED
 * for, not that the rasteriser honoured it or that the three colours arrived distinct.
 *
 * So this draws the pair directly through `eegs_triangle()` -- the same recorder, the same group, the
 * same PRE, the same stride as the XMB's own stream -- and not through the SPRITE poster every earlier
 * control used, because a SPRITE has no second vertex to interpolate with.  Box A is one triangle with
 * red at its bottom-left, green at its bottom-right and blue at its apex, asked with IIP=1.  Box B is
 * the same three vertices asked with IIP=0, and it is the control: it is expected to come back ONE
 * colour, and which of the three it comes back as names the vertex whose RGBAQ the GS kept.
 *
 *   A holds all three corners plus a field of words that are neither ....... the GS interpolates
 *                                                                            per-vertex RGBAQ
 *                                                                            through this exact
 *                                                                            group shape
 *   A is one corner colour, and B is the same one ........................... the flag is not what
 *                                                                            makes the page flat:
 *                                                                            the colours the three
 *                                                                            vertices carry are not
 *                                                                            three distinct words in
 *                                                                            the rasteriser, which is
 *                                                                            a packet question, or
 *                                                                            the XMB never asked --
 *                                                                            read `wide=`/`big=`'s
 *                                                                            three `c=` words next
 *   A is one colour and B is another ......................................... the flag is honoured,
 *                                                                            and the XMB's own
 *                                                                            triangles are not asking
 *                                                                            for it -- `sm=` in the
 *                                                                            [GSTS] row is then the
 *                                                                            answer, and it is an EE
 *                                                                            bug
 *   neither box holds a pixel that is not its canvas .......................... the recorded TRIANGLE
 *                                                                            lays nothing at all, in
 *                                                                            any shading mode, which
 *                                                                            retires every shading
 *                                                                            hypothesis at once
 *
 * It prints nothing between its own draw and the read, for the reason the buffer allocation above
 * gives: a console row repaints the band the control sits in.  A refused group is therefore counted
 * into `s_sh_refused` and reported after the read, not printed where it happened.
 *
 * MEASURED, 2026-10-18 run (`ctrl=1024/1024`, so the read was certified when this printed):
 *
 *   A iip=1 tot=2048 painted=960 red=960 green=0 blue=0 other=0 canvas=1088
 *   B iip=0 tot=2048 painted=945 red=0 green=0 blue=945 other=0 canvas=1103   2 groups, 0 refused
 *
 * So neither reading in the table above is the one that happened, and the pair says something none
 * of them predicted: the flag IS honoured (A and B come back different words, which retires "the
 * tag's PRIM is ignored" outright), the three RGBAQ words DO reach the register (box B paints the
 * KICK vertex's blue, which is only possible if the register held blue at the third XYZ2), the
 * geometry rasterises (960 px of a 2,048 px box is the triangle, to the pixel) -- and yet the shaded
 * box is ONE colour, bit-exactly v0's, with no intermediate word anywhere.  A and B together say the
 * GS reads the whole triangle's colour from one register and picks WHICH register by IIP: the first
 * vertex's when shading is asked for, the last one's when it is not.  That is a flat triangle twice
 * over, and it reproduces the XMB's plate exactly, because the plate's v0 is the word the census
 * found painted over 356,087 px.
 *
 * What this rung cannot decide is whether some OTHER packet shape makes this GS interpolate.  That was
 * rung 12c, and it ANSWERED: the shape ps2sdk's own Gouraud cube sends (`draw3d.c:11-53` +
 * `teapot.c:193-202`: an AD pair carrying PRIM and RGBAQ, then a `FLG=REGLIST`, `PRE=0`, NREG=2 chain
 * of {RGBAQ, XYZ2} pairs -- no ST, no NOP, no PRIM in the tag) lays a gradient on this GS, box C of
 * the run above.  So the recorder changes its group and the plate follows -- no EE-side pre-shading,
 * which was the alternative this rung left open.  What is still undecided is WHICH of C's three
 * differences from A is the one that matters, and that is rung 12d, because the recorder has to change
 * one thing it can say WHY about, not a shape it merely happens to resemble. */
static uint32_t s_sh_groups;
static uint32_t s_sh_refused;

/* ── Hardware GIF Packet Capture & Decoder Verification ───────────────────
 *
 * Captures low-level quadwords emitted to the GIF DMAC channel to verify register
 * layout and primitive tag encoding against hardware expectations.
 *
 * GIF Quadword Packing:
 * - Word 0 (Tag Low): NLOOP (bits 0-14), EOP (bit 15), PRE (bit 46), PRIM (bits 47-57),
 *   FLG (bits 58-59), NREG (bits 60-63).
 * - Word 1 (Tag High): REGS list packing.
 * - Words 2-7: Dense alternating RGBAQ and XYZ2 vertex payloads. */
#define GSLIVE_CAP_WORDS 8u
/* Rung 12i's send dump: 12h's cell G was the recorder's THIRD group of one session -- three transfers
 * sharing a buffer, cut at every PRIM change -- so the census had never seen what the recorder hands the
 * GIF for a triangle of its own, and had never SEPARATED the sender from the frame's order.  Every group
 * of this run's three one-triangle sessions is kept, in the order they leave, so a row can say "this
 * session sent one group and it was this packet" instead of leaving the count inferred.  Box A's group is
 * the one rung 12g compares the hand chain with, and because A now goes LAST its slot is not send 0, so
 * `s_cap_rec_at` names where it landed.
 *
 * Rung 12v moved that count: the recorder now hands its sink TWO groups per triangle -- the AD write that
 * programs PRIM, which is what 12u measured as the only thing that does program it, and then the vertex
 * chain.  Six of them now leave a three-session grid, so the array is six deep and every reader that wants
 * a box's CHAIN -- 12g's tag xor, 12o's vertex decode, 12u's replay -- gets the index of its first
 * REGLIST group, not of its first send.  `s_sh_sess_first[]` therefore changed meaning to that index, and
 * `s_sh_sess_owner[]` was added because the dumps need to attribute the preamble too. */
#define GSLIVE_CAP_SENDS 6u
static uint64_t s_cap_send[GSLIVE_CAP_SENDS][GSLIVE_CAP_WORDS];
static uint32_t s_cap_send_words[GSLIVE_CAP_SENDS];
static uint32_t s_cap_sends;
static uint32_t s_cap_rec_at;
#define s_cap_rec       (s_cap_send[s_cap_rec_at])
#define s_cap_rec_words (s_cap_send_words[s_cap_rec_at])
static uint64_t s_cap_hand[GSLIVE_CAP_WORDS];    /* box A's triangle built by the OTHER sender */
static uint32_t s_cap_hand_words;
static uint32_t s_sh_sess_first[GSLIVE_SH_N];    /* index in s_cap_send[] of this box's session's first VERTEX group */
static uint32_t s_sh_sess_groups[GSLIVE_SH_N];   /* groups this box's session handed its sink (chain + its PRIM write) */
static uint8_t  s_sh_sess_owner[GSLIVE_CAP_SENDS]; /* 1 + the box a captured send came from, 0 = no session of the census */

/* GIFTAG fields, read out of a captured word the way the GIF reads them.  The unit deliberately
 * duplicates `verify/tests/gs_group_view.h`: this row is a claim about what the machine was handed, and
 * reading it with a copy of the decoder rather than with the test's view is what keeps the test suite
 * and the census from agreeing with each other and not with the GS. */
static uint32_t gslive_nloop_of(const uint64_t *g) { return (uint32_t)(g[0] & 0x7FFFu); }
static uint32_t gslive_eop_of(const uint64_t *g) { return (uint32_t)((g[0] >> 15) & 1u); }
static uint32_t gslive_pre_of(const uint64_t *g) { return (uint32_t)((g[0] >> 46) & 1u); }
static uint32_t gslive_prim_of(const uint64_t *g) { return (uint32_t)((g[0] >> 47) & 0x7FFu); }
static uint32_t gslive_flg_of(const uint64_t *g) { return (uint32_t)((g[0] >> 58) & 3u); }
static uint32_t gslive_nreg_of(const uint64_t *g) { return (uint32_t)(g[0] >> 60); }
/* Rung 12o's packet decode: the exact inverse of `gs_vertex_coord16()` in src/drivers/ps2/ps2_gs_pack.c,
 * which writes a vertex axis as `((px * 16) + GS_XYOFFSET_PAGE) & 0xFFFF` into a 16-bit 12.4 field (X bits
 * 0-15, Y bits 16-31 of an XYZ2 quadword).  Subtracting the page back off therefore recovers the pixel the
 * encoder was handed -- valid for any px below GS_POSTER_MAX_PX (2047), where (px+2048)*16 still fits the
 * field, which every coordinate in this census is.  The census env sets win_x = win_y = 0, so XYOFFSET
 * itself is the plain page offset and no window term enters either side of that law.  In a two-register
 * {RGBAQ,XYZ2} group the XYZ2 words are the capture's indices 3, 5 and 7, one vertex per quadword. */
static int32_t gslive_xyz_axis(const uint64_t w, int is_y)
{
    const uint32_t f = (is_y) ? (uint32_t)((w >> 16) & 0xFFFFu) : (uint32_t)(w & 0xFFFFu);

    return (int32_t)(f >> 4) - 2048;
}

static uint32_t s_sh_box;                 /* which box the session now open draws; the sink tags its sends */

/* ── PRIM Register Latching & GIFTAG PRE Semantic Invariant ───────────────
 *
 * The PS2 GS GIFTAG bit 46 (PRE) controls whether the PRIM field embedded within
 * the GIFTAG is latched into the GS's internal `GS_REG_PRIM` register.
 *
 * Hardware Rule:
 * When PRE=1 (GIF_PRE_ENABLE), output of the tag's PRIM field is disabled;
 * the primitive executes using whatever primitive type and shading mode (IIP)
 * was previously latched into the PRIM register.
 *
 * To guarantee that the desired primitive configuration (e.g. TRIANGLE with
 * Gouraud shading IIP=1) is active, an explicit A+D register write to GS_REG_PRIM
 * is dispatched immediately preceding the REGLIST vertex packet. */
static uint32_t s_pre0_box = GSLIVE_SH_N;   /* the box whose tag has PRE cleared; SH_N = no patch */
/* Rung 12m's second dial, and it exists because 12l's negative is CONDITIONAL.  Clearing PRE only
 * changes what a group draws if the PRIM register it would otherwise inherit DIFFERS from the TRI+IIP
 * the tag carries -- and this census never measured that register, so "PRE is not the variable" is only
 * proven against a latch that may already have held the right value.  12m removes the condition instead
 * of measuring the register: box D's tag now also has its PRIM FIELD (bits 47-57) zeroed, which is
 * POINT, flat, untextured, context 1, no FIX.  A tag that asks for points and still lays a solid 88x16
 * block says the field is not what rasterises this group; a tag that asks for points and lays points
 * says it is, and 12l's block was therefore laid by TRI+IIP, which moves the diagnosis to the vertices
 * and the clipping rather than to the primitive type.  One box, and the same 8 words otherwise. */
static uint32_t s_prim_pt_box = GSLIVE_SH_N; /* the box whose tag PRIM field is zeroed; SH_N = no patch */
/* Rung 12n's variable is the GEOMETRY, and it exists because of what 12l and 12m MEASURED together.  Both
 * fields that a GIF tag uses to say what to draw have now been changed on purpose on box D -- PRE (12l)
 * and the whole PRIM field (12m) -- and both times the captured tag proved the patch left the EE, and both
 * times box D laid the same 88x16 block, `top=88/88 painted=1408 RECT nofield`, byte-for-byte unchanged.
 * A measurement that refuses to move when two of its inputs are edited is not a measurement of those
 * inputs, so what has to be doubted is the thing every rung since 12i has assumed without testing: that the
 * block in D's window is D's GROUP's ink.  It may belong to another writer -- the poster, the environment
 * stream, the desktop's own upload -- and if so all of 12i's ORDER/PLACE/SESSION grid and both of 12l/12m's
 * negatives were read off someone else's pixels.
 * The one dial that settles it is the geometry: ask D's triangle 24 px wide at the LEFT of the 88-px window
 * the census reads, change nothing else, and count painted px in that window at an x that was never asked
 * for.  Ink that follows the ask is this group's; a block that holds while the ask moves is not.  So the
 * session records what each box was ASKED beside the window it is READ at, and the two being different is
 * this rung's experiment rather than a coordinate typo.  `0` means never asked -- the hand controls C, E
 * and F are drawn by the shape matrix and have no ask of their own, and the prints fall back to the window
 * for them so a missing ask cannot read as a mismatch. */
static int32_t s_sh_ask_x0[GSLIVE_SH_N], s_sh_ask_x1[GSLIVE_SH_N];
/* Rung 12o adds the other axis to that record, because 12n's answer -- the block held while the ask moved
 * in x -- has exactly two explanations and they are told apart by y.  Either this group's ink does not
 * follow its own vertices at all, or it follows them in one axis and not the other, which is what a
 * per-axis override (a CLAMP, a double-added XYOFFSET, a writer that keeps the y it is given and re-derives
 * x) looks like from a read-back.  So the session records the RECT it was asked for, and 12o asks D a rect
 * entirely below the band: if the block in the band goes away, the y dial bit; if ink also appears in the
 * asked rect, this group did draw.  The two are separate readings, and only the pair of them can say
 * whether the band's block is this group's ink at a wrong x or somebody else's ink entirely. */
static int32_t s_sh_ask_y0[GSLIVE_SH_N], s_sh_ask_y1[GSLIVE_SH_N];
/* Rung 12p's dial is the GIF seam itself, and it exists because 12o gave two answers at once and named
 * nothing.  Moving box D's ask 16 rows down made 384 red px fill the asked 24x16 bbox AND changed the
 * control box B from 12n's `top=128/128 px=1920 RECT` to `top=63/128 px=1073 PARTIAL` -- in all eight
 * censuses of both pinned runs, deterministically.  Neither reading can be attributed from that run: the
 * ink at the moved ask might be this transfer's rasterisation or a foreign writer that happens to paint
 * those rows, and B might be coupled to D's bytes or might be answering the instrument.
 * So this dial edits the one thing 12n and 12o left alone: whether the bytes go out at all.  D's session
 * runs, its group is captured and decoded exactly as 12o certified it, and then the sink returns WITHOUT
 * calling `ps2_gs_gif_send()`.  Every other box is asked, encoded and sent as in 12o, and the transfers the
 * GIF receives are the same ones in the same order.  Two questions with independent answers follow: ink at
 * the moved ask that survives the bytes being withheld is NOT this group's, and a B that returns to 12n's
 * number the moment D's bytes stop arriving is coupled to D's delivery -- which is the mechanism 12l's
 * PRE=1 latch pointed at, and the reason every per-box count here is really a whole-frame count.
 * `GSLIVE_SH_N` (the default, mask 0) hands everything to the GIF, so the ship route is untouched by this
 * file.
 * Rung 12p's answer turned the dial's meaning inside out, so 12q widens it into a mask.  Holding ONLY box
 * D's bytes left D's band block (1408 px) and D's moved ask (384 px) both standing at their 12o numbers,
 * while the one box whose group DID still go out -- B -- emptied completely (`painted=0`, its foreign blue
 * 945 included).  That says the per-box counts are not per-box: withholding one transfer's bytes rewrote a
 * neighbour's picture.  Two explanations survive and they need the opposite experiment.  Either the ink the
 * census counts is almost entirely the app's own XMB stream and no census triangle has ever rasterised --
 * in which case holding every recorder session changes nothing except the timing -- or the band is the
 * census's own ink and each box's count answers the whole frame.  So the dial is a MASK now: 12q holds all
 * three recorder sessions (A, B and D), sends nothing from this file's geometry, and keeps the hand-built
 * shape boxes C/E/F plus the moved ask as witnesses.  If a recorder box's counted ink stands while its own
 * bytes never reached the GIF, that ink was never the census's to lay. */
static uint32_t s_hold_gif_mask;               /* bit per box whose bytes the sink keeps to itself */
static uint32_t s_sh_held;                     /* groups captured and withheld, by box or in total */
/* Rung 12t's dial, the opposite of 12p/12q's: instead of withholding one box's bytes from a census that
 * still sends six things, it lets ONE session reach the GS and withholds everything else -- the magenta
 * control, the other two recorder sessions, and the hand chain C/E/F.  The question it answers is the one
 * 12s left open on a band certified clean: the recorder's group at box D lays 660 FLAT red px as a 44-wide
 * by 15-tall block, while the packet 12i captured as differing from it in exactly three position words and
 * no colour word lays 891 px of a correct apex-up gradient triangle at box A -- and 12j's spill says D's
 * block is really one continuous flat red rect over x384..579, wider than D's own 536..623 ask, reaching
 * through box C's whole window and both 16-px gutters beside it.  Two claims fit that, with opposite cures:
 * the packet alone rasterises flat and mis-shaped, in which case the recorder's own encoding or the GS state
 * it lands in is the bug; or a NEIGHBOUR's transfer drew the block and D only owns the part its window
 * happens to cover, in which case the packet is fine and the frame that carries it is not.  A census that
 * sends seven things cannot tell them apart, because every count in it is a sum of seven.  GSLIVE_SH_N means
 * the dial is off and the census runs 12i's whole grid. */
static uint32_t s_census_only = GSLIVE_SH_N;   /* < GSLIVE_SH_N: only that box's session is sent */
static uint32_t s_t_lone_n;                    /* the LONE census's band ink, kept for the grid to compare */
static uint32_t s_t_lone_field;
static uint32_t s_t_lone_bbox[4] = { GSLIVE_BB_NONE, GSLIVE_BB_NONE,
                                     GSLIVE_BB_NONE, GSLIVE_BB_NONE };
static uint32_t s_t_lone_done;                 /* 1 = a LONE census has been measured in this process */
/* Rung 12u's accumulators: the same lone packet replayed four times, each on a band the upload put back to
 * its canvas, each read before anything prints.  See `ps2_gslive_u_probe()` for what the four modes are and
 * what answer each one buys. */
static uint32_t s_u_ok;                          /* 1 = all four variants went out and came back readable */
static uint32_t s_u_read_ok[4];                  /* per variant: this variant's band read filled its words */
static uint32_t s_u_send[4];                     /* quadwords this variant handed the GIF, dial included */
static uint32_t s_u_n[4];                        /* band px that differ from the canvas */
static uint32_t s_u_corner[4];                   /* ... of those, the three corner words */
static uint32_t s_u_field[4];                    /* ... and the words that are none of them: interpolated px */
static uint32_t s_u_top[4];                      /* painted px in band row 0 INSIDE D's window: the shape key */
static uint32_t s_u_bbox[4][4];                  /* the whole band's ink extent per variant */
/* Rung 12q answered, and its answer re-opens the assumption every rung since 12a has been built on.  With
 * A, B and D all withheld (`to the bus=0`), A and B emptied exactly as their own deliveries predict (891 ->
 * 0 and 1073 -> 0), so those two boxes really are the census's ink -- while D's band block (1408 px) and
 * D's moved rect (384 red) stood at their 12o numbers with NO census byte ever reaching the GS for them.
 * One box is therefore the instrument's and one is somebody else's, and the only way to tell which is
 * which is to look at the page BEFORE this census puts anything on it.  That is what 12a's own note claims
 * -- "rows 560..575 hold nothing but its own control" -- but the census never tested it: `ctrl=1024` says
 * the magenta box came back whole, which says nothing about the other five windows or about the rows below
 * the band.  These hold the pre-draw tally, read once per census before the control is sent.
 *
 * One accumulator type, filled by the read loop's OWN class order -- the three corner words tested before
 * the canvas compare, the canvas compare before the field -- so a number from here and the number in the
 * `shade` row beside it are the same measurement taken at two times and nothing else differs.  `painted` is
 * derived as `tot - same`, exactly as every rung since 12a derives it.  `top` is 12l's shape law on row 0,
 * so a pre-existing block can be called a rect or an apex without waiting for 12k's picture.  `sent` counts
 * words the read never filled: a window with a non-zero `sent` has no answer, which must not be printable
 * as a blank one.  `s_pre_pass` numbers the censuses of THIS process, because the census re-runs on every
 * app open and only the first of them looks at a page this file has never drawn on -- at pass 2 a held block
 * could be this file's own earlier triangle, and the two readings would mean opposite things.
 *
 * Rung 12s adds the last four fields, because 12r's first look was real and its conclusion was still wrong.
 * `painted` cannot see whether the canvas holds the SAME corner word at the SAME position: a corner word is
 * counted before the canvas compare, so desktop chrome drawn in the palette's red or blue is indistinguishable
 * from this census's ink in that column.  `cv_r/cv_g/cv_b` therefore count the CANVAS words in the same window
 * and `match` counts the positions whose two words are equal -- the pixel-by-pixel agreement that says
 * "the page is the canvas here".  A window with corner words in the page and none in the canvas is a real
 * foreign GS writer; one where both hold them and `match == tot` is chrome, and chrome is what 12a..12r were
 * measuring.  `match` is a separate count on purpose, not `same`: `same` is what `painted` subtracts and the
 * two must stay comparable with the post-draw column. */
typedef struct {
    uint32_t tot, same, other, top, sent;
    uint32_t r, g, b;
    uint32_t cv_r, cv_g, cv_b, match;
} gslive_pre_t;

static uint32_t s_pre_pass;                      /* 1 = the first census this process has ever run */
static int s_pre_ok;                             /* the pre-draw read of the band came back GS_READ_BUS_OK */
static int s_pre_old_ok;                         /* the same, for the read of the strip 12a..12r used */
static gslive_pre_t s_pre[GSLIVE_SH_N];          /* the six box windows, before any census send */
static gslive_pre_t s_pre_old[GSLIVE_SH_N];      /* the same six x ranges at rows 560..575, 12s's control */
/* 12r's own control, and the row that decides whether the rung may be believed at all.  The measured
 * pre-draw read (build/repro/20261018T064640Z, census #1 of the process, `read=OK, words never filled: 0`)
 * came back with D's window ALREADY holding red 1024 + blue 384 as a whole-width RECT, D's moved ask already
 * holding 384 red, and C/E already 320/1088 -- while A, B and F came back clean.  That is either the finding
 * 12q forced (the two D windows are another writer's) or a read that is not looking at the page it claims
 * to.  Nothing in the rung so far separates those, so this control asks the read about a window whose ONLY
 * writer is this census and whose draw is known to come after the read: the magenta box.  It answered on the
 * first try (`magenta=0 painted=0` at build/repro/20261018T065254Z): the read IS a first look, and the block
 * is not this census's.  12s then named its author offline from that same run -- the ink stops at x599 and
 * x699, the Colour Test Bar's swatch edges, not at 623 and 719, the census's own box edges.  The control
 * stays, because it is the only thing that tells a first look from a late one. */
static uint32_t s_pre_ctrl_mag, s_pre_ctrl_paint;

static void ps2_gslive_sh_sink(void *user, const uint64_t *qw, uint32_t qwc)
{
    /* The recorder's own buffer is a `uint64_t` array, which the ABI guarantees eight bytes of, and
     * channel 2 reads sixteen.  Every real sender therefore stages a copy first -- `eegs_present.c`
     * does it into an aligned static -- so this sink does the same rather than DMA-ing words that were
     * never aligned to send. */
    static uint64_t s_pkt[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    uint32_t i;

    (void)user;
    if (qwc == 0u || qwc > GSLIVE_SH_QWC_MAX) {
        s_sh_refused++;
        return;
    }
    for (i = 0u; i < 2u * qwc; i++) s_pkt[i] = qw[i];
    /* Rung 12l's one bit, applied BEFORE the capture so every dump in this census describes the bytes
     * the machine was actually handed rather than the bytes the recorder meant to send. */
    if (s_sh_box < GSLIVE_SH_N && s_sh_box == s_pre0_box)
        s_pkt[0] &= ~((uint64_t)1u << 46);
    /* Rung 12m's field, on the same box and for the same reason: with PRE already 0 the tag's own PRIM
     * is the thing that ought to rule, so zeroing its 11 bits (op 0 = POINT, IIP 0, TME 0, USEST 0,
     * CONTEXT 0, FIX 0) is an ask the machine cannot be accused of never receiving.  Everything else in
     * the packet -- NLOOP, EOP, FLG, NREG, the REGS list, all six vertex words -- is left exactly as the
     * recorder built it, and 12g's byte identity of box A's two senders is untouched because A is not
     * patched. */
    if (s_sh_box < GSLIVE_SH_N && s_sh_box == s_prim_pt_box)
        s_pkt[0] &= ~(0x7FFull << 47);
    if (s_cap_sends < GSLIVE_CAP_SENDS) {
        /* Keep EVERY group, in the order it left, rather than the first one.  Rung 12i's question is
         * which group belongs to which session and what went out beside it, and a capture that kept
         * only send 0 could not answer either.  Copy before the send, because the recorder reuses its
         * buffer for the next group the moment this one is handed over. */
        for (i = 0u; i < 2u * qwc && i < GSLIVE_CAP_WORDS; i++)
            s_cap_send[s_cap_sends][i] = s_pkt[i];
        s_cap_send_words[s_cap_sends] = 2u * qwc;
        if (s_sh_box < GSLIVE_SH_N) s_sh_sess_owner[s_cap_sends] = (uint8_t)(s_sh_box + 1u);
        /* Rung 12v: a session now has two groups, and every reader of `s_sh_sess_first[]` wants the
         * chain -- 12g and 12l/12m read its tag, 12o decodes its vertex words, 12u replays it -- so the
         * index names the session's first REGLIST group.  A PACKED group is the AD write of PRIM and
         * never the thing being read; `s_cap_rec_at` is set from the same event, which is what keeps
         * 12g's identity a comparison of two chains rather than of a chain against a preamble. */
        if (s_sh_box < GSLIVE_SH_N && gslive_flg_of(s_pkt) == GS_FLG_REGLIST &&
            s_sh_sess_first[s_sh_box] >= GSLIVE_CAP_SENDS) {
            s_sh_sess_first[s_sh_box] = s_cap_sends;
            if (s_sh_box == 0u) s_cap_rec_at = s_cap_sends;
        }
    }
    if (s_sh_box < GSLIVE_SH_N) s_sh_sess_groups[s_sh_box]++;
    s_cap_sends++;
    /* Rung 12p, widened to a mask by 12q: the bytes stop here for every armed box.  Everything above -- the
     * staged copy, the tag dials, the capture and the per-session bookkeeping -- ran as it always did, so
     * the census still holds the packets it can decode and still knows which group each was; what the GS
     * never gets is the transfer itself.  `s_sh_groups` counts the groups that reached the bus, and
     * `s_sh_held` the ones this dial kept, so a row can say "one went out, two did not" instead of hiding
     * the difference. */
    if (s_sh_box < GSLIVE_SH_N && (s_hold_gif_mask & (1u << s_sh_box)) != 0u) {
        s_sh_held++;
        return;
    }
    ps2_gs_gif_send(s_pkt, qwc);
    s_sh_groups++;
}

/* One triangle, three known colours, at the page position the census will read back.  The vertex
 * order is the XMB's own -- v0 first, the kick on v2 -- because "which vertex wins when the flag is
 * off" is only answerable if the order matches the stream being diagnosed. */
static void ps2_gslive_sh_tri(int32_t x0, int32_t y1, int32_t x1, int32_t apex_x,
                              int32_t y0, uint32_t smooth)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    v[0].x_px = x0;      v[0].y_px = y1;   v[0].rgba = GSQUAD_RED;
    v[1].x_px = x1 - 1;  v[1].y_px = y1;   v[1].rgba = GSQUAD_GREEN;
    v[2].x_px = apex_x;  v[2].y_px = y0;   v[2].rgba = GSQUAD_BLUE;
    v[0].z = v[1].z = v[2].z = 0u;
    v[0].s = v[1].s = v[2].s = 0.0f;
    v[0].t = v[1].t = v[2].t = 0.0f;

    st.smooth = smooth ? 1u : 0u;
    st.textured = 0u;
    st.blended = 0u;
    st.brighten = 0u;              /* named: `st` is a local, and rung 28 gave it a field */
    st.depth_test = 0u;
    eegs_triangle(v, st);
}

/* Rung 12i: the recorder draws ONE triangle per session, and the census puts those sessions at
 * opposite ends of the frame.
 *
 * 12h's grid printed SENDER, but the same run says something else just as cleanly: the recorder's three
 * groups were triangle transfers 1, 2 and 3 and the hand chain's four were 4, 5, 6 and 7, so "the first
 * three triangles of this frame are flat and the next four lay a field" fits every one of the eight
 * counts as well as "the recorder is flat and the hand chain shades".  Nothing in that run separates
 * them, and the two answers have opposite cures -- a sender answer means the recorder's own send is the
 * bug, an order answer means the packet is fine and the frame that carries it is not.
 *
 * This rung separates them by moving each sender across the split while holding one box fixed.  Box A's
 * triangle is now asked LAST, after three hand triangles, which is the position and the box where rung
 * 12h measured the hand chain interpolate (other=890) and the box where the recorder's ask at the front
 * of the frame is measured FLAT four times over -- so A-early against A-late is one encoding, one box,
 * one variable: where in the frame it went out.  Box D's group moves to the FRONT, as the frame's first
 * triangle transfer, where 12h's cell G had it third.  And box A gets one writer again: the hand chain
 * that overdrew it in 12h is built below and never sent, so `painted=` on A's row is the recorder's
 * triangle alone rather than whichever of two senders covered more of the rect.
 *
 * One triangle per session, not three per session: `gs_rec_prim()` closes the open group whenever the
 * PRIM word changes, so A (IIP=1), B (IIP=0) and D (IIP=1) were three groups of one session and every
 * group after the first left on a cut rather than on `gs_rec_end()`.  A session per box makes each group
 * the first and only thing its session ever handed the GIF, and makes every one of them leave the same
 * way. */
static void ps2_gslive_shading_session(uint32_t box, int32_t x0, int32_t x1,
                                       int32_t y0, int32_t y1, uint32_t smooth)
{
    /* Rung 12t: in a LONE census every other box is withheld BEFORE its ask is even recorded, so a skipped
     * box stays `never asked` in 12n's bookkeeping and its `out=` column keeps its zero meaning. */
    if (s_census_only < GSLIVE_SH_N && box != s_census_only) return;

    s_sh_box = box;
    /* Rung 12n: record the span this box was ASKED for, as distinct from the span the census READS, so a
     * narrowed ask is legible to the read loop instead of being inferred from the picture.  One triangle
     * per session (12i's rule), so one record per box per census; `ps2_gslive_shading_reset()` puts every
     * box back to 0 = never asked, which is what makes the hand controls fall back to their window.
     * Rung 12o extends the record to the other axis, because 12n's answer needs the y dial read too. */
    if (box < GSLIVE_SH_N) {
        s_sh_ask_x0[box] = x0;
        s_sh_ask_x1[box] = x1;
        s_sh_ask_y0[box] = y0;
        s_sh_ask_y1[box] = y1;
    }
    /* Rung 12g's identity is box A's chain against the hand chain built at A's coordinates, and A's
     * session is now the frame's LAST, so its slot in the capture array is wherever the count stands
     * when it opens -- not send 0.  Since rung 12v that slot is named in the sink, at the moment A's
     * REGLIST group is copied, because a session's first send is now its PRIM write and the two are
     * different packets with different words. */
    eegs_frame_begin(ps2_gslive_sh_sink, 0);
    ps2_gslive_sh_tri(x0, y1 - 1, x1, (x0 + x1) / 2, y0, smooth);
    eegs_frame_end();
}

static void ps2_gslive_shading_reset(void)
{
    uint32_t b;

    s_sh_groups = 0u;
    s_sh_refused = 0u;
    s_sh_held = 0u;
    s_sh_box = GSLIVE_SH_N;
    s_pre0_box = GSLIVE_SH_N;
    s_prim_pt_box = GSLIVE_SH_N;
    s_hold_gif_mask = 0u;         /* disarmed: the sink hands every group to the GIF */
    for (b = 0u; b < GSLIVE_SH_N; b++) {
        /* 0 = no session has asked anything at this box yet (rung 12n's bookkeeping). */
        s_sh_ask_x0[b] = 0;
        s_sh_ask_x1[b] = 0;
        s_sh_ask_y0[b] = 0;
        s_sh_ask_y1[b] = 0;
    }
    s_cap_sends = 0u;
    /* Rung 12v: the sink names A's chain slot when A's session opens, so the reset value has to be a slot
     * that is valid to read but empty to describe -- the last one.  A LONE census never opens A, and there
     * 12g reads words=0 and prints its own `SKIP -- a packet was not captured`, rather than reading box
     * D's preamble and calling it box A's triangle. */
    s_cap_rec_at = GSLIVE_CAP_SENDS - 1u;
    s_cap_hand_words = 0u;
    for (b = 0u; b < GSLIVE_CAP_SENDS; b++) {
        s_cap_send_words[b] = 0u;
        s_sh_sess_owner[b] = 0u;
    }
    for (b = 0u; b < GSLIVE_SH_N; b++) {
        s_sh_sess_first[b] = GSLIVE_CAP_SENDS;   /* no vertex chain of this box's session captured yet */
        s_sh_sess_groups[b] = 0u;
    }
}

/* ── Gouraud Shading Interpolation & Register List Stride ───────────────────
 *
 * The Graphics Synthesizer Gouraud rasterizer (IIP=1) requires per-vertex color
 * (RGBAQ) to directly precede the vertex coordinate (XYZ2) that triggers the kick.
 *
 * Register Packing Modes:
 * 1. Untextured Gouraud Triangles (NREG=2):
 *    REGS = {GS_REG_RGBAQ, GS_REG_XYZ2}.
 *    Consumes 2 quadwords per vertex pair. Smoothly interpolates RGB channels
 *    across triangle interiors.
 *
 * 2. Textured Gouraud Triangles (NREG=3):
 *    REGS = {GS_REG_RGBAQ, GS_REG_ST, GS_REG_XYZ2}.
 *    Consumes 3 quadwords per vertex triplet (with quadword alignment padding).
 *    Enables simultaneous Gouraud color modulation and perspective-correct texture mapping.
 *
 * Invariants:
 * - A 4-register format {PRIM, RGBAQ, ST, XYZ2} with per-loop PRIM kicks degenerate
 *   flat primitives on each vertex.
 * - Flat shading (IIP=0) collapses triangle interior to the color of the final vertex (v2). */
static uint32_t s_shape_sent;
static uint32_t s_shape_refused;

static void ps2_gslive_corners(const int32_t v[6], uint64_t rgba[3], uint64_t xyz[3])
{
    static const uint32_t c[3] = { GSQUAD_RED, GSQUAD_GREEN, GSQUAD_BLUE };
    int i;

    for (i = 0; i < 3; i++) {
        /* The same four bytes `emit_vertex()` hands `gs_rec_color()`: the corner colours are written
         * as the page holds them, so a pixel that comes back equal to GSQUAD_RED is red by the read's
         * own unit and not by a swap this function would have had to make twice. */
        rgba[i] = gs_pack_rgbaq(c[i] & 0xFFu, (c[i] >> 8) & 0xFFu,
                                (c[i] >> 16) & 0xFFu, (c[i] >> 24) & 0xFFu, GS_Q_ONE);
        xyz[i] = gs_pack_xyz2(gs_vertex_coord16(v[2 * i]),
                              gs_vertex_coord16(v[2 * i + 1]), 0u);
    }
}

static uint32_t ps2_gslive_shape_packet(int shape, const int32_t v[6], uint64_t *qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_TRI, 1u, 0u, 0u, 0u, 0u);
    const uint64_t st = gs_pack_st(0.0f, 0.0f);
    const uint32_t chain2[2] = { GS_NIB_RGBAQ, GS_NIB_XYZ2 };
    const uint32_t chain3[3] = { GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2 };
    const uint32_t pre = (uint32_t)((shape == 1 || shape == 3) ? 1u : 0u);
    uint64_t rgba[3], xyz[3];
    uint32_t n = 0u;
    int i;

    ps2_gslive_corners(v, rgba, xyz);

    /* C's AD group, which shape 2 repeats and shapes 1 and 3 withhold: draw3d.c:11-31 writes PRIM and
     * the first RGBAQ as AD pairs under one PACKED tag whose EOP is CLEAR, because the chain that
     * follows is the part that ends the transfer. */
    if (shape == 0 || shape == 2) {
        const uint32_t ad[1] = { GS_NIB_AD };

        qw[n++] = gs_pack_giftag(2u, 0u, 0u, 0u, GS_FLG_PACKED, 1u);
        qw[n++] = gs_pack_regs(ad, 1u);
        qw[n++] = prim;                qw[n++] = GS_GP_PRIM;
        qw[n++] = rgba[0];             qw[n++] = GS_GP_RGBAQ;
    }

    /* One difference between 0 and 1, and between 2 and 3: where PRIM comes from.  With `pre` set the
     * tag's own 11-bit PRIM field programs the register at packet start, which is how this port's
     * recorder delivers it, so those two chains ride the recorder's tag. */
    if (shape == 0 || shape == 1) {
        qw[n++] = gs_pack_giftag(3u, 1u, pre, pre ? prim : 0u, GS_FLG_REGLIST, 2u);
        qw[n++] = gs_pack_regs(chain2, 2u);
        for (i = 0; i < 3; i++) { qw[n++] = rgba[i]; qw[n++] = xyz[i]; }
    } else {
        /* Three registers to a loop, which is one and a half quadwords, so the nine data words stop
         * half way through a quadword and the GIF's transfer unit does not.  texture.c:259-263 appends
         * `if ((u32)dw % 16) *dw++ = 0;` for exactly that -- the pad lands in a fourth slot that no
         * nibble of a three-nibble REGS list names, so nothing reads it -- and draw2d.c:409 uses the
         * same idiom for the same reason. */
        qw[n++] = gs_pack_giftag(3u, 1u, pre, pre ? prim : 0u, GS_FLG_REGLIST, 3u);
        qw[n++] = gs_pack_regs(chain3, 3u);
        for (i = 0; i < 3; i++) { qw[n++] = rgba[i]; qw[n++] = st; qw[n++] = xyz[i]; }
        qw[n++] = 0u;
    }
    return n / 2u;
}

static void ps2_gslive_shape_tri(int shape, int x0, int x1)
{
    static uint64_t s_qw[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    int32_t v[6];

    if (s_census_only < GSLIVE_SH_N) return;   /* rung 12t: a LONE census sends one group, this is not it */

    v[0] = x0;             v[1] = GSLIVE_SH_Y1 - 1;
    v[2] = x1 - 1;         v[3] = GSLIVE_SH_Y1 - 1;
    v[4] = (x0 + x1) / 2;  v[5] = GSLIVE_SH_Y0;

    {
        const uint32_t q = ps2_gslive_shape_packet(shape, v, s_qw);

        if (q == 0u || q > GSLIVE_SH_QWC_MAX) {
            s_shape_refused++;
            return;
        }
        ps2_gs_gif_send(s_qw, q);
        s_shape_sent++;
    }
}

static void ps2_gslive_shape_matrix(void)
{
    s_shape_sent = 0u;
    s_shape_refused = 0u;

    /* The three controls, at the coordinates that measured them: shape 0 is box C (ps2sdk's AD pair plus
     * a PRE=0 two-register chain), shape 2 is box E (the same with an ST slot), shape 3 is box F (the
     * recorder's own tag with the three-register chain).  These are the frame's triangle transfers 3, 4
     * and 5 -- in front of them go the recorder's D and B sessions, behind them the recorder's A session,
     * and box A's hand chain is built by `ps2_gslive_shape_hold()` instead of drawn, because rung 12h's
     * overdraw of A (other=890) made a two-writer rect of the one box whose early/late pair decides this
     * rung. */
    ps2_gslive_shape_tri(0, GSLIVE_SH_C_X0, GSLIVE_SH_C_X1);
    ps2_gslive_shape_tri(2, GSLIVE_SH_E_X0, GSLIVE_SH_E_X1);
    ps2_gslive_shape_tri(3, GSLIVE_SH_F_X0, GSLIVE_SH_F_X1);
}

/* Rung 12i: build box A's triangle the way the hand builder builds it, and DO NOT SEND IT.
 *
 * Rung 12g's claim is an identity between two packets -- the recorder's group for box A and this chain --
 * and the census has to keep printing both of them to hold that claim open on every run.  12h proved the
 * identity by drawing the hand chain over box A as well, which put two writers in one rect: its read
 * describes whichever one covered more pixels, and the recorder's answer there is not separable from it.
 * 12i needs box A as a single-writer rect -- A early against A late is the rung's whole measurement -- so
 * the hand chain stops being drawn and starts being captured.  Its eight words go to `s_cap_hand` exactly
 * as 12h sent them, and the row that compares them with the recorder's captured A group is unchanged: a
 * gradient at A now belongs to one sender and so does a flat colour. */
static void ps2_gslive_shape_hold(int shape, int x0, int x1)
{
    static uint64_t s_qw[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    int32_t v[6];
    uint32_t c, q;

    v[0] = x0;             v[1] = GSLIVE_SH_Y1 - 1;
    v[2] = x1 - 1;         v[3] = GSLIVE_SH_Y1 - 1;
    v[4] = (x0 + x1) / 2;  v[5] = GSLIVE_SH_Y0;

    q = ps2_gslive_shape_packet(shape, v, s_qw);
    if (q == 0u || q > GSLIVE_SH_QWC_MAX) {
        s_shape_refused++;
        return;
    }
    for (c = 0u; c < 2u * q && c < GSLIVE_CAP_WORDS; c++) s_cap_hand[c] = s_qw[c];
    s_cap_hand_words = 2u * q;
}

/* ── Primitive State Programming Verification (Tag vs A+D Write) ────────────
 *
 * Empirical verification of primitive opcode (OP) and interpolation (IIP) latching:
 * - V0: Baseline REGLIST packet with PRE=1.
 * - V1: REGLIST packet with PRE=0 (tag PRIM field enabled).
 * - V2: Explicit A+D register write to GS_REG_PRIM (TRIANGLE, IIP=1) followed by REGLIST.
 * - V3: Explicit A+D register write to GS_REG_PRIM with flat shading (IIP=0).
 *
 * Confirms that explicit A+D writes to GS_REG_PRIM reliably configure the rasterizer
 * pipeline independently of GIFTAG PRE flags. */
static void ps2_gslive_u_band(uint32_t *buf, const uint32_t *canvas, uint32_t width, uint32_t slot)
{
    uint32_t x, r;

    s_u_n[slot] = 0u;
    s_u_corner[slot] = 0u;
    s_u_field[slot] = 0u;
    s_u_top[slot] = 0u;
    for (x = 0u; x < 4u; x++) s_u_bbox[slot][x] = GSLIVE_BB_NONE;

    for (r = 0u; r < (uint32_t)GSLIVE_PIC_ROWS; r++) {
        const uint32_t *row = buf + (size_t)r * width;
        const uint32_t *crow = canvas + (uint32_t)(GSLIVE_SH_Y0 + r) * width;

        for (x = 0u; x < width; x++) {
            /* 12s's class order, kept: a sentinel means the read did not land, and the canvas compare comes
             * BEFORE the corner-word test so desktop chrome in the palette's red is never counted as ink. */
            if (row[x] == GSREAD_SENT || row[x] == crow[x]) continue;
            s_u_n[slot]++;
            gslive_bbox(s_u_bbox[slot], x, (uint32_t)GSLIVE_SH_Y0 + r);
            if (row[x] == GSQUAD_RED || row[x] == GSQUAD_GREEN || row[x] == GSQUAD_BLUE) s_u_corner[slot]++;
            else s_u_field[slot]++;
            if (r == 0u && x >= (uint32_t)GSLIVE_SH_D_X0 && x < (uint32_t)GSLIVE_SH_D_X1) s_u_top[slot]++;
        }
    }
}

/* Hand D's captured group to the GIF with this variant's dial on it, and return the quadwords sent for it
 * (the AD preamble is counted in `s_u_send` because it is part of what this variant put on the bus).
 *
 * Rung 12v moved the census this replay reads from: the recorder now writes PRIM in front of every chain,
 * so by the time the probe runs the register holds the triangle the frame drew, and a bare replay would
 * draw a triangle on the strength of that write rather than on the strength of nothing.  V0 therefore gets
 * its own neutralising write -- PRIM=POINT, an OP that cannot make a triangle from three kicks -- so that
 * "what does the chain do with no ask of its own" stays a question with an answer.  V1 needs no preamble:
 * clearing PRE lets its own tag write PRIM, which is the dial. */
static uint32_t ps2_gslive_u_variant(uint32_t mode, const uint64_t *g, uint32_t words)
{
    static uint64_t s_qw[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    static uint64_t s_ad[4] __attribute__((aligned(16)));
    const uint32_t ad[1] = { GS_NIB_AD };
    uint32_t i;

    if (mode == 0u || mode >= 2u) {
        /* NLOOP=1, EOP=1, PACKED, PRE=0, one AD slot: the value lands in PRIM and the transfer ends.  Same
         * four words ps2_gslive_shape_packet() builds for box C, with EOP raised because nothing follows. */
        s_ad[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
        s_ad[1] = gs_pack_regs(ad, 1u);
        s_ad[2] = (mode == 0u) ? gs_pack_prim(GS_OP_POINT, 0u, 0u, 0u, 0u, 0u)
                               : gs_pack_prim(GS_OP_TRI, (mode == 2u) ? 1u : 0u, 0u, 0u, 0u, 0u);
        s_ad[3] = GS_GP_PRIM;
        ps2_gs_gif_send(s_ad, 2u);
    }
    for (i = 0u; i < words; i++) s_qw[i] = g[i];
    if (mode == 1u) s_qw[0] &= ~((uint64_t)1u << 46);   /* PRE=0: let the tag's own field rule */
    ps2_gs_gif_send(s_qw, words / 2u);
    return (mode == 1u) ? (words / 2u) : (words / 2u) + 2u;
}

static void ps2_gslive_u_probe(uint32_t *buf, const uint32_t *canvas, uint32_t width)
{
    uint32_t slot, words, v, i;

    s_u_ok = 0u;
    /* Only the LONE census is replayable: in 12i's grid `s_cap_send` holds six recorder groups -- two per
     * session since 12v -- and the chain at D's session index would be drawn beside five others, which is
     * the state this rung exists to exclude. */
    if (s_census_only != 3u) return;
    if (s_sh_sess_first[3u] >= GSLIVE_CAP_SENDS) return;
    slot = s_sh_sess_first[3u];
    words = s_cap_send_words[slot];
    /* Rung 12v: the index above is a vertex chain, not the session's first send, and this is the replay
     * that has to say what a chain does on its own -- V0 is "these eight words and nothing in front of
     * them".  A group with no vertices in it would make that sentence false, so the gate now asks the
     * chain's own two questions: its tag says REGLIST, and it is long enough to hold three vertices.
     * Four halves is what box D's AD write of PRIM measures, and replaying that would report "no ink"
     * for all four variants and read as a verdict about the GS. */
    if (words < 8u || words > GSLIVE_CAP_WORDS || (words & 1u) != 0u) return;
    if (gslive_flg_of(s_cap_send[slot]) != GS_FLG_REGLIST) return;

    for (v = 0u; v < 4u; v++) {
        /* The order is part of the instrument: V0 goes out FIRST so no later variant's own PRIM write can be
         * what it draws under, and V1/V2/V3 each program PRIM before their kick, so nothing they lay is
         * inherited.  A probe that ran V3 then V0 would report "the group lays a triangle alone" for V0 on
         * the strength of V3's write. */
        s_u_read_ok[v] = 0u;
        ps2_gs_upload(0, GSLIVE_SH_Y0, PS2_SCREEN_WIDTH, GSLIVE_PIC_ROWS);
        (void)ps2_gif_drain();
        s_u_send[v] = ps2_gslive_u_variant(v, s_cap_send[slot], words);
        (void)ps2_gif_drain();
        for (i = 0u; i < width * (uint32_t)GSLIVE_PIC_ROWS; i++) buf[i] = GSREAD_SENT;
        (void)ps2_gs_vram_read(0, GSLIVE_SH_Y0, (int)width, GSLIVE_PIC_ROWS, buf);
        if (ps2_gs_vram_read_err() != GS_READ_BUS_OK) return;
        s_u_read_ok[v] = 1u;
        ps2_gslive_u_band(buf, canvas, width, v);
    }
    /* Leave the strip as this census found it, so the next census's own 12s pre-draw control still means
     * "nothing of mine is on this band" rather than being made true by a leftover triangle. */
    ps2_gs_upload(0, GSLIVE_SH_Y0, PS2_SCREEN_WIDTH, GSLIVE_PIC_ROWS);
    s_u_ok = 1u;
}

/* The six boxes, in the order they go out on the bus (A last), and what each one holds since rung 12i.
 * The label is what the row prints, and since 12i it has to name THREE things -- the encoding, the place
 * and the position in the frame -- because 12h proved that a row naming only two of them can be read two
 * ways: its grid said SENDER and said "the first three transfers of the frame" with the same eight
 * counts.  C, E and F keep the CONTROL tag: they are the shapes that measured as interpolating, and if
 * one of them comes back flat the READ has stopped working and the whole table is void.  A and D are this
 * rung's cells, both the recorder's, both alone in a session of their own: A is asked LAST, behind three
 * hand triangles, at the box where 12h measured the recorder flat at the front of the frame, so A's pair
 * is the frame's order with the box, the packet and the sender held still; D is asked FIRST, as the
 * frame's first triangle transfer, where 12h's cell G had the same group third in one session.  B is
 * unchanged from 12a and still names which vertex wins with IIP off.
 *
 * Since rung 12l that sentence needed one more fact, and 12l then measured it: 12k's pictures graded D and
 * B as RECTS and A, C, E, F as triangles, in the order D, B, C, E, F, A went out, and the hypothesis that
 * fit was that D's and B's PRE=1 tags left them drawing under the PRIM the last PRE=0 tag wrote -- in this
 * census, the magenta SPRITE poster.  12l cleared PRE on D's tag and on no other, and the machine said no:
 * D's tag left as PRE=0 PRIM=0x0b (the A-to-D tag xor is bit 46 alone), and D laid the same 88x16 block,
 * 1408 painted px, no field.  So PRE is not the variable -- CONDITIONALLY, which is the finding that
 * matters for the next rung: clearing PRE only bites if the inherited PRIM register differs from the
 * TRI+IIP the tag carries, and nothing here ever read that register.  12m therefore keeps PRE=0 on D and
 * zeroes D's tag PRIM FIELD as well, so the one authoritative ask in the frame is POINT; box B stays
 * unpatched, and what B does is no longer the question -- B is the control that says the census has not
 * moved.
 *
 * 12m answered that one too, and the two negatives together are the finding: the tag's PRE bit did not move
 * D's ink, and the tag's PRIM field did not move D's ink either, both times with the captured tag proving
 * the patch left the EE.  Two of the three things a tag can say about a transfer have now been edited on
 * purpose with the same 88x16 block, 1408 px, as the answer -- so the assumption carrying all of it, that
 * the block is box D's GROUP's ink, is the one thing left untested.  Rung 12n tests it by the one input no
 * rung has moved since 12h: the geometry.  D is asked 24 px wide at the left edge of its own window and
 * read across the whole 88 px, so the ask and the window differ by coordinate, and painted px can be
 * counted outside what was asked for.  Both tag dials go off for it, which is what makes D's row comparable
 * to 12k's unpatched baseline (top=88/88 painted=1408) rather than to a patched one.
 *
 * 12n answered, and it answered in the same 8 censuses as 12m: asked 24 px, D still laid
 * `top=88/88 px=1408 out=1024 RECT` -- the identical red x536-599 plus blue x600-623 block, and 1024 of its
 * painted px at an x never asked for.  The ask is therefore not the variable either: PRE did not move D's
 * ink, the PRIM field did not, and neither did the geometry.  Three inputs a group can hand the GIF have now
 * been edited on purpose with one unchanging answer, so rung 12o stops dialling the band and asks where
 * THIS group's ink goes at all: D's ask moves OUT of the band into the empty rows below it, inside the
 * spill window, and the census reads both places -- the band, to see whether the block survives the group
 * leaving it, and the moved rect, to see whether anything lands where the packet asked.
 *
 * 12o answered, in all eight censuses of both pinned runs and identically in every one: the packet is the
 * ask (`v0=(536,591) v1=(559,591) v2=(548,576) -> PACKET == ASK`), 384 red px fill the asked 24x16 bbox at
 * x536-559 y576-591 (the whole rect, not the 192 px a triangle would cover, and not one green px anywhere),
 * and the band's block is unchanged at 1408 px with out=1408, so 12n's THE BLOCK HELD was right and that ink
 * is a foreign writer's.  It also moved the control: box B went 12n's `top=128/128 px=1920 RECT` to
 * `top=63/128 px=1073 PARTIAL`, which is why 12o's own verdict is CONTROL MOVED and nothing above can be
 * attributed until that movement has a cause.  Rung 12p supplies the one input none of 12n/12o moved --
 * whether the bytes go out at all -- by withholding D's captured group at the GIF seam and reading the
 * same two answers off the same page.
 *
 * 12p answered NEITHER, and answered with a third fact: with D's bytes withheld, D's band block stood at
 * 1408 px and D's moved rect stood at 384 red px (so neither picture is this transfer's in any causal
 * sense), while box B -- whose own group DID go out -- read `painted=0` and lost its foreign blue rect with
 * it.  One withheld transfer emptied a neighbour, so no per-box number on this page can be read as that
 * box's raster.  Rung 12q therefore holds the dial as a MASK over all three recorder sessions (A, B and D)
 * and asks whether ANY of the census's counted ink depends on the census's own bytes: if every recorder
 * window stands at its 12o number with nothing sent, rung 12 has been reading the app's stream since the
 * beginning, and the instrument -- not the GS -- is what needs fixing next. */
static const uint32_t s_sh_x0[GSLIVE_SH_N] = { GSLIVE_SH_A_X0, GSLIVE_SH_B_X0, GSLIVE_SH_C_X0,
                                               GSLIVE_SH_D_X0, GSLIVE_SH_E_X0, GSLIVE_SH_F_X0 };
static const uint32_t s_sh_x1[GSLIVE_SH_N] = { GSLIVE_SH_A_X1, GSLIVE_SH_B_X1, GSLIVE_SH_C_X1,
                                               GSLIVE_SH_D_X1, GSLIVE_SH_E_X1, GSLIVE_SH_F_X1 };
static const char *const s_sh_label[GSLIVE_SH_N] = {
    "A recorder NREG2 chain, LAST, SENT (12r)",
    "B recorder PRE1 NREG2 IIP0, SENT (12r)",
    "C CONTROL 12c winner: AD(PRIM,RGBAQ) + PRE0 NREG2 {RGBAQ,XYZ2}",
    "D recorder NREG2 chain, FIRST, whole window (12s)",
    "E CONTROL AD group + PRE0 NREG3 {RGBAQ,ST,XYZ2} + pad",
    "F CONTROL no AD group, PRE1 NREG3 {RGBAQ,ST,XYZ2} + pad (recorder tag)"
};
/* The shape word both 12l's and 12m's rows print, kept in one table because the two rungs grade the same
 * top-row law and a second copy of the words would let them drift apart.  `PARTIAL` is the honest middle:
 * a top row that is neither one pixel nor edge-to-edge is neither shape, and 12k's pictures show what that
 * is -- C and E lay a solid rect AND a gradient triangle over the same box. */
static const char *const gslive_sh_kind[3] = { "TRIANGLE", "PARTIAL", "RECT" };

/* Rung 12r: tally one rectangle of a read band with the census's own class order -- corner words first,
 * then the canvas compare, then the field -- so the pre-draw picture and the post-draw picture differ ONLY
 * in when they were taken.  It writes its own accumulator and never touches `sh_*`, `mv_*` or the gspeak
 * grids: those hold this census's answer, and a shared accumulator would make the two readings one number.
 * `buf` is the band this read filled, whose row 0 is absolute `y0`; `canvas` is the desktop canvas the census
 * compares against, indexed by absolute row, so both have to be handed the same `width`.
 *
 * Rung 12s adds the four fields on the right, and they are the reason the class order above is now known to
 * be a liability rather than a convention: a corner word is counted before the canvas compare, so a page
 * pixel that is the desktop's own palette red is indistinguishable, in `r`/`b`/`painted`, from this census's
 * ink.  `cv_r/cv_g/cv_b` count the canvas's words by the same three constants, at the same positions, and
 * `match` counts positions where the page word equals the canvas word whatever they are -- the agreement
 * that separates "chrome uploaded through the same buffer" from "a GS-side writer this file does not own". */
static void ps2_gslive_pre_rect(gslive_pre_t *acc, const uint32_t *buf, uint32_t width,
                                const uint32_t *canvas, uint32_t x0, uint32_t x1,
                                uint32_t y0, uint32_t rows)
{
    uint32_t rr, x;

    acc->tot = acc->same = acc->other = acc->top = acc->sent = 0u;
    acc->r = acc->g = acc->b = 0u;
    acc->cv_r = acc->cv_g = acc->cv_b = acc->match = 0u;
    for (rr = 0u; rr < rows; rr++) {
        const uint32_t *row = buf + (size_t)rr * width;
        const uint32_t *crow = canvas + (size_t)(y0 + rr) * width;

        for (x = x0; x < x1; x++) {
            const uint32_t v = row[x];
            const uint32_t c = crow[x];

            if (v == c) acc->match++;                       /* before the class order: pure agreement */
            if (c == GSQUAD_RED)        acc->cv_r++;
            else if (c == GSQUAD_GREEN) acc->cv_g++;
            else if (c == GSQUAD_BLUE)  acc->cv_b++;
            if (v == GSREAD_SENT) { acc->sent++; continue; }
            acc->tot++;
            if (v == GSQUAD_RED)        { acc->r++;     if (rr == 0u) acc->top++; }
            else if (v == GSQUAD_GREEN) { acc->g++;     if (rr == 0u) acc->top++; }
            else if (v == GSQUAD_BLUE)  { acc->b++;     if (rr == 0u) acc->top++; }
            else if (v == c)              acc->same++;
            else                        { acc->other++; if (rr == 0u) acc->top++; }
        }
    }
}

static void ps2_bench_gs_live_census(uint32_t frame)
{
    static uint64_t s_pkt[2u * GS_POSTER_QWC(1u)] __attribute__((aligned(16)));
    gs_poster_rect r;
    gs_draw_env env;
    const uint32_t *canvas = ps2_gs_get_framebuffer();
    const WND *w = ps2_bench_wnd_named("Cross Media Bar");
    const uint32_t width = (uint32_t)PS2_SCREEN_WIDTH;
    const uint32_t height = (uint32_t)PS2_SCREEN_HEIGHT;
    uint32_t *buf;
    uint32_t ink_v[GSLIVE_TOP], ink_n[GSLIVE_TOP], ink_in[GSLIVE_TOP];
    uint32_t read_px = 0u, sent = 0u, same = 0u, win_px = 0u;
    uint32_t diff_all = 0u, diff_console = 0u, diff_ink = 0u, diff_win = 0u;
    uint32_t ctrl = 0u, box_px = 0u, rare = 0u, bins = 0u, qw_sent = 0u, us;
    /* Rung 12a's and 12d's tallies, one slot per box -- see `s_sh_label[]` for which box is which
     * shape.  A box is `tot` px of which `same` are the canvas the desktop put there, the three corner
     * colours are counted where they land whole, and `other` is everything painted that is not one of
     * the three -- which is the interpolated field, and the only reading that says a shape shaded. */
    uint32_t sh_tot[GSLIVE_SH_N];
    uint32_t sh_r[GSLIVE_SH_N], sh_g[GSLIVE_SH_N], sh_b[GSLIVE_SH_N];
    uint32_t sh_other[GSLIVE_SH_N], sh_same[GSLIVE_SH_N], sh_px = 0u;
    /* Rung 12j's two extra reads, on the same walk: `sh_bbox[b][c]` is where colour c of box b's own
     * corners actually is, as {minx, maxx, miny, maxy} with an inclusive max, and the spill arrays are the
     * same three colours found OUTSIDE every box inside GSLIVE_SP_Y0..Y1.  A box of count 0 keeps its
     * bbox at the impossible 0xFFFFFFFF so the print can say `none` instead of inventing coordinates. */
    uint32_t sh_bbox[GSLIVE_SH_N][3][4];
    uint32_t sp_n[3], sp_bbox[3][4], sp_same = 0u;
    /* Rung 12l's shape tally: painted px in the band's FIRST row and in its LAST row.  A rect covers the
     * whole width of the row its top edge sits on; an apex-up triangle covers one or two pixels of it.
     * That is the only shape law that also grades a FLAT triangle, which is what a fixed box B is
     * supposed to become -- the `other=` field test cannot see that one, because flat means no field. */
    uint32_t sh_top[GSLIVE_SH_N], sh_bot[GSLIVE_SH_N];
    /* Rung 12n's attribution tally: painted px in a box's READ window that sit outside the span that box
     * was ASKED to draw.  With the ask equal to the window it is identically zero, which is what makes it
     * a measurement rather than an argument -- the only way it grows is the geometry dial. */
    uint32_t sh_outside[GSLIVE_SH_N];
    /* Rung 12t(iii): the band's ink counted over the FULL width and in every non-canvas class at once.  The
     * per-box tallies cannot answer 12t's question, because they attribute by coordinate: a block that
     * starts on box B's right edge, crosses box C's whole window and both 16-px gutters and ends on D's apex
     * gets split between 12j's spill count, C's `painted` and D's `painted`, and that split is exactly what
     * hides the fact that it is ONE rectangle.  These counters make no attribution: `bp_n` is every band
     * pixel that disagrees with the canvas under it, `bp_corner`/`bp_field` split it by the same corner-word
     * law the boxes use, and `bp_bbox` is their extent.  The magenta control's rect is not in here -- the
     * walk excludes it by coordinate before this branch is reached -- and rows outside the band are not in
     * here either, so `bp_bbox` is a statement about the strip the six boxes live in. */
    uint32_t bp_n = 0u, bp_corner = 0u, bp_field = 0u, bp_bbox[4];
    /* Rung 12o's read of a MOVED ask.  12n moved D's ask within the band and the band block did not move
     * with it; this rung moves the ask OUT of the band, into rows the box branch never reads, and so needs a
     * second place to look.  These counters therefore classify, in the spill window, exactly the pixels that
     * sit inside a session's asked rect while that rect lies outside the band -- the only ink this census can
     * attribute to the dialled group by coordinate alone.  `mv_box` names which box owns the rect, or stays
     * GSLIVE_SH_N when nothing was asked outside the band, which is what lets the print say SKIP instead of
     * reading an unasked rect as an answer.  A pixel claimed here is NOT added to 12j's off-box aggregate:
     * the two rows must describe different regions or 12j's bbox becomes a merge of an unknown writer and
     * this rung's named one. */
    uint32_t mv_box = GSLIVE_SH_N;
    uint32_t mv_tot = 0u, mv_same = 0u, mv_other = 0u, mv_painted = 0u;
    uint32_t mv_r = 0u, mv_g = 0u, mv_b = 0u, mv_bbox[4];
    /* Rung 6b -- SURVIVAL, added because rung 6 and the operator disagree.  Rung 6 answers "did the
     * stream's ink arrive"; the complaint is about a window that is empty when it is looked at, and
     * those are different questions -- ink that arrives and is then re-laid over by the next present
     * reads as a full client rect at the instant of the census and shows nothing one frame later.
     * The two reads this run already made differ that way (in_win=383951 at one census, 287731 at
     * the next, with `same` rising by the difference), and `xmb surface ... black=384000 other=0`
     * says what the replacement value is: the app's CPU canvas, which the eegs path never paints.
     * So every census keeps the per-row ink count of the window's client rect and compares it with
     * the previous census's.  A per-pixel mask would be exact and cost 60 kB of a pool this image
     * has already spent down to a 350 kB hole; a per-row count costs 4.8 kB and answers the only
     * question asked of it -- did the ink stay, and in which rows did it leave. */
    static uint32_t s_live_prev_ink[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_cur_ink[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_cur_same[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_census_no;
    static int s_live_prev_valid;
    /* Rung 6c -- WHO put the canvas back, and in which rows.  The per-row ink delta says where the
     * eraser reached; the cover ring above (file statics, recorded at the send) says what went out.
     * This is the per-client-row px those two agree on, and it is a row count rather than a rect
     * list because the comparison the verdict needs is "did every lost row have an upload". */
    static uint32_t s_live_cover_row[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_nostream_row[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_cover_since;
    uint32_t ink_now = 0u, same_now = 0u, ink_then = 0u, erased = 0u, laid = 0u;
    uint32_t lost_rows = 0u, lost_first = 0u, lost_last = 0u;
    uint32_t lost_rows_covered = 0u, cover_px = 0u, cover_rects = 0u, cover_missed = 0u;
    /* Rung 20w's half of the ring: the pass's own canvas uploads, and how many of them went out on a
     * pass with no stream behind them.  The clip in ps2_upload_canvas() is what makes the second
     * number zero, and the row is written so that it says so rather than being inferred from a hole
     * that the cover list does explain. */
    uint32_t canvas_rects = 0u, canvas_px = 0u, nostream_rects = 0u, lost_rows_nostream = 0u;
    /* Rung 20x: the same number split by the condition that let the upload out, because "the cut is
     * not holding" is three different bugs -- a stream that staged nothing, a layer whose rect the
     * window manager could not name, and a pass with no layer to protect (which is correct and must
     * not read as a failure). */
    uint32_t ns_why[8];   /* PS2_WHY_*; room for rung 20y's PLATED, which is never counted here
                           * because a plated upload always has a stream behind it */
    /* The geometry of the uploads in this census's own window, kept because a rect is what names its
     * producer: `desktop.c`'s deskbar rows and panel rows, `workbench.c`'s open menus, `tip_ife.c`'s
     * popup and the cursor's own box are all cover-adders, and they are told apart by where their
     * rect is -- not by the count. */
    int hc_rect[PS2_HIST_RECTS][4];
    uint32_t hc_pass[PS2_HIST_RECTS], hc_n = 0u;
    uint8_t hc_kind[PS2_HIST_RECTS];
    uint8_t hc_why[PS2_HIST_RECTS];
    /* Rung 6c's other half: the colour the eraser LEAVES.  A hole filled with the app's CPU pixmap
     * (black), with the console's navy, with the desktop's own plate and with a stale wallpaper are
     * four different authors, and the per-row delta cannot tell them apart -- it only knows the page
     * came back to the canvas.  So the client pixels that equal the canvas get the same top-N
     * treatment the ink does. */
    uint32_t cv_v[GSLIVE_TOP], cv_n[GSLIVE_TOP], cv_bins = 0u, cv_rare = 0u;
    int cv_box[GSLIVE_TOP][4];
    int cv_printed[GSLIVE_TOP];
    int have_prev;
    int all_box[4] = { -1, -1, -1, -1 };
    int win_box[4] = { -1, -1, -1, -1 };
    int ink_box[GSLIVE_TOP][4];
    int ctrl_box[4] = { -1, -1, -1, -1 };
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
    uint32_t i, y_start, k, best, col, pr;
    int printed[GSLIVE_TOP];
    int32_t got, qw;
    gs_read_bus_err err;

#if BTRON_PS2_BENCH_GS >= 5
    /* Rung 27, half 1: the six boxes as this census FOUND the page -- before its strip upload, its
     * magenta control, its six shading groups and its own page read, which are the four things this
     * file could otherwise be accused of.  Silent on purpose: the note above `buf` explains that a
     * print here would repaint the console band between this census and the page it grades. */
    if (w && w->gl_layer) ps2_bench_ink_at_census(&w->client);
#endif

    for (k = 0u; k < GSLIVE_TOP; k++) {
        ink_v[k] = 0u; ink_n[k] = 0u; ink_in[k] = 0u;
        ink_box[k][0] = ink_box[k][1] = ink_box[k][2] = ink_box[k][3] = -1;
        printed[k] = 0;
        cv_v[k] = 0u; cv_n[k] = 0u; cv_printed[k] = 0;
        cv_box[k][0] = cv_box[k][1] = cv_box[k][2] = cv_box[k][3] = -1;
    }
    for (k = 0u; k < PS2_SCREEN_HEIGHT; k++) {
        s_live_cur_ink[k] = 0u;
        s_live_cur_same[k] = 0u;
        s_live_cover_row[k] = 0u;
        s_live_nostream_row[k] = 0u;
    }
    for (k = 0u; k < sizeof ns_why / sizeof ns_why[0]; k++) ns_why[k] = 0u;
    for (k = 0u; k < GSLIVE_SH_N; k++) {
        sh_tot[k] = 0u; sh_r[k] = 0u; sh_g[k] = 0u; sh_b[k] = 0u;
        sh_other[k] = 0u; sh_same[k] = 0u;
        sh_top[k] = 0u; sh_bot[k] = 0u;
        sh_outside[k] = 0u;
        for (col = 0u; col < 3u; col++) {
            sh_bbox[k][col][0] = sh_bbox[k][col][1] = GSLIVE_BB_NONE;
            sh_bbox[k][col][2] = sh_bbox[k][col][3] = GSLIVE_BB_NONE;
        }
        for (pr = 0u; pr < GSLIVE_PIC_ROWS; pr++) {
            for (col = 0u; col < GSLIVE_PIC_COLS; col++) s_band_pic[k][pr][col] = 0u;
        }
    }
    for (col = 0u; col < 3u; col++) {
        sp_n[col] = 0u;
        sp_bbox[col][0] = sp_bbox[col][1] = GSLIVE_BB_NONE;
        sp_bbox[col][2] = sp_bbox[col][3] = GSLIVE_BB_NONE;
    }
    sp_same = 0u;
    bp_n = 0u; bp_corner = 0u; bp_field = 0u;
    bp_bbox[0] = bp_bbox[1] = bp_bbox[2] = bp_bbox[3] = GSLIVE_BB_NONE;
    mv_tot = 0u; mv_same = 0u; mv_other = 0u; mv_painted = 0u;
    mv_r = 0u; mv_g = 0u; mv_b = 0u;
    mv_bbox[0] = mv_bbox[1] = mv_bbox[2] = mv_bbox[3] = GSLIVE_BB_NONE;

    ps2_kprintf("[GSLIVE] live page vs its canvas at frame %u: one MAGENTA control and\n",
                (unsigned int)frame);
    ps2_kprintf("[GSLIVE] six triangles into six boxes -- rung 12i separates the frame's\n");
    ps2_kprintf("[GSLIVE] ORDER from its SENDER, which 12h never did: its recorder groups\n");
    ps2_kprintf("[GSLIVE] were transfers 1,2,3 and the hand chain's 4,5,6,7, so every flat\n");
    ps2_kprintf("[GSLIVE] box was also an early one.  Each recorder triangle now goes alone\n");
    ps2_kprintf("[GSLIVE] in a session of its own: box D's FIRST, box A's LAST, B's IIP=0\n");
    ps2_kprintf("[GSLIVE] control early, and C/E/F are the hand controls that say a gradient\n");
    ps2_kprintf("[GSLIVE] is readable in this band.  Box A's hand chain is built, not sent.\n");
    ps2_kprintf("[GSLIVE] Rung 12j reads the SAME page again for WHERE each corner colour is:\n");
    ps2_kprintf("[GSLIVE] a bbox per box per colour, plus the corner words outside every box,\n");
    ps2_kprintf("[GSLIVE] because 1408 px in an 88x16 box is not a triangle's coverage.\n");
    ps2_kprintf("[GSLIVE] Rung 12k prints that same band as a picture, 4 px per character,\n");
    ps2_kprintf("[GSLIVE] so a rect's straight edge and a triangle's hypotenuse cannot be\n");
    ps2_kprintf("[GSLIVE] confused for each other the way two readings of a bbox can be.\n");
    ps2_kprintf("[GSLIVE] 12k's pictures said the two FLAT boxes are RECTS, not bad shading,\n");
    ps2_kprintf("[GSLIVE] and that the recorder chain interpolates when it goes out LAST.\n");
    ps2_kprintf("[GSLIVE] Rung 12l clears PRE on box D's tag alone.  ps2sdk says PRE=1 means\n");
    ps2_kprintf("[GSLIVE] the tag's PRIM field is not output, so D has been drawing under the\n");
    ps2_kprintf("[GSLIVE] SPRITE poster's latched PRIM.  B is NOT patched: it is the test of\n");
    ps2_kprintf("[GSLIVE] whether that latch carries to the next PRE=1 group or not.\n");
    ps2_kprintf("[GSLIVE] 12l answered NO, and the answer is conditional: clearing PRE only\n");
    ps2_kprintf("[GSLIVE] bites if the PRIM register D inherits differs from the TRI+IIP its\n");
    ps2_kprintf("[GSLIVE] tag carries, and no rung here read that register.  Rung 12m therefore\n");
    ps2_kprintf("[GSLIVE] zeroes D's whole PRIM FIELD too, so the one authoritative ask this\n");
    ps2_kprintf("[GSLIVE] frame makes is for POINT: points laid means 12l's block was laid by\n");
    ps2_kprintf("[GSLIVE] TRI+IIP and the answer is geometry; the same block laid means no tag\n");
    ps2_kprintf("[GSLIVE] PRIM field of this group reaches the rasteriser at all.\n");
    ps2_kprintf("[GSLIVE] 12m answered THAT too: the same 1408-px block under a tag that asks\n");
    ps2_kprintf("[GSLIVE] for POINT and under one that asks for TRI+IIP.  Two tag fields edited on\n");
    ps2_kprintf("[GSLIVE] purpose, no answer, so rung 12n doubts what both of them assumed: that\n");
    ps2_kprintf("[GSLIVE] the block in D's window is D's triangle's ink.  D is now asked 24 px\n");
    ps2_kprintf("[GSLIVE] wide at the left of the 88-px window it is read across, and painted px\n");
    ps2_kprintf("[GSLIVE] outside the ask are counted: ink that follows its ask is this group's,\n");
    ps2_kprintf("[GSLIVE] a block that holds while the ask moves is another writer's ink.\n");
    ps2_kprintf("[GSLIVE] 12n held too: asked 24 px, it laid top=88/88 painted=1408 with\n");
    ps2_kprintf("[GSLIVE] out=1024 -- a thousand painted px at an x never asked for.  Three\n");
    ps2_kprintf("[GSLIVE] inputs edited on purpose, three identical answers, so rung 12o\n");
    ps2_kprintf("[GSLIVE] asks the one question none of them could: where does THIS group's\n");
    ps2_kprintf("[GSLIVE] ink go?  D's ask moves out of the band into rows 576-591, which no\n");
    ps2_kprintf("[GSLIVE] box is read out of, and the census reads both the emptied band and\n");
    ps2_kprintf("[GSLIVE] the asked rect -- after decoding D's own XYZ2 words, so the ask is\n");
    ps2_kprintf("[GSLIVE] certified as bytes that left the EE and not as an argument.\n");
    ps2_kprintf("[GSLIVE] 12o answered, and with two surprises: the packet IS the ask, and 384\n");
    ps2_kprintf("[GSLIVE] red px fill the moved 24x16 rect -- while the band's 1408-px block\n");
    ps2_kprintf("[GSLIVE] stands unchanged with the group gone, so it was never D's.  And B\n");
    ps2_kprintf("[GSLIVE] moved: its blue rect (945 px, foreign since 12j) held, but its OWN red\n");
    ps2_kprintf("[GSLIVE] went from the left half of rows 560-574 (975 px) to row 575 alone\n");
    ps2_kprintf("[GSLIVE] (128 px).  A neighbour's y rewrote this triangle's scanlines, so rung\n");
    ps2_kprintf("[GSLIVE] 12p edits the one input left: DELIVERY.  D's group is still captured\n");
    ps2_kprintf("[GSLIVE] and decoded, and the sink then hands it to nobody.  If the moved ink\n");
    ps2_kprintf("[GSLIVE] and the band block both stand, neither is D's; and if B's red comes\n");
    ps2_kprintf("[GSLIVE] back with D's bytes absent, one transfer's delivery rules another's.\n");
    ps2_kprintf("[GSLIVE] 12p answered NEITHER: with D's bytes withheld both its windows stood\n");
    ps2_kprintf("[GSLIVE] (band 1408, moved 384) and box B, whose own group DID go out, read\n");
    ps2_kprintf("[GSLIVE] painted=0, its foreign blue rect included.  So one withheld transfer\n");
    ps2_kprintf("[GSLIVE] emptied a neighbour: no per-box count here means what 12a assumed.\n");
    ps2_kprintf("[GSLIVE] 12q arms the mask on all three recorder sessions (A, B, D) and asks\n");
    ps2_kprintf("[GSLIVE] whether ANY of its ink survives with no census bytes sent at all.\n");
    ps2_kprintf("[GSLIVE] 12q answered SPLIT: A fell 891 -> 0 and B fell 1073 -> 0, so those two\n");
    ps2_kprintf("[GSLIVE] really are this file's delivery, while D's band block (1408) and its\n");
    ps2_kprintf("[GSLIVE] moved rect (384 red) stood with `held=3, to the bus=0` -- no census byte\n");
    ps2_kprintf("[GSLIVE] reached the GS for them.  No send-side dial can now say who laid them,\n");
    ps2_kprintf("[GSLIVE] because every dial so far graded a page this census had drawn on first.\n");
    ps2_kprintf("[GSLIVE] Rung 12r dials the one input never touched -- WHEN the page is\n");
    ps2_kprintf("[GSLIVE] read: the mask is disarmed so every send goes out as at 12o, and the\n");
    ps2_kprintf("[GSLIVE] band is read once BEFORE the control is sent, by the same law, and\n");
    ps2_kprintf("[GSLIVE] printed beside the post-draw numbers.  Census #1 of the process is the\n");
    ps2_kprintf("[GSLIVE] only look at a page this file has not drawn on, so it alone carries a\n");
    ps2_kprintf("[GSLIVE] verdict -- and it came back with its own falsifier: the magenta control\n");
    ps2_kprintf("[GSLIVE] held magenta=0 painted=0, so the read IS a first look.  At that first\n");
    ps2_kprintf("[GSLIVE] look D's window already held a whole-width red-1024/blue-384 rect, C 320\n");
    ps2_kprintf("[GSLIVE] and E 1088, while A, B and F came back clean.\n");
    ps2_kprintf("[GSLIVE] That is not a foreign GS writer -- it is the desktop's own Colour Test\n");
    ps2_kprintf("[GSLIVE] Bar.  `desktop_render()` fills the bottom 40 rows (560..599) with 8\n");
    ps2_kprintf("[GSLIVE] swatches of w/8 = 100 px, and swatch 5 is `COLOR_RED`, swatch 6 its blue:\n");
    ps2_kprintf("[GSLIVE] `fill_rec()` stores them through `BTRON_PIXEL_FB()`, which swaps R and B,\n");
    ps2_kprintf("[GSLIVE] so they land bit-equal to this census's GSQUAD_RED and GSQUAD_BLUE.  The\n");
    ps2_kprintf("[GSLIVE] key tests corner words BEFORE the canvas compare, so it has counted that\n");
    ps2_kprintf("[GSLIVE] chrome as ink since 12a: D's 1408 is 64 red columns plus 24 blue ones,\n");
    ps2_kprintf("[GSLIVE] E's 1088 is 68 blue columns, C's 320 is the bar in its last 20 columns.\n");
    ps2_kprintf("[GSLIVE] Rung 12s therefore moves the instrument to rows 32..47 -- the XMB's title\n");
    ps2_kprintf("[GSLIVE] bar, chrome whose words are not corner words and which no cover rect\n");
    ps2_kprintf("[GSLIVE] re-lays -- retires D's moved ask, and reads BOTH strips before the first\n");
    ps2_kprintf("[GSLIVE] send: the new one to certify it, the old one to test the bar explanation\n");
    ps2_kprintf("[GSLIVE] with the page against its own canvas instead of with a reading of the\n");
    ps2_kprintf("[GSLIVE] source.  Every verdict below is gated on that certification.\n");
    ps2_kprintf("[GSLIVE] Rung 12t puts that certified strip to the question 12s left open.  Box A's\n");
    ps2_kprintf("[GSLIVE] recorder group, sent LAST, lays 891 px of apex-up triangle whose every cell is a\n");
    ps2_kprintf("[GSLIVE] ramp pixel; box D's -- sent FIRST, and 12i captured it as differing from A's in\n");
    ps2_kprintf("[GSLIVE] three position words and no colour word -- lays 660 px of pure red as a 44-wide\n");
    ps2_kprintf("[GSLIVE] block with a 44-px apex row, so flat AND mis-shaped; and 12j says the same red\n");
    ps2_kprintf("[GSLIVE] runs x384..579, box B's right edge through box C's whole window, both 16-px\n");
    ps2_kprintf("[GSLIVE] gutters and D's left half: 2940 px of one rectangle no session ever asked for.\n");
    ps2_kprintf("[GSLIVE] Either this packet alone does that, in which case the recorder's encoding or the\n");
    ps2_kprintf("[GSLIVE] GS state its first group lands in is the bug, or a NEIGHBOUR drew the rectangle\n");
    ps2_kprintf("[GSLIVE] and D owns only the part its window covers, in which case 12i's ORDER answer is\n");
    ps2_kprintf("[GSLIVE] about register latching, not shading.  A census that sends seven things cannot\n");
    ps2_kprintf("[GSLIVE] tell them apart -- every count in it is a sum of seven.  So census #1 now\n");
    ps2_kprintf("[GSLIVE] uploads the strip from its own canvas, sends box D's group ALONE (control, B, A\n");
    ps2_kprintf("[GSLIVE] and the hand chain C/E/F all withheld) and reads the whole band's ink extent;\n");
    ps2_kprintf("[GSLIVE] censuses 2..4 run 12i's grid on a strip reset the same way, which is also why\n");
    ps2_kprintf("[GSLIVE] 12s's verdict is no longer confined to whichever census happened to be first.\n");
    ps2_kprintf("[GSLIVE] 12t ANSWERED, and against its own verdict label: `build/repro/20261018T074741Z`\n");
    ps2_kprintf("[GSLIVE] census #1 sent box D's group alone on a reset band and got painted=1 at x580 y32\n");
    ps2_kprintf("[GSLIVE] -- the packet's own apex vertex -- where the grid gets 660 px in that window and\n");
    ps2_kprintf("[GSLIVE] 6803 px across the band.  A lone group does not even make a TRIANGLE, which is\n");
    ps2_kprintf("[GSLIVE] what PRE=1 means: GIF_PRE_ENABLE disables the tag's PRIM output, so OP and IIP\n");
    ps2_kprintf("[GSLIVE] come from whatever the last PRIM WRITE latched, and this port's recorder groups\n");
    ps2_kprintf("[GSLIVE] never write it.  12u therefore makes that write the only variable: the same eight\n");
    ps2_kprintf("[GSLIVE] words replayed four times on the same reset band -- V0 undialled, V1 with PRE\n");
    ps2_kprintf("[GSLIVE] cleared so the tag's own field can rule, V2 with a ps2sdk AD(PRIM) write of the\n");
    ps2_kprintf("[GSLIVE] Gouraud ask in front, V3 the same preamble with IIP clear as V2's control.  Tag\n");
    ps2_kprintf("[GSLIVE] field alone puts a triangle down => flip PRE in the recorder; only the AD write\n");
    ps2_kprintf("[GSLIVE] does => the recorder must program PRIM per group; V2 fields and V3 does not =>\n");
    ps2_kprintf("[GSLIVE] IIP is the shading lever, which is what the flat gears on the monitor are.\n");
    ps2_kprintf("[GSLIVE] 12u ANSWERED that, on the machine: the AD write rules and the tag field does not,\n");
    ps2_kprintf("[GSLIVE] and V2's field against V3's zero named IIP as the lever.  Rung 12v LANDS it --\n");
    ps2_kprintf("[GSLIVE] ps2_gs_rec.c's open_group() now sends a PACKED NLOOP=1/NREG=1 AD write of the same\n");
    ps2_kprintf("[GSLIVE] PRIM word its tag carries, in front of every vertex chain, and gs_rec_prim_writes()\n");
    ps2_kprintf("[GSLIVE] counts them as the [GSTS] row's pwr= column.  So this run is the fix's measurement\n");
    ps2_kprintf("[GSLIVE] and has three numbers that decide it: box D's own census row must lay a TRIANGLE\n");
    ps2_kprintf("[GSLIVE] rather than the 1 px 12t found, 12i's grid must call the recorder's cell\n");
    ps2_kprintf("[GSLIVE] INTERPOLATES, and box B -- the same chain asked with IIP clear -- must stay one\n");
    ps2_kprintf("[GSLIVE] flat colour.  B is the falsifying control: if it shades, the read is broken and\n");
    ps2_kprintf("[GSLIVE] nothing else on this page means anything.  12u's replay keeps its bite by putting\n");
    ps2_kprintf("[GSLIVE] a write of POINT in front of V0, because the census now DOES program PRIM and a\n");
    ps2_kprintf("[GSLIVE] bare chain would otherwise draw under its own frame's ask rather than under none.\n");

    if (!canvas) {
        ps2_kprintf("[GSLIVE] no bound canvas -- SKIP, the page has nothing to be compared against\n");
        return;
    }
    if (w) {
        cx0 = (int)w->client.left; cy0 = (int)w->client.top;
        cx1 = (int)w->client.right; cy1 = (int)w->client.bottom;
        if (cx1 > cx0 && cy1 > cy0) win_px = (uint32_t)(cx1 - cx0) * (uint32_t)(cy1 - cy0);
    }

    /* Rung 6c, taken before the read because the read is the long part: every cover upload that has
     * gone out since the previous census is a candidate eraser, and each is already intersected with
     * the layer's client rect by ps2_upload_cover_in_layer(), so its whole area is in the hole.  The
     * window is [s_live_cover_since, s_cover_writes); if the ring wrapped inside that window the
     * older rects are named as missed rather than silently dropped, because a verdict of "the cover
     * path did it" has to say how much of the set it actually saw. */
    if (win_px) {
        const uint32_t writes_now = s_cover_writes;
        uint32_t t = s_live_cover_since;

        cover_missed = (writes_now > t + PS2_COVER_TRACE_N)
                         ? (writes_now - t - PS2_COVER_TRACE_N) : 0u;
        if (writes_now > PS2_COVER_TRACE_N &&
            t < writes_now - PS2_COVER_TRACE_N)
            t = writes_now - PS2_COVER_TRACE_N;
        for (; t < writes_now; t++) {
            const int *r = s_cover_rect[t % PS2_COVER_TRACE_N];
            const int x0 = r[0] > cx0 ? r[0] : cx0;
            const int x1 = r[2] < cx1 ? r[2] : cx1;
            const int y0 = r[1] > cy0 ? r[1] : cy0;
            const int y1 = r[3] < cy1 ? r[3] : cy1;
            const int xw = x1 > x0 ? x1 - x0 : 0;
            int rows = y1 > y0 ? y1 - y0 : 0;
            int rr;

            if (y0 - cy0 >= PS2_SCREEN_HEIGHT) rows = 0;
            else if (y0 - cy0 + rows > PS2_SCREEN_HEIGHT)
                rows = PS2_SCREEN_HEIGHT - (y0 - cy0);
            if (xw <= 0 || rows <= 0) continue;
            if (s_cover_kind[t % PS2_COVER_TRACE_N] == PS2_UPLOAD_COVER) {
                cover_rects++;
                cover_px += (uint32_t)xw * (uint32_t)rows;   /* rung 6c's test is about this kind alone */
            } else {
                canvas_rects++;
                canvas_px += (uint32_t)xw * (uint32_t)rows;
                if (!s_cover_stream[t % PS2_COVER_TRACE_N]) {
                    const uint8_t why = s_cover_why[t % PS2_COVER_TRACE_N];

                    nostream_rects++;
                    if (why < sizeof ns_why / sizeof ns_why[0]) ns_why[why]++;
                    for (rr = 0; rr < rows; rr++)
                        s_live_nostream_row[(uint32_t)(y0 - cy0 + rr)] += (uint32_t)xw;
                }
            }
            if (hc_n < PS2_HIST_RECTS) {
                hc_rect[hc_n][0] = x0;  hc_rect[hc_n][1] = y0;
                hc_rect[hc_n][2] = x1;  hc_rect[hc_n][3] = y1;
                hc_pass[hc_n] = s_cover_pass[t % PS2_COVER_TRACE_N];
                hc_kind[hc_n] = s_cover_kind[t % PS2_COVER_TRACE_N];
                hc_why[hc_n]  = s_cover_why[t % PS2_COVER_TRACE_N];
                hc_n++;
            }
            for (rr = 0; rr < rows; rr++)
                s_live_cover_row[(uint32_t)(y0 - cy0 + rr)] += (uint32_t)xw;
        }
        s_live_cover_since = writes_now;
    }

    /* The band buffer is asked for before the control is drawn for the same reason gsread allocates
     * before it sends: Imalloc() prints a [HEAP] row over its own threshold, and a print repaints
     * the console band at full width, which is the one upload that would land between this rung's
     * draw and its read. */
    buf = (uint32_t *)Imalloc((size_t)GSREAD_BAND_QWC * 16u);
    if (!buf) {
        ps2_kprintf("[GSLIVE] no %u B band buffer -- SKIP, nothing was read\n",
                    (unsigned int)(GSREAD_BAND_QWC * 16u));
        return;
    }
    gspeak_reset();     /* rung 10's two grids: every pixel of this read goes into them */

    /* Whatever the frame's own presents queued has to be on the GS before a read is meaningful --
     * and before a synchronous send is allowed to interleave with it. */
    (void)ps2_gif_drain();
    (void)ps2_gif_reap();

    /* Rung 12t(i): put the band back to its canvas, then look.
     *
     * 12s could only grade census #1, because VRAM is not cleared between app opens and a dirty window at
     * census #2 is equally this file's own triangle left over from census #1.  That limit is what stops any
     * rung that wants a different SEND set from getting a clean page: the first census has one page and
     * every later one inherits it.  So the census now writes the strip first -- `ps2_gs_upload()` is a
     * Host->Local IMAGE stream of these very canvas rows, synchronous through `ps2_gs_gif_send()`, and the
     * identical thing the present does every frame, so it is the port's own law being applied rather than a
     * special state the bench invents.  Rows GSLIVE_SP_Y0..Y1 go back to the canvas: the band the six boxes
     * are read out of AND the spill strip around them, because 12j's off-box block is half of the question.
     * The old colour-bar strip at 560..575 is deliberately NOT reset: its page-vs-canvas agreement is 12s's
     * independent control on the chrome explanation, and an upload would make that agreement true by
     * construction instead of by measurement.
     *
     * The reset does not have to be trusted.  The pre-draw read below is the check on it: `band_dirty` counts
     * the six windows that still disagree with their canvas or still hold a sentinel, and the 12s verdict
     * takes case 3 when that count is non-zero, which now means the upload failed rather than that some
     * desktop writer is ahead of this census. */
    ps2_gs_upload(0, GSLIVE_SP_Y0, PS2_SCREEN_WIDTH, GSLIVE_SP_Y1 - GSLIVE_SP_Y0);

    /* Rung 12r: look at the page BEFORE this census puts anything on it.  12q's answer split the band down
     * the middle -- A emptied 891 -> 0 and B emptied 1073 -> 0 with their own bytes withheld, while D's
     * 1408-px block and D's 384-px moved rect stood at their 12o numbers with NOTHING of this file's ever
     * reaching the GS -- so the two readings of the same column are now opposite claims about the same pixel
     * count, and no send-side dial can settle them: every dial still graded a page this census had already
     * drawn on.  This read is the other half of the instrument.  The GIF is drained and reaped above, so
     * what comes back is the page as the app, the console and any EARLIER census left it; the magenta
     * control has not been sent yet and neither has any triangle.  Nothing prints before it (`Imalloc()`
     * already happened above, and a print repaints the console band), and nothing prints between it and the
     * draw below, so no upload of this file's sits between the look and the page it describes.
     *
     * `s_pre_pass` is what makes the reading decidable: the census re-runs on every app open, and at pass 2
     * a held block may be this file's OWN triangle from pass 1 -- VRAM is not cleared between opens -- which
     * is a different finding from "the app laid it".  Only pass 1 can say that, so only pass 1 is graded.
     *
     * Rung 12s reads TWO bands in that one pre-draw pass, because 12r's first look was real and its conclusion
     * was still wrong: the block it found in D is the desktop's own Colour Test Bar (see the note at GSLIVE_X0),
     * which the corner-word key counted as ink because it tests colours BEFORE the canvas compare.  The
     * instrument therefore moved to rows 32..47, and the old strip stays in the read as a control on that
     * explanation: a window whose page words ARE the canvas words at the same positions is chrome, and only a
     * window where the two disagree is a writer this file has not named. */
    s_pre_pass++;
    /* Rung 12t(ii): census #1 of the process runs LONE -- box D's recorder session and nothing else -- and
     * censuses 2..4 run 12i's whole grid, each on a band the reset above just made equal to its canvas.
     * D is the box because D is the anomaly: it is the frame's FIRST transfer, it is the one box whose count
     * 12q withheld-and-it-stood, and the block inside it reaches outside its own ask. */
    s_census_only = (s_pre_pass == 1u) ? 3u : (uint32_t)GSLIVE_SH_N;
    s_pre_ok = 0;
    s_pre_old_ok = 0;
    {
        const uint32_t pre_y[2]  = { (uint32_t)GSLIVE_SH_Y0, GSLIVE_OLD_BAND_Y0 };
        gslive_pre_t *pre_box[2] = { s_pre, s_pre_old };
        uint32_t pb, pre_b, pre_x;
        int ok;

        for (pb = 0u; pb < 2u; pb++) {
            ok = 0;
            for (pre_b = 0u; pre_b < (uint32_t)GSLIVE_PIC_ROWS; pre_b++)
                for (pre_x = 0u; pre_x < width; pre_x++)
                    buf[(size_t)pre_b * width + pre_x] = GSREAD_SENT;
            if (ps2_gs_vram_read(0, (int)pre_y[pb], (int)width,
                                 (int)GSLIVE_PIC_ROWS, buf) > 0
                    && ps2_gs_vram_read_err() == GS_READ_BUS_OK) {
                ok = 1;
                for (pre_b = 0u; pre_b < GSLIVE_SH_N; pre_b++)
                    ps2_gslive_pre_rect(&pre_box[pb][pre_b], buf, width, canvas,
                                        s_sh_x0[pre_b], s_sh_x1[pre_b],
                                        pre_y[pb], (uint32_t)GSLIVE_PIC_ROWS);
            }
            if (pb == 0u) s_pre_ok = ok; else s_pre_old_ok = ok;
        }

        /* 12r's own control, on the band that actually holds it: the magenta box has exactly one writer --
         * the send below -- so any magenta here is this read looking at ink that has not been laid yet.
         * Rung 12s makes it certify more than the read's timing: the control now sits INSIDE the band, at
         * x32..95 of rows 32..47, so a clean read there is 12a's missing proof that the strip the six boxes
         * are read out of holds nothing before this census draws on it. */
        s_pre_ctrl_mag = 0u;
        s_pre_ctrl_paint = 0u;
        if (s_pre_ok) {
            for (pre_b = 0u; pre_b < (uint32_t)GSLIVE_PIC_ROWS; pre_b++) {
                const uint32_t *row = buf + (size_t)pre_b * width;
                const uint32_t *crow = canvas + (uint32_t)(GSLIVE_SH_Y0 + pre_b) * width;

                for (pre_x = (uint32_t)GSLIVE_X0; pre_x < (uint32_t)GSLIVE_X1; pre_x++) {
                    if (row[pre_x] == GSQUAD_MAGENTA) s_pre_ctrl_mag++;
                    if (row[pre_x] != crow[pre_x]) s_pre_ctrl_paint++;
                }
            }
        }
    }

    r.x0 = GSLIVE_X0; r.y0 = GSLIVE_Y0;
    r.x1 = GSLIVE_X1; r.y1 = GSLIVE_Y1;
    qw = gs_poster_packet(&r, 1u, GSQUAD_MAGENTA, s_pkt, gs_poster_qwc(1u));
    ps2_stage1_gs_env(&env);
    if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0) {
        ps2_kprintf("[GSLIVE] the control was refused (qw=%d) -- SKIP, its count cannot certify a read\n",
                    (int)qw);
        Ifree(buf);
        return;
    }
    /* Rung 12t: the environment the control is drawn under is applied in a LONE census too -- that is the
     * state D's lone group will rasterise in, and it has to be the same state the grid censuses give it --
     * but the magenta quad itself is one of the sends this census exists to withhold. */
    if (s_census_only >= GSLIVE_SH_N) ps2_gs_gif_send(s_pkt, (uint32_t)qw);

    /* Rung 12a rides the same window as the magenta box: the GIF is drained, the environment is the
     * one the box was drawn with, and nothing prints between this draw and the read below.  Rung 12i
     * puts the recorder's own triangles at both ends of it -- D first, A last -- because the order the
     * transfers leave in is now the variable being measured. */
    ps2_gslive_shading_reset();
    /* Rung 12q asked whether ANY of this census's counted ink depends on its own bytes, and got a split
     * answer: A emptied 891 -> 0 and B emptied 1073 -> 0, so those two really are delivered by this file,
     * while D's band block (1408) and D's moved rect (384 red) stood at their 12o numbers with
     * `mask=0xb, held=3, to the bus=0` -- no census byte ever reached the GS for them.  C moved again
     * (1221 -> 1121, the same shift it made in 12p when only D was held), so the hand shapes answer to
     * D's delivery too, which is a coupling no per-box model has.  That leaves two claims about the same
     * 1408 px and no send-side dial that can separate them: every dial so far has graded a page this
     * census had ALREADY drawn on.  So 12r puts the dial back where the claim lives -- time, not delivery.
     * The mask stays DISARMED (the reset above leaves it 0), which is what makes the whole rung honest:
     * every session goes out as it did at 12o, the shading grid grades again instead of self-gating to
     * SKIP, and the pre-draw read taken above is the only new input.  Rung 12s then moved the geometry: the
     * ask is D's WHOLE window in the new band again, because the narrowing and the y576-591 rect were both
     * dials on a strip that turned out to be the Colour Test Bar, and a dial that grades chrome answers
     * nothing.  With ask == window, 12n's `out=` is identically zero and 12o's moved-rect rows self-gate to
     * SKIP, which is the honest price of the finding. */
    ps2_gslive_shading_session(3u, GSLIVE_SH_D_X0, GSLIVE_SH_D_X1,
                               GSLIVE_SH_Y0, GSLIVE_SH_Y1, 1u);
    ps2_gslive_shading_session(1u, GSLIVE_SH_B_X0, GSLIVE_SH_B_X1,
                               GSLIVE_SH_Y0, GSLIVE_SH_Y1, 0u);
    ps2_gslive_shape_matrix();
    ps2_gslive_shape_hold(1, GSLIVE_SH_A_X0, GSLIVE_SH_A_X1);
    ps2_gslive_shading_session(0u, GSLIVE_SH_A_X0, GSLIVE_SH_A_X1,
                               GSLIVE_SH_Y0, GSLIVE_SH_Y1, 1u);

    us = btron_render_perf_us();
    for (y_start = 0u; y_start < height; y_start += GSREAD_BAND_ROWS) {
        const uint32_t rows = (height - y_start < GSREAD_BAND_ROWS)
                                ? (height - y_start) : GSREAD_BAND_ROWS;

        for (i = 0u; i < width * rows; i++) buf[i] = GSREAD_SENT;
        got = ps2_gs_vram_read(0, (int)y_start, (int)width, (int)rows, buf);
        err = ps2_gs_vram_read_err();
        if (err != GS_READ_BUS_OK) {
            ps2_kprintf("[GSLIVE] page y=%u FAILED: %s -- SKIP, the counts above are a part-page\n",
                        (unsigned int)y_start, gsread_bus_name(err));
            Ifree(buf);
            return;
        }
        qw_sent += (uint32_t)got;
        read_px += width * rows;

        for (i = 0u; i < width * rows; i++) {
            const uint32_t v = buf[i];
            uint32_t x, y;
            int inside;

            if (v == GSREAD_SENT) { sent++; continue; }
            x = i % width;
            y = y_start + i / width;
#if BTRON_PS2_BENCH_GS >= 5
            ps2_bench_ink_census_px(x, y, v);     /* rung 27's second road, on this same read */
#endif
            /* Rung 10 keeps every pixel this loop sees, including the control box and the console's
             * own rows: the magenta box coming back magenta in the P grid is the read's alignment
             * proving itself in the picture, which is a stronger statement than `ctrl=1024` in a
             * count column. */
            gspeak_add(&s_spk_page, x, y, v);
            gspeak_add(&s_spk_canvas, x, y, canvas[y * width + x]);
            if (x >= GSLIVE_X0 && x < GSLIVE_X1 && y >= GSLIVE_Y0 && y < GSLIVE_Y1) {
                /* This rung's own ink, excluded by COORDINATE and not by colour: the box is the
                 * certification and nothing else, so a magenta UI pixel outside it stays census
                 * material and a control that partly failed still shows as ctrl < GSLIVE_PX.  The
                 * exclusion is counted, so `read = sent + box + same + diff_all` is a partition the
                 * row below can be checked against rather than a set of numbers that merely look
                 * plausible next to each other. */
                box_px++;
                if (v == GSQUAD_MAGENTA) { ctrl++; gsread_box(ctrl_box, x, y); }
                continue;
            }
            if (y >= GSLIVE_SH_Y0 && y < GSLIVE_SH_Y1) {
                uint32_t sh;

                /* Rung 12t: attributed to no box, this pixel is band ink, and it is either a corner word or
                 * a ramp pixel.  The box walk below still counts it its own way; nothing is moved here. */
                if (v != canvas[y * width + x]) {
                    bp_n++;
                    gslive_bbox(bp_bbox, x, y);
                    if (v == GSQUAD_RED || v == GSQUAD_GREEN || v == GSQUAD_BLUE) bp_corner++;
                    else bp_field++;
                }
                for (sh = 0u; sh < GSLIVE_SH_N; sh++) {
                    if (x >= s_sh_x0[sh] && x < s_sh_x1[sh]) break;
                }
                if (sh < GSLIVE_SH_N) {
                    /* Counted by coordinate and excluded from the page's own partition the same way
                     * the magenta box is: this rung's ink is its own evidence, and a red pixel that
                     * also entered `ink_v[]` would spend one of eight histogram slots on the
                     * instrument.  The gspeak grids above still hold these pixels, so rung 10's
                     * picture shows the pair and can be looked at rather than read as counts. */
                    const uint32_t prow = y - (uint32_t)GSLIVE_SH_Y0;
                    const uint32_t pcol = (x - s_sh_x0[sh]) >> GSLIVE_PIC_SHIFT;
                    uint32_t bit;

                    sh_tot[sh]++;
                    sh_px++;
                    if (v == GSQUAD_RED)        { bit = GSLIVE_PIC_R;     sh_r[sh]++;
                                                  gslive_bbox(sh_bbox[sh][0], x, y); }
                    else if (v == GSQUAD_GREEN) { bit = GSLIVE_PIC_G;     sh_g[sh]++;
                                                  gslive_bbox(sh_bbox[sh][1], x, y); }
                    else if (v == GSQUAD_BLUE)  { bit = GSLIVE_PIC_B;     sh_b[sh]++;
                                                  gslive_bbox(sh_bbox[sh][2], x, y); }
                    else if (v == canvas[y * width + x]) { bit = GSLIVE_PIC_CANVAS; sh_same[sh]++; }
                    else                                  { bit = GSLIVE_PIC_FIELD;  sh_other[sh]++; }
                    /* Rung 12l's shape tally, straight off the class bit: the first and last rows of a
                     * box separate a rect (its top edge is the whole width) from an apex-up triangle
                     * (one or two px), and they do it whatever the shading flag says, so this grades a
                     * flat triangle as well as a gradient one. */
                    if (bit != GSLIVE_PIC_CANVAS) {
                        if (prow == 0u) sh_top[sh]++;
                        else if (prow == (uint32_t)GSLIVE_PIC_ROWS - 1u) sh_bot[sh]++;
                        /* Rung 12n's attribution count, widened by 12o to the other axis: this pixel is
                         * painted, and it sits outside the RECT the session asked.  Boxes with no ask of
                         * their own -- the hand controls are not drawn by a session -- are read AT their
                         * window, so their count is 0 by definition and `out=` can only grow where a
                         * geometry dial moved.  With 12o's ask entirely below the band, every painted band
                         * pixel of that box is outside it, so out= == px is 12o's reading of the block
                         * holding; out= == 0 would mean the band emptied itself, which is the other answer. */
                        if (s_sh_ask_x1[sh] != 0
                                && ((int32_t)x < s_sh_ask_x0[sh]
                                    || (int32_t)x >= s_sh_ask_x1[sh]
                                    || (int32_t)y < s_sh_ask_y0[sh]
                                    || (int32_t)y >= s_sh_ask_y1[sh]))
                            sh_outside[sh]++;
                    }
                    if (prow < GSLIVE_PIC_ROWS && pcol < GSLIVE_PIC_COLS)
                        s_band_pic[sh][prow][pcol] =
                            (uint8_t)(s_band_pic[sh][prow][pcol] | bit);
                    continue;
                }
            }
            if (y >= GSLIVE_SP_Y0 && y < GSLIVE_SP_Y1) {
                /* Rung 12j: this pixel is outside every box, in the rows the band does not read, and is
                 * bit-exactly one of the three corner words -- so either one of this rung's triangles was
                 * rasterised HERE, or the desktop painted this exact word and put it on its own canvas
                 * too.  The second case is what `sp_same` counts, and it is why the tally asks the canvas
                 * rather than calling every corner-coloured pixel outside a box a spill: an uploaded UI
                 * pixel is page == canvas, ink this census did not lay is not.
                 *
                 * No `continue` here, unlike the box branch above: a spill pixel is still a page pixel,
                 * and the partition `read = sent + box + same + diff_all` has to stay checkable. */
                int sc = -1;
                int claimed = 0;   /* rung 12o: inside a moved ask, so mv_* names this pixel already */
                uint32_t mb;

                /* Rung 12o: a session asked for a rect OUTSIDE the band (only the dialled box can be in
                 * that state, because every other ask is at the band's own rows), and this pixel lies inside
                 * it.  Classed by the same three corner words and the same `differs from the canvas` law the
                 * box branch uses, so the two readings are the same measurement at two positions. */
                for (mb = 0u; mb < GSLIVE_SH_N; mb++) {
                    if (s_sh_ask_x1[mb] == 0) continue;            /* never asked */
                    if (s_sh_ask_y0[mb] >= (int32_t)GSLIVE_SH_Y0
                            && s_sh_ask_y1[mb] <= (int32_t)GSLIVE_SH_Y1) continue; /* asked IN the band */
                    if ((int32_t)x >= s_sh_ask_x0[mb] && (int32_t)x < s_sh_ask_x1[mb]
                            && (int32_t)y >= s_sh_ask_y0[mb] && (int32_t)y < s_sh_ask_y1[mb]) {
                        claimed = 1;
                        mv_box = mb;
                        mv_tot++;
                        if (v == GSQUAD_RED)        { mv_r++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        else if (v == GSQUAD_GREEN) { mv_g++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        else if (v == GSQUAD_BLUE)  { mv_b++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        else if (v == canvas[y * width + x]) mv_same++;
                        else { mv_other++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        break;
                    }
                }
                if (sc < 0 && !claimed) {
                    /* 12j's aggregate keeps its original meaning -- corner colours off-box and NOT named by
                     * any ask of this census -- so its bbox cannot merge an unknown writer with a dialled
                     * one.  A corner word inside the moved rect is counted once, by mv_*. */
                    if (v == GSQUAD_RED)               sc = 0;
                    else if (v == GSQUAD_GREEN)        sc = 1;
                    else if (v == GSQUAD_BLUE)         sc = 2;
                    if (sc >= 0) {
                        if (v == canvas[y * width + x]) sp_same++;
                        else { sp_n[sc]++; gslive_bbox(sp_bbox[sc], x, y); }
                    }
                }
            }
            if (win_px && (int)x >= cx0 && (int)x < cx1 && (int)y >= cy0 && (int)y < cy1) {
                /* Rung 6b's per-row tally, restricted to the client rect and keyed on the row rather
                 * than on the band: the two reads have to be comparable frame to frame even though
                 * the bands themselves are a read-stride detail of this file, not a property of the
                 * picture.  A pixel equal to the canvas is either still untouched or has been put
                 * BACK to the canvas, and telling those two apart is the whole question, so it is
                 * answered by the row's count moving down and not by the pixel's colour. */
                const uint32_t row = (uint32_t)((int)y - cy0);
                if (row < PS2_SCREEN_HEIGHT) {
                    if (v == canvas[y * width + x]) {
                        s_live_cur_same[row]++;
                        /* Rung 6c: what the page was put BACK to, colour by colour.  The ink
                         * histogram below cannot see these -- they are the pixels that agree with
                         * the canvas -- and they are the whole of the eraser's evidence trail. */
                        for (k = 0u; k < cv_bins; k++) {
                            if (cv_v[k] != v) continue;
                            cv_n[k]++;
                            gsread_box(cv_box[k], x, y);
                            break;
                        }
                        if (k >= cv_bins) {
                            if (cv_bins < GSLIVE_TOP) {
                                cv_v[cv_bins] = v;
                                cv_n[cv_bins] = 1u;
                                cv_box[cv_bins][0] = cv_box[cv_bins][2] = (int)x;
                                cv_box[cv_bins][1] = cv_box[cv_bins][3] = (int)y;
                                cv_bins++;
                            } else cv_rare++;
                        }
                    } else s_live_cur_ink[row]++;
                }
            }
            if (v == canvas[y * width + x]) { same++; continue; }            diff_all++;
            gsread_box(all_box, x, y);
            if (v == GSLIVE_INK || v == GSLIVE_BG) { diff_console++; continue; }
            diff_ink++;
            inside = ((int)x >= cx0 && (int)x < cx1 && (int)y >= cy0 && (int)y < cy1);
            if (inside) { diff_win++; gsread_box(win_box, x, y); }

            for (k = 0u; k < bins; k++) {
                if (ink_v[k] != v) continue;
                ink_n[k]++;
                if (inside) ink_in[k]++;
                gsread_box(ink_box[k], x, y);
                break;
            }
            if (k < bins) continue;
            if (bins < GSLIVE_TOP) {
                ink_v[bins] = v;
                ink_n[bins] = 1u;
                ink_in[bins] = inside ? 1u : 0u;
                ink_box[bins][0] = ink_box[bins][2] = (int)x;
                ink_box[bins][1] = ink_box[bins][3] = (int)y;
                bins++;
            } else {
                /* No slot: counted, not evicted.  Evicting a kept colour would make the rows below
                 * disagree with themselves frame to frame, and a census whose histogram is not the
                 * same histogram next time cannot be quoted against a fix. */
                rare++;
            }
        }
    }
    us = btron_render_perf_us() - us;
#if BTRON_PS2_BENCH_GS >= 5
    /* Rung 27, half 3: the entry read and this walk's own road, for the same pixels.  It prints here
     * rather than next to the maps because the walk above is the second road's source, and because
     * the maps' block reads would otherwise be a third thing between the two samples. */
    if (w && w->gl_layer) ps2_bench_ink_cross(&w->client, width, canvas);
#endif
    /* Rung 21's pixels, on rung 10's gate, and BEFORE the replay below for the reason the replay's own
     * comment gives: it sends and re-reads bands into `buf`, so after it the buffer is not this
     * present's page any more. */
    if (sent == 0u && w && w->gl_layer && win_px != 0u &&
        diff_win > win_px / 2u && s_gspix_printed < 2u) {
        s_gspix_printed++;
        gspeak_pixels(buf, GSREAD_BAND_QWC * 4u, width, cx0, cy0, cx1, cy1);
    }
    /* Rung 21b/21c/21d: the whole client as luminance, alpha-owner and hue maps, once, on the same
     * gate and for the same reason -- before the replay, which rewrites `buf`. */
    if (sent == 0u && w && w->gl_layer && win_px != 0u &&
        diff_win > win_px / 2u && s_gspix_mapped < 1u) {
        s_gspix_mapped++;
        gspeak_page_map(buf, GSREAD_BAND_QWC * 4u, width, cx0, cy0, cx1, cy1);
    }
    /* Rung 12u: replay D's captured lone group four times under four different PRIM sources.  It runs here,
     * after this census's own page has been graded into the counters above and before `buf` goes back, so it
     * cannot disturb any count the rows above print -- and it prints nothing, because a print would upload
     * the console band between one variant's send and that variant's read. */
    ps2_gslive_u_probe(buf, canvas, width);
    Ifree(buf);

    ps2_kprintf("[GSLIVE] win: client=%d,%d..%d,%d px=%u gl_layer=%d img_valid=%d visible=%d\n",
                cx0, cy0, cx1, cy1, (unsigned int)win_px,
                w ? (int)w->gl_layer : -1, w ? (int)w->img_valid : -1,
                w ? (int)w->visible : -1);
    ps2_kprintf("[GSLIVE] page: read=%u same=%u diff_all=%u console=%u ink=%u box=%u ctrl=%u "
                "shading=%u sent=%u %u us %u qw\n",
                (unsigned int)read_px, (unsigned int)same, (unsigned int)diff_all,
                (unsigned int)diff_console, (unsigned int)diff_ink, (unsigned int)box_px,
                (unsigned int)ctrl, (unsigned int)sh_px, (unsigned int)sent,
                (unsigned int)us, (unsigned int)qw_sent);
    ps2_kprintf("[GSLIVE] boxes: all=%d,%d..%d,%d window=%d,%d..%d,%d in_win=%u control=%u/%u "
                "%d,%d..%d,%d rare=%u\n",
                all_box[0], all_box[1], all_box[2], all_box[3],
                win_box[0], win_box[1], win_box[2], win_box[3],
                (unsigned int)diff_win, (unsigned int)ctrl, (unsigned int)GSLIVE_PX,
                ctrl_box[0], ctrl_box[1], ctrl_box[2], ctrl_box[3], (unsigned int)rare);
    /* The four hottest inks, each named ONCE.  The search has to seed itself on an UNPRINTED bin: it
     * only ever compares a candidate against `best`, so starting `best` at bin 0 re-selects bin 0 on
     * every pass from the second row onward whenever bin 0 is the hottest -- which, being the largest
     * bin, it always is.  The rung then printed the same colour four times, and its own closing
     * instruction ("read the ink colours and boxes above") had one colour to read.  That is the whole
     * difference between "this page is one flat plate" and "this page is this plate plus these inks",
     * and only the histogram can answer the second question. */
    for (k = 0u; k < 4u && k < bins; k++) {
        int seeded = 0;

        best = 0u;
        for (i = 0u; i < bins; i++) {
            if (printed[i]) continue;
            if (!seeded || ink_n[i] > ink_n[best]) { best = i; seeded = 1; }
        }
        if (!seeded) break;
        printed[best] = 1;
        ps2_kprintf("[GSLIVE] ink: 0x%08x n=%u in=%u box=%d,%d..%d,%d\n",
                    (unsigned int)ink_v[best], (unsigned int)ink_n[best],
                    (unsigned int)ink_in[best],
                    ink_box[best][0], ink_box[best][1], ink_box[best][2], ink_box[best][3]);
    }

    /* Rung 12a's and 12d's reading, printed where its counts can be checked against the certification
     * above.  `painted` is the box minus the pixels that still hold their canvas value, and it is the
     * denominator every reading needs: a triangle covers half its rect, so A's and B's 2,048-px boxes
     * hold ~1,024 painted px and C/D/E's 1,920-px boxes ~960, and a threshold in whole pixels would
     * call a half-drawn box a flat one.  A box is GRADIENT when a quarter of its painted px are none of
     * its three corners: an interpolated triangle's interior is everything between its corners, so the
     * field is most of the box and any threshold well under a half separates the two readings without
     * needing to know how the rasteriser walks a span. */
    {
        char grad[16];
        uint32_t n_grad = 0u, n_empty = 0u, n_flat = 0u, gn = 0u;
        uint32_t b;

        for (b = 0u; b < GSLIVE_SH_N; b++) {
            const uint32_t painted = sh_tot[b] - sh_same[b];
            const char *kind;
            char win = '-';   /* for a flat box, the corner that won -- and since red is v0, green is
                               * v1 and blue is v2, the same letter names WHICH VERTEX this shape's GS
                               * drew with, the half of rung 12a's answer a packet cannot show. */

            if (painted == 0u) {
                kind = "lays NOTHING";
                n_empty++;
            } else if (sh_other[b] > painted / 4u) {
                kind = "INTERPOLATES";
                n_grad++;
                if (gn + 1u < sizeof(grad)) grad[gn++] = (char)('A' + b);
            } else {
                win = (sh_r[b] >= sh_g[b] && sh_r[b] >= sh_b[b]) ? 'R'
                    : ((sh_g[b] >= sh_b[b]) ? 'G' : 'B');
                kind = "one FLAT colour";
                n_flat++;
            }
            ps2_kprintf("[GSLIVE] shade %s: tot=%u painted=%u red=%u green=%u blue=%u other=%u "
                        "canvas=%u -> %s %c\n", s_sh_label[b],
                        (unsigned int)sh_tot[b], (unsigned int)painted,
                        (unsigned int)sh_r[b], (unsigned int)sh_g[b], (unsigned int)sh_b[b],
                        (unsigned int)sh_other[b], (unsigned int)sh_same[b],
                        kind, win);
        }
        grad[gn] = '\0';
        ps2_kprintf("[GSLIVE] shapes: recorder %u group(s) / %u refused, %u captured send(s),"
                    " hand matrix %u sent / %u refused; %u gradient (%s), %u flat, %u empty\n",
                    (unsigned int)s_sh_groups, (unsigned int)s_sh_refused,
                    (unsigned int)s_cap_sends,
                    (unsigned int)s_shape_sent, (unsigned int)s_shape_refused,
                    (unsigned int)n_grad, grad,
                    (unsigned int)n_flat, (unsigned int)n_empty);

        /* Rung 12j -- WHERE, not how many.  The counts above grade a box as a gradient or a flat colour,
         * and 12i's answer ("the variable is the frame's ORDER") only means shading if the flat boxes are
         * flat AT THEIR OWN COORDINATES.  They did not look like that: box D came back `painted=1408`,
         * its whole 88x16 rect, split red=1024 blue=384, and a 1,024-px red region inside an 88-wide box
         * is a 64-wide full-height column, which no triangle in this census has.  Box B came back
         * `painted=1920` of 2,048 in TWO colours, which is two triangles' coverage for one packet.  So
         * the same walk now records the bounding box of each corner colour inside its box, and of the
         * three corner words OUTSIDE every box, and this row prints both.
         *
         * `%c %u-%ux%u` is the x range and the number of rows that colour spans; a triangle's own corner
         * colour spans the full 16 rows only at its wide end and a few rows at the apex, so a colour that
         * spans 16 rows across a stripe of x is a rect, and the SOLID rows below name exactly that.  A
         * box that never got its colour prints `%c n=0` rather than an invented range. */
        {
            static const char sp_chr[3] = { 'R', 'G', 'B' };
            static const char *const sp_word[3] = { "red", "green", "blue" };
            uint32_t b2, sp_all = 0u, sp_i;

            for (b2 = 0u; b2 < GSLIVE_SH_N; b2++) {
                ps2_kprintf("[GSLIVE] 12j %c:", s_sh_label[b2][0]);
                for (col = 0u; col < 3u; col++) {
                    const uint32_t cnt = (col == 0u) ? sh_r[b2] : ((col == 1u) ? sh_g[b2] : sh_b[b2]);

                    if (cnt == 0u) {
                        ps2_kprintf(" %c n=0", sp_chr[col]);
                        continue;
                    }
                    ps2_kprintf(" %c %u-%ux%u n=%u", sp_chr[col],
                                (unsigned int)sh_bbox[b2][col][0], (unsigned int)sh_bbox[b2][col][1],
                                (unsigned int)(sh_bbox[b2][col][3] - sh_bbox[b2][col][2] + 1u),
                                (unsigned int)cnt);
                }
                ps2_kprintf("\n");
            }
            /* A colour that fills its own bounding box solidly is not a corner of a triangle.  Quoted per
             * box so the row that says `one FLAT colour` can be read against the SHAPE it was counted on:
             * SOLID means the geometry is wrong, no SOLID row with a full box means the rasteriser laid a
             * real triangle and only the interpolation failed. */
            for (b2 = 0u; b2 < GSLIVE_SH_N; b2++) {
                for (col = 0u; col < 3u; col++) {
                    const uint32_t cnt = (col == 0u) ? sh_r[b2] : ((col == 1u) ? sh_g[b2] : sh_b[b2]);
                    const uint32_t area = (sh_bbox[b2][col][1] - sh_bbox[b2][col][0] + 1u)
                                        * (sh_bbox[b2][col][3] - sh_bbox[b2][col][2] + 1u);

                    if (cnt >= area && cnt != 0u) {
                        ps2_kprintf("[GSLIVE] 12j SOLID %c %s: x%u-%u y%u-%u n=%u fills its own box\n",
                                    s_sh_label[b2][0], sp_word[col],
                                    (unsigned int)sh_bbox[b2][col][0], (unsigned int)sh_bbox[b2][col][1],
                                    (unsigned int)sh_bbox[b2][col][2], (unsigned int)sh_bbox[b2][col][3],
                                    (unsigned int)cnt);
                    }
                }
            }
            for (sp_i = 0u; sp_i < 3u; sp_i++) sp_all += sp_n[sp_i];
            ps2_kprintf("[GSLIVE] 12j spill off-box rows %u..%u: R=%u G=%u B=%u (equal-canvas %u)\n",
                        (unsigned int)GSLIVE_SP_Y0, (unsigned int)GSLIVE_SP_Y1 - 1u,
                        (unsigned int)sp_n[0], (unsigned int)sp_n[1], (unsigned int)sp_n[2],
                        (unsigned int)sp_same);
            for (sp_i = 0u; sp_i < 3u; sp_i++) {
                if (sp_n[sp_i] == 0u) continue;
                ps2_kprintf("[GSLIVE] 12j spill %s: x%u-%u y%u-%u n=%u\n", sp_word[sp_i],
                            (unsigned int)sp_bbox[sp_i][0], (unsigned int)sp_bbox[sp_i][1],
                            (unsigned int)sp_bbox[sp_i][2], (unsigned int)sp_bbox[sp_i][3],
                            (unsigned int)sp_n[sp_i]);
            }
            ps2_kprintf("[GSLIVE] 12j verdict: %s\n",
                        (sp_all == 0u)
                          ? "nothing this rung laid is outside its own box, so a FLAT box is a SHADING "
                              "answer and 12i's ORDER reading stands"
                          : "corner-coloured ink landed off-box, so at least one triangle was rasterised "
                              "somewhere other than its own rect -- the flat boxes are a GEOMETRY answer, "
                              "not a shading one, and 12i's grid has to be re-read that way");
        }

        /* Rung 12k -- the band as a PICTURE, because 12j's numbers were being read three ways in one
         * conversation.  A bounding box says how far a colour reached; only a picture says whether it
         * reached as a triangle's hypotenuse or as the straight edge of a rect, and that distinction is
         * what separates "the interpolator never ran" from "the geometry landed wrong".  Each box gets
         * the corners it ASKED for on the header line, so the reader can hold the picture up against the
         * triangle it should be: v0 red at the box's left on the bottom row, v1 green at the right on the
         * same row, v2 blue at the apex on the band's first row.  `want` is what was sent, not what was
         * read back -- 12a's certification is that the packet left, so anything else on the picture is the
         * GS's answer to it.
         *
         * One character per FOUR pixels of x, one line per band row: a 128-px box is 32 columns and the
         * whole band prints in six headers and 96 rows.  The resolution is the compromise -- at one char
         * per pixel a 128-px line is 137 chars and wraps the 80-col console, and at 8 px/col a triangle's
         * 1-px-per-row hypotenuse disappears.  At 4 px a column is a span of 4 rows of the hypotenuse, so
         * a slope still reads as a slope. */
        {
            uint32_t b3, pc;
            char pic[GSLIVE_PIC_COLS + 1];

            ps2_kprintf("[GSLIVE] 12k mask: R red  G green  B blue  Y=R+G  M=R+B  C=G+B  W all three\n");
            ps2_kprintf("[GSLIVE] 12k f = field only; lowercase r g y b m c w = the same, in a cell\n");
            ps2_kprintf("[GSLIVE]     that also holds interpolated field ink (a ramp's edge)\n");
            ps2_kprintf("[GSLIVE] 12k '.' = nothing but the canvas here; a space = past this box\n");
            for (b3 = 0u; b3 < GSLIVE_SH_N; b3++) {
                const uint32_t painted = sh_tot[b3] - sh_same[b3];
                const char *kind = (painted == 0u) ? "NOTHING"
                                   : ((sh_other[b3] > painted / 4u) ? "INTERP" : "FLAT");
                const uint32_t cols = (s_sh_x1[b3] - s_sh_x0[b3]) >> GSLIVE_PIC_SHIFT;
                /* Rung 12n makes `want` mean what it always claimed to mean -- the corners that were SENT --
                 * by reading them off the ask rather than off the read window.  A box with no session ask
                 * falls back to its window, which is what every box was until this rung, so the five rows
                 * that are not dialled keep the header 12k printed. */
                const uint32_t ax0 = (s_sh_ask_x1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_x0[b3] : s_sh_x0[b3];
                const uint32_t ax1 = (s_sh_ask_x1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_x1[b3] : s_sh_x1[b3];
                const uint32_t ay0 = (s_sh_ask_y1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_y0[b3] : (uint32_t)GSLIVE_SH_Y0;
                const uint32_t ay1 = (s_sh_ask_y1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_y1[b3] - 1u
                                       : (uint32_t)GSLIVE_SH_Y1 - 1u;
                const int ask_moved = (ax0 != s_sh_x0[b3] || ax1 != s_sh_x1[b3]
                                        || ay0 != (uint32_t)GSLIVE_SH_Y0
                                        || ay1 != (uint32_t)GSLIVE_SH_Y1 - 1u);

                ps2_kprintf("[GSLIVE] 12k %c: x%u-%u %s  want R(%u,%u) G(%u,%u) B(%u,%u)\n",
                            s_sh_label[b3][0], (unsigned int)s_sh_x0[b3],
                            (unsigned int)s_sh_x1[b3] - 1u, kind,
                            (unsigned int)ax0, (unsigned int)ay1,
                            (unsigned int)ax1 - 1u, (unsigned int)ay1,
                            (unsigned int)((ax0 + ax1) / 2u), (unsigned int)ay0);
                if (ask_moved) {
                    ps2_kprintf("[GSLIVE] 12k %c: ASKED x%u-%u y%u-%u, READ at x%u-%u y%u-%u\n",
                                s_sh_label[b3][0], (unsigned int)ax0, (unsigned int)ax1 - 1u,
                                (unsigned int)ay0, (unsigned int)ay1,
                                (unsigned int)s_sh_x0[b3], (unsigned int)s_sh_x1[b3] - 1u,
                                (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
                }
                for (pr = 0u; pr < GSLIVE_PIC_ROWS; pr++) {
                    for (pc = 0u; pc < cols; pc++) {
                        const uint32_t m = s_band_pic[b3][pr][pc];

                        pic[pc] = (m & 15u) ? s_pic_chr[m & 15u]
                                            : ((m & GSLIVE_PIC_CANVAS) ? '.' : ' ');
                    }
                    pic[cols] = '\0';
                    ps2_kprintf("[GSLIVE] 12k %c y%u %s\n", s_sh_label[b3][0],
                                (unsigned int)(GSLIVE_SH_Y0 + pr), pic);
                }
            }
        }

        /* Rung 12l's own reading -- the SHAPE rows, because the colour law cannot see a fixed box B (a
         * flat triangle has no field, so it still grades FLAT) and 12k's pictures cannot be diffed
         * between runs by eye.  `top=` is painted px in the band's first row: 12k measured D with its
         * whole 88-px top edge laid, B with its whole 128, and A with exactly one pixel (the apex).
         * `bot=` is the last row, which 12k showed as the ramp boxes' unwritten base -- it is printed
         * because a rect that reaches y575 and a triangle that stops at y574 is itself a difference the
         * next rung may need. */
        {
            static const char *const v_tag[4] = {
                "PRE IS THE LATCH", "PRE FIXED D ONLY", "PRE FIXED B, NOT D", "PRE IS NOT THE VARIABLE"
            };
            static const char *const v_body[4][2] = {
                { "D and B are both triangles now: the register D's tag wrote",
                  "carried to the next PRE=1 group -- the latch is the cause" },
                { "only D moved: B kept its rect with TRI already in the",
                  "register, so B's flatness is IIP=0 or position, not PRE" },
                { "only B moved: D still laid its rect with its own PRIM",
                  "honoured, so PRE is not the front-of-frame variable" },
                { "neither moved -- but this rung cannot fire unless the inherited",
                  "PRIM differs from TRI+IIP, and 12m tests the tag field directly" }
            };
            uint32_t b4;
            const uint32_t a_at = s_sh_sess_first[0u], d_at = s_sh_sess_first[3u];
            const int d_tri = (sh_top[3u] <= 4u);
            const int b_tri = (sh_top[1u] <= 4u);
            const int case_i = (d_tri && b_tri) ? 0 : ((d_tri && !b_tri) ? 1
                                                   : ((!d_tri && b_tri) ? 2 : 3));
            /* Rung 12n retires this dial, and a retired dial must not keep voting: 12l's four-way answer
             * is only this run's answer if D went out patched AND asked at its whole window.  With either
             * changed, the shape rows above are 12n's evidence, not 12l's, and the verdict says SKIP. */
            const int armed = (s_pre0_box == 3u && s_prim_pt_box == GSLIVE_SH_N
                                && s_sh_ask_x0[3u] == (int32_t)s_sh_x0[3u]
                                && s_sh_ask_x1[3u] == (int32_t)s_sh_x1[3u]
                                && s_sh_ask_x0[1u] == (int32_t)s_sh_x0[1u]
                                && s_sh_ask_x1[1u] == (int32_t)s_sh_x1[1u]);

            ps2_kprintf("[GSLIVE] 12l dial %s: D's tag bit 46 PRE 1->0, so its own PRIM is\n",
                        armed ? "ARMED" : "OFF");
            ps2_kprintf("[GSLIVE]   honoured; ps2sdk gif_tags.h names that bit GIF_PRE_ENABLE =\n");
            ps2_kprintf("[GSLIVE]   \"Disable PRIM field output\", so a PRE=1 group draws under the\n");
            ps2_kprintf("[GSLIVE]   LAST tag's PRIM.  B and A keep PRE=1: B is the latch cell.\n");
            ps2_kprintf("[GSLIVE]   12k's baseline: D laid top=88/88, B laid top=128/128, A top=1.\n");
            ps2_kprintf("[GSLIVE] 12l shape (row %u = a rect's whole width, a triangle's 1-2 px):\n",
                        (unsigned int)GSLIVE_SH_Y0);
            for (b4 = 0u; b4 < GSLIVE_SH_N; b4++) {
                const uint32_t w = s_sh_x1[b4] - s_sh_x0[b4];
                const int kind = (sh_top[b4] <= 4u) ? 0 : ((sh_top[b4] * 2u >= w) ? 2 : 1);

                ps2_kprintf("[GSLIVE] 12l %c: top=%u/%u bot=%u/%u -> %s\n", s_sh_label[b4][0],
                            (unsigned int)sh_top[b4], (unsigned int)w,
                            (unsigned int)sh_bot[b4], (unsigned int)w, gslive_sh_kind[kind]);
            }
            if (a_at < GSLIVE_CAP_SENDS && d_at < GSLIVE_CAP_SENDS) {
                const uint64_t hx = s_cap_send[d_at][0] ^ s_cap_send[a_at][0];

                ps2_kprintf("[GSLIVE] 12l tag check: D PRE=%u PRIM=0x%02x, A PRE=%u PRIM=0x%02x\n",
                            (unsigned int)gslive_pre_of(s_cap_send[d_at]),
                            (unsigned int)gslive_prim_of(s_cap_send[d_at]),
                            (unsigned int)gslive_pre_of(s_cap_send[a_at]),
                            (unsigned int)gslive_prim_of(s_cap_send[a_at]));
                if (armed) {
                    ps2_kprintf("[GSLIVE] 12l tag xor: hi=0x%08x lo=0x%08x -> %s\n",
                                (unsigned int)(hx >> 32), (unsigned int)(hx & 0xFFFFFFFFu),
                                (hx == ((uint64_t)1u << 46)) ? "exactly bit 46, nothing else moved"
                                                             : "MORE THAN bit 46 moved");
                } else {
                    /* With the dial off, neither tag is patched and this xor says nothing about PRE -- it
                     * is the two boxes' ordinary difference, and printing it as though 12l had fired is how
                     * a retired rung got read as a live measurement once.  The check row above still stands:
                     * it is what certifies the dials really are off. */
                    ps2_kprintf("[GSLIVE] 12l tag xor: not printed -- the dial is off, so this xor is\n");
                    ps2_kprintf("[GSLIVE]   not 12l's evidence; D's PRE above just says unpatched\n");
                }
            } else {
                ps2_kprintf("[GSLIVE] 12l tag check: SKIP -- D's or A's group was not captured\n");
            }
            if (!armed) {
                ps2_kprintf("[GSLIVE] 12l verdict: SKIP -- 12l's dial is not the one armed here (D's\n");
                ps2_kprintf("[GSLIVE]   PRE patch is off, or its ask moved), so the shape rows above are\n");
                ps2_kprintf("[GSLIVE]   12n's evidence.  12l measured NO on 2026-10-18, conditionally.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12l verdict: %s\n", v_tag[case_i]);
                ps2_kprintf("[GSLIVE]   %s\n", v_body[case_i][0]);
                ps2_kprintf("[GSLIVE]   %s\n", v_body[case_i][1]);
            }
        }

        /* Rung 12m's reading, and the reason it exists is that 12l's verdict has a hole in it.  Clearing
         * PRE changes what a group draws only if the PRIM register the group would otherwise inherit
         * DIFFERS from the TRI+IIP its tag carries -- and no rung here ever read that register, so "PRE is
         * not the variable" is proven against a latch that may already have held the right value.  12m
         * closes the hole by making the tag's ask WRONG on purpose: box D goes out PRE=0 with its whole
         * PRIM field zeroed, which the SDK's own op table reads as POINT, flat, untextured, context 1, no
         * FIX.  A tag that asks for points and lays points means the field rules and 12l's block was laid
         * by TRI+IIP, so the flat boxes are a VERTEX or CLIP answer; a tag that asks for points and still
         * lays the same 88x16 block means no PRIM field of this group reaches the rasteriser, and the next
         * rung has to write the register itself.  Either way the diagnosis narrows, which is what the rung
         * was designed to buy.
         *
         * B is no longer a propagation cell, because 12m's answer does not depend on it: it is the control
         * that says the census has not moved, and if it stops being the RECT 12l measured then neither
         * verdict can be read from this run.  The verdict is also gated on the captured tag decoding as
         * patched, so a patch that never left the EE cannot come back as a measurement. */
        {
            static const char *const m_tag[3] = {
                "THE TAG'S PRIM FIELD RULES", "THE TAG'S PRIM FIELD IS IGNORED", "CONTROL MOVED: READ NOTHING"
            };
            static const char *const m_body[3][2] = {
                { "an ask for POINT laid points, so 12l's block WAS laid by TRI+IIP:",
                  "the flat boxes are a vertex/clip answer, not a primitive one" },
                { "an ask for POINT laid the same block, so this group's tag PRIM field",
                  "is not what the GS rasterises by -- write the register and re-read" },
                { "B is not the RECT that 12l measured, so this census is not 12l's and",
                  "neither answer above can be taken from these rows" }
            };
            const uint32_t wd = s_sh_x1[3u] - s_sh_x0[3u];
            const uint32_t wb = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t dm = s_sh_sess_first[3u], am = s_sh_sess_first[0u];
            const int have_dm = (dm < GSLIVE_CAP_SENDS && s_cap_send_words[dm] != 0u);
            const int have_am = (am < GSLIVE_CAP_SENDS && s_cap_send_words[am] != 0u);
            const uint32_t dpre = have_dm ? gslive_pre_of(s_cap_send[dm]) : 2u;
            const uint32_t dprim = have_dm ? gslive_prim_of(s_cap_send[dm]) : 0x7FFu;
            /* Two things must hold for this row to be 12m's answer: the tag that left the EE is the tag
             * 12m patched (measured, not assumed), AND the geometry the patched tag asked for is the box
             * the shape numbers are read across -- rung 12n narrows exactly that, so 12m's "same block"
             * comparison has to stop claiming once the ask and the window are different things. */
            const int prim_dial = (s_prim_pt_box == 3u);
            const int same_geo = (s_sh_ask_x0[3u] == (int32_t)s_sh_x0[3u]
                                   && s_sh_ask_x1[3u] == (int32_t)s_sh_x1[3u]);
            const int certified = (have_dm && dpre == 0u && dprim == 0u
                                    && prim_dial && same_geo);
            const int scattered = (sh_top[3u] * 2u <= wd);
            const int b_stable = (sh_top[1u] >= wb);
            const int case_m = !certified ? -1 : (!b_stable ? 2 : (scattered ? 0 : 1));
            uint32_t b5;

            ps2_kprintf("[GSLIVE] 12m dial %s: D's tag ALSO has PRIM (bits 47-57) zeroed, so the\n",
                        prim_dial ? "ARMED" : "OFF");
            ps2_kprintf("[GSLIVE]   authoritative ask in this frame is POINT -- flat, untextured,\n");
            ps2_kprintf("[GSLIVE]   ctx1, no FIX.  NLOOP, EOP, FLG, NREG, the REGS list and all six\n");
            ps2_kprintf("[GSLIVE]   vertex words are the recorder's own, unchanged from 12l.\n");
            ps2_kprintf("[GSLIVE]   12l's xor row now reads MORE THAN bit 46, and that is this field.\n");
            ps2_kprintf("[GSLIVE]   12l laid D as top=88/88 bot=88/88 painted=1408 nofield: the ask\n");
            ps2_kprintf("[GSLIVE]   to compare against is TRI+IIP -> block, POINT -> ?\n");
            ps2_kprintf("[GSLIVE] 12m shape and field, two words because one hid that C and E each\n");
            ps2_kprintf("[GSLIVE]   lay a solid rect AND a gradient triangle in the same box:\n");
            for (b5 = 0u; b5 < GSLIVE_SH_N; b5++) {
                const uint32_t w5 = s_sh_x1[b5] - s_sh_x0[b5];
                const uint32_t p5 = sh_tot[b5] - sh_same[b5];
                const int kind = (sh_top[b5] <= 4u) ? 0 : ((sh_top[b5] * 2u >= w5) ? 2 : 1);
                const int fld = (p5 != 0u && sh_other[b5] > p5 / 4u);

                ps2_kprintf("[GSLIVE] 12m %c: top=%u/%u painted=%u %s %s\n", s_sh_label[b5][0],
                            (unsigned int)sh_top[b5], (unsigned int)w5, (unsigned int)p5,
                            gslive_sh_kind[kind], fld ? "FIELD" : "nofield");
            }
            if (have_dm && have_am) {
                ps2_kprintf("[GSLIVE] 12m tag decode: D PRE=%u PRIM=0x%03x, A PRE=%u PRIM=0x%03x\n",
                            (unsigned int)dpre, (unsigned int)dprim,
                            (unsigned int)gslive_pre_of(s_cap_send[am]),
                            (unsigned int)gslive_prim_of(s_cap_send[am]));
            } else {
                ps2_kprintf("[GSLIVE] 12m tag decode: SKIP -- D's or A's group was not captured\n");
            }
            if (case_m < 0) {
                ps2_kprintf("[GSLIVE] 12m verdict: SKIP -- this run is not 12m's run: the dial is off or\n");
                ps2_kprintf("[GSLIVE]   the tag that left is not the tag it patched, or rung 12n moved the\n");
                ps2_kprintf("[GSLIVE]   geometry the shape rows measure.  12m measured IGNORED.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12m verdict: %s\n", m_tag[case_m]);
                ps2_kprintf("[GSLIVE]   %s\n", m_body[case_m][0]);
                ps2_kprintf("[GSLIVE]   %s\n", m_body[case_m][1]);
            }
        }

        /* Rung 12n's own reading -- WHOSE ink.  12l changed one bit of box D's tag and 12m changed the
         * tag's whole primitive ask, and both times the captured bytes proved the patch left the EE and D
         * laid the same 88x16 block, 1408 px, no field.  Two of the three things a tag can say about a
         * transfer have therefore been edited on purpose without moving the picture, which leaves exactly
         * one assumption untested since 12i: that the block in D's window is D's GROUP's ink.  It may
         * belong to another writer -- the poster that sets the environment, the AD stream, the desktop's
         * own upload -- and if so the ORDER/PLACE/SESSION grid of 12h and 12i and both of 12l/12m's
         * negatives were read off somebody else's pixels.
         *
         * The dial is the geometry: D is asked a 24-px triangle at the LEFT edge of its own 88-px window
         * and read across the whole window, so the ask and the ruler differ by coordinate and painted px
         * can be counted at an x that was never asked for.  Three readings, and the third one is the one
         * that costs a new rung either way:
         *   out=0 and ink present -- the ink moved with the ask, so the block IS this group's, and the
         *     flat full-width box is this group's coverage of its own bounding box;
         *   the 88-px top edge still laid -- the block held while the ask moved, so it is NOT this group's
         *     ink and the tag rungs have been grading a writer that was never the recorder;
         *   anything in between -- two writers in one box, and 12k's picture is the evidence.
         * B is 12m's control unchanged: it must still lay its whole 128-px top edge, or this census is not
         * 12k's census with one number changed and none of the readings above can be taken from it. */
        {
            static const char *const n_tag[5] = {
                "THE INK FOLLOWS THE ASK", "THE BLOCK HELD: NOT THIS GROUP'S INK",
                "CONTROL MOVED: READ NOTHING", "SPLIT: TWO WRITERS IN ONE BOX",
                "THE INK VANISHED WITH THE WIDE ASK"
            };
            static const char *const n_body[5][2] = {
                { "asked 24 px, laid only 24: box D's block is this recorder group's",
                  "rasterisation, and the flat box is its OWN bbox -- a setup answer" },
                { "asked 24 px, laid the same 88-px block: that ink is another writer's",
                  "and 12i-12m read their tag answers off pixels this group never laid" },
                { "B is not the RECT 12m measured, so the census itself moved and",
                  "nothing here can say whose pixels D's window holds" },
                { "some of the ink followed the ask and some of it stayed: two senders",
                  "share box D, and 12k's picture of it is what to read next" },
                { "the narrowed ask laid nothing where the wide ask laid 1408 px, so the",
                  "ask does rule -- and a wide ask is what turns it into a block" }
            };
            const uint32_t wd = s_sh_x1[3u] - s_sh_x0[3u];
            const uint32_t wb = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t pd = sh_tot[3u] - sh_same[3u];
            const uint32_t aw = (s_sh_ask_x1[3u] > s_sh_ask_x0[3u])
                                  ? (uint32_t)(s_sh_ask_x1[3u] - s_sh_ask_x0[3u]) : 0u;
            const int dialled = (aw != 0u && aw < wd);
            /* Rung 12o takes this box's ask out of the band, and 12n's law -- painted px at an x never
             * asked for, measured across the window the group was asked to fill -- only means what 12n
             * measured while the ask is still IN the band.  out= then reads 1408 simply because every band
             * row is a row that was never asked, which is 12o's evidence, not a split. */
            const int in_band = (s_sh_ask_y0[3u] == (int32_t)GSLIVE_SH_Y0
                                    && s_sh_ask_y1[3u] == (int32_t)GSLIVE_SH_Y1);
            const int b_stable = (sh_top[1u] >= wb);
            const int confined = (sh_outside[3u] == 0u);
            const int block_kept = (sh_top[3u] * 2u >= wd);
            int case_n;
            uint32_t b6;

            if (!dialled || !in_band) {
                case_n = -1;
            } else if (!b_stable) {
                case_n = 2;
            } else if (pd == 0u) {
                case_n = 4;
            } else if (confined) {
                case_n = 0;
            } else if (block_kept) {
                case_n = 1;
            } else {
                case_n = 3;
            }

            ps2_kprintf("[GSLIVE] 12n dial: box D is ASKED x%u-%u, %u px wide (apex %u), and READ\n",
                        (unsigned int)s_sh_ask_x0[3u], (unsigned int)s_sh_ask_x1[3u] - 1u,
                        (unsigned int)aw,
                        (unsigned int)(((uint32_t)s_sh_ask_x0[3u]
                                        + (uint32_t)s_sh_ask_x1[3u]) / 2u));
            ps2_kprintf("[GSLIVE]   across x%u-%u, %u px, because its window is the ruler.  Both tag\n",
                        (unsigned int)s_sh_x0[3u], (unsigned int)s_sh_x1[3u] - 1u, (unsigned int)wd);
            ps2_kprintf("[GSLIVE]   asked rows y%u-%u, band rows y%u-%u%s\n",
                        (unsigned int)s_sh_ask_y0[3u], (unsigned int)s_sh_ask_y1[3u] - 1u,
                        (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u,
                        in_band ? "" : "  <- 12o moved it");
            ps2_kprintf("[GSLIVE]   dials are OFF, so the ask is the one thing that moved since 12k,\n");
            ps2_kprintf("[GSLIVE]   which laid D top=88/%u painted=1408 RECT nofield at the wide ask.\n",
                        (unsigned int)wd);
            ps2_kprintf("[GSLIVE]   out= counts painted px in the window at an x never asked for; it\n");
            ps2_kprintf("[GSLIVE]   is 0 wherever ask == window, so only a dialled box can show it.\n");
            ps2_kprintf("[GSLIVE] 12n rows: ask span, painted px in the top row, painted px, out=:\n");
            for (b6 = 0u; b6 < GSLIVE_SH_N; b6++) {
                const uint32_t w6 = s_sh_x1[b6] - s_sh_x0[b6];
                const uint32_t p6 = sh_tot[b6] - sh_same[b6];
                const int kind = (sh_top[b6] <= 4u) ? 0 : ((sh_top[b6] * 2u >= w6) ? 2 : 1);
                const int fld = (p6 != 0u && sh_other[b6] > p6 / 4u);
                const uint32_t q0 = (s_sh_ask_x1[b6] != 0u)
                                      ? (uint32_t)s_sh_ask_x0[b6] : s_sh_x0[b6];
                const uint32_t q1 = (s_sh_ask_x1[b6] != 0u)
                                      ? (uint32_t)s_sh_ask_x1[b6] : s_sh_x1[b6];

                ps2_kprintf("[GSLIVE] 12n %c: ask %u-%u top=%u/%u px=%u out=%u %s %s\n",
                            s_sh_label[b6][0], (unsigned int)q0, (unsigned int)q1 - 1u,
                            (unsigned int)sh_top[b6], (unsigned int)w6, (unsigned int)p6,
                            (unsigned int)sh_outside[b6], gslive_sh_kind[kind],
                            fld ? "FIELD" : "nofield");
            }
            ps2_kprintf("[GSLIVE] 12n inside the ask: %u of D's %u painted px.  The asked bbox is\n",
                        (unsigned int)(pd - sh_outside[3u]), (unsigned int)pd);
            ps2_kprintf("[GSLIVE]   %u px (%u wide x %u rows): that many means the bbox was FILLED,\n",
                        (unsigned int)(aw * (uint32_t)GSLIVE_PIC_ROWS), (unsigned int)aw,
                        (unsigned int)GSLIVE_PIC_ROWS);
            ps2_kprintf("[GSLIVE]   half of it (%u px) means a triangle, 0 means the ink is elsewhere.\n",
                        (unsigned int)(aw * (uint32_t)GSLIVE_PIC_ROWS / 2u));
            if (case_n < 0) {
                ps2_kprintf("[GSLIVE] 12n verdict: SKIP -- this run does not hold 12n's condition (D is\n");
                ps2_kprintf("[GSLIVE]   asked across its whole window, or 12o moved its ask out of the\n");
                ps2_kprintf("[GSLIVE]   band), so out= is 12o's reading, not a split.  12n measured\n");
                ps2_kprintf("[GSLIVE]   THE BLOCK HELD on 2026-10-18, eight censuses, both runs.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12n verdict: %s\n", n_tag[case_n]);
                ps2_kprintf("[GSLIVE]   %s\n", n_body[case_n][0]);
                ps2_kprintf("[GSLIVE]   %s\n", n_body[case_n][1]);
            }
        }

        /* Rung 12o's own reading -- WHERE this group's ink goes, and whether the ask is even the ask.
         *
         * 12n narrowed D's ask to 24 px at the left of its own 88-px window and the block stayed
         * byte-for-byte the same, so a group's geometry joined PRE (12l) and its PRIM field (12m) as a third
         * input that does not move this picture.  Three answers of "no" in a row are a suspicious coincidence
         * unless the thing being measured has one property none of the rungs tested: it was never this
         * group's to move.  So this rung stops dialling inside the band and moves D's ask OUT of it -- the
         * same 24x16 triangle, 16 rows lower, at rows the band branch never reads -- and asks two questions
         * that can now be answered separately: did the band's block go away when the group left the band, and
         * did ink arrive inside the rect the packet asked for?  The four combinations are four different
         * diagnoses, which is why both halves are needed rather than the one 12n read.
         *
         * The third row is the rung's insurance: the captured group's own XYZ2 words are decoded by the
         * inverse of the encoder this file uses, so "the ask" is certified as bytes that left the EE rather
         * than as a C argument.  If the decoded corners differ from the ask, the geometry was rewritten
         * between the session and the packet and no verdict below can be taken from this run -- that itself
         * would be the finding, and it is why the rows print both. */
        {
            static const char *const o_tag[5] = {
                "Y IS HONOURED: BAND EMPTIED, INK LANDED",
                "TWO WRITERS: INK LANDED, BAND BLOCK STAYED",
                "THIS GROUP DRAWS NOTHING: no ink landed",
                "BAND EMPTIED, NO INK: read 12j's spill bbox",
                "CONTROL MOVED: READ NOTHING"
            };
            static const char *const o_body[5][2] = {
                { "the block followed the ask in y and not in x, so an x-specific",
                  "override (clip, clamp, re-derived x) is what 12n was hiding" },
                { "this group's ink is in the asked rect and the band block is a",
                  "different writer's: the census graded a stranger's pixels" },
                { "nothing landed in the asked rect and the band is unchanged: no",
                  "vertex of this group rasterises, so its stream is the dead one" },
                { "the ask rules and the answer is emptiness -- the ink went",
                  "somewhere this rung did not name; 12j's bbox is next" },
                { "B is not the RECT 12k measured, so the census moved and",
                  "neither reading above can be attributed to this dial" }
            };
            const uint32_t od = s_sh_sess_first[3u];
            const int have_d = (od < GSLIVE_CAP_SENDS && s_cap_send_words[od] >= 8u);
            const int nreg_d = have_d ? (int)gslive_nreg_of(s_cap_send[od]) : 0;
            const int32_t vx0 = have_d ? gslive_xyz_axis(s_cap_send[od][3], 0) : -2048;
            const int32_t vy0 = have_d ? gslive_xyz_axis(s_cap_send[od][3], 1) : -2048;
            const int32_t vx1 = have_d ? gslive_xyz_axis(s_cap_send[od][5], 0) : -2048;
            const int32_t vy1 = have_d ? gslive_xyz_axis(s_cap_send[od][5], 1) : -2048;
            const int32_t vx2 = have_d ? gslive_xyz_axis(s_cap_send[od][7], 0) : -2048;
            const int32_t vy2 = have_d ? gslive_xyz_axis(s_cap_send[od][7], 1) : -2048;
            const int sent_ok = (have_d && nreg_d == 2
                                    && vx0 == s_sh_ask_x0[3u]
                                    && vy0 == s_sh_ask_y1[3u] - 1
                                    && vx1 == s_sh_ask_x1[3u] - 1
                                    && vy1 == s_sh_ask_y1[3u] - 1
                                    && vx2 == (s_sh_ask_x0[3u] + s_sh_ask_x1[3u]) / 2
                                    && vy2 == s_sh_ask_y0[3u]);
            const uint32_t wb_o = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t pd_o = sh_tot[3u] - sh_same[3u];
            const int dialled = (mv_box < GSLIVE_SH_N);
            const int b_stable = (sh_top[1u] >= wb_o);
            const int block_gone = (pd_o == 0u);
            const int ink_found = (mv_painted != 0u);
            int case_o;

            if (!dialled || !sent_ok) case_o = -1;
            else if (!b_stable) case_o = 4;
            else if (block_gone && ink_found) case_o = 0;
            else if (ink_found) case_o = 1;
            else if (!block_gone) case_o = 2;
            else case_o = 3;

            ps2_kprintf("[GSLIVE] 12o dial: D's ask is x%u-%u y%u-%u -- below the band (y%u-%u),\n",
                        (unsigned int)s_sh_ask_x0[3u], (unsigned int)s_sh_ask_x1[3u] - 1u,
                        (unsigned int)s_sh_ask_y0[3u], (unsigned int)s_sh_ask_y1[3u] - 1u,
                        (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   inside the spill window (%u-%u), same 24x16 triangle as 12n, and\n",
                        (unsigned int)GSLIVE_SP_Y0, (unsigned int)GSLIVE_SP_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   both tag dials off.  Two questions: did the band block go, and did\n");
            ps2_kprintf("[GSLIVE]   ink arrive in the asked rect.  12n's baseline: painted=1408 RECT.\n");
            ps2_kprintf("[GSLIVE] 12o packet: D's group NREG=%u words=%u; XYZ2 at 3/5/7 decoded as\n",
                        (unsigned int)nreg_d,
                        (unsigned int)(have_d ? s_cap_send_words[od] : 0u));
            ps2_kprintf("[GSLIVE]   (field >> 4) - 2048, the encoder's own law inverted:\n");
            ps2_kprintf("[GSLIVE] 12o got v0=(%d,%d) v1=(%d,%d) v2=(%d,%d)\n",
                        (int)vx0, (int)vy0, (int)vx1, (int)vy1, (int)vx2, (int)vy2);
            ps2_kprintf("[GSLIVE] 12o ask v0=(%d,%d) v1=(%d,%d) v2=(%d,%d) -> %s\n",
                        (int)s_sh_ask_x0[3u], (int)s_sh_ask_y1[3u] - 1,
                        (int)s_sh_ask_x1[3u] - 1, (int)s_sh_ask_y1[3u] - 1,
                        (int)((s_sh_ask_x0[3u] + s_sh_ask_x1[3u]) / 2), (int)s_sh_ask_y0[3u],
                        sent_ok ? "PACKET == ASK" : "PACKET DIFFERS: SKIP");
            ps2_kprintf("[GSLIVE] 12o band D: top=%u/%u painted=%u out=%u  <- 12n laid 1408 here\n",
                        (unsigned int)sh_top[3u], (unsigned int)(s_sh_x1[3u] - s_sh_x0[3u]),
                        (unsigned int)pd_o, (unsigned int)sh_outside[3u]);
            if (dialled) {
                const uint32_t mv_area = ((uint32_t)(s_sh_ask_x1[mv_box] - s_sh_ask_x0[mv_box]))
                                       * ((uint32_t)(s_sh_ask_y1[mv_box] - s_sh_ask_y0[mv_box]));

                ps2_kprintf("[GSLIVE] 12o moved %c: tot=%u/%u painted=%u R=%u G=%u B=%u field=%u\n",
                            s_sh_label[mv_box][0], (unsigned int)mv_tot, (unsigned int)mv_area,
                            (unsigned int)mv_painted, (unsigned int)mv_r, (unsigned int)mv_g,
                            (unsigned int)mv_b, (unsigned int)mv_other);
                ps2_kprintf("[GSLIVE]   same-as-canvas there: %u of %u px (ink %u + field %u)\n",
                            (unsigned int)mv_same, (unsigned int)mv_tot,
                            (unsigned int)(mv_r + mv_g + mv_b), (unsigned int)mv_other);
                if (mv_painted != 0u) {
                    ps2_kprintf("[GSLIVE] 12o moved ink bbox: x%u-%u y%u-%u\n",
                                (unsigned int)mv_bbox[0], (unsigned int)mv_bbox[1],
                                (unsigned int)mv_bbox[2], (unsigned int)mv_bbox[3]);
                } else {
                    ps2_kprintf("[GSLIVE] 12o moved ink bbox: none -- every px equals its canvas\n");
                }
            } else {
                ps2_kprintf("[GSLIVE] 12o moved: SKIP -- no box was asked outside the band this run\n");
            }
            ps2_kprintf("[GSLIVE] 12o control B: top=%u/%u painted=%u (12k: 128/128, 1920)\n",
                        (unsigned int)sh_top[1u], (unsigned int)wb_o,
                        (unsigned int)(sh_tot[1u] - sh_same[1u]));
            if (case_o < 0) {
                ps2_kprintf("[GSLIVE] 12o verdict: SKIP -- either nothing was asked below the band, or\n");
                ps2_kprintf("[GSLIVE]   D's packet is missing/too short/NREG!=2, or its decoded corners\n");
                ps2_kprintf("[GSLIVE]   are not the ask (the row above says which).\n");
            } else {
                ps2_kprintf("[GSLIVE] 12o verdict: %s\n", o_tag[case_o]);
                ps2_kprintf("[GSLIVE]   %s\n", o_body[case_o][0]);
                ps2_kprintf("[GSLIVE]   %s\n", o_body[case_o][1]);
            }
        }

        /* Rung 12p: the SAME BYTES, WITHHELD.  Every rung from 12i to 12o has edited something the transfer
         * says -- PRE (12l), its PRIM field (12m), its x ask (12n), its y ask (12o) -- and the census has
         * never asked the one question that separates "this group's picture" from "a picture in this
         * group's neighbourhood": does the group reach the GS at all?  12o's answer makes that question
         * urgent twice over.  It found 384 red px filling D's moved 24x16 ask -- which it could only call
         * "ink where the packet asked", not "ink THIS packet laid" -- and it found the control box B change
         * shape, eight times in a row and identically in both pinned runs, for a dial that named nothing
         * about B.
         *
         * B's own picture says what moved, and it is not the pair of shapes 12l/12n's counts suggested: the
         * blue rect is INVARIANT at 945 px (x320-382, rows 560-574 -- 12j's "exactly 63x15", a foreign
         * writer) and only B's red moved.  With D's ask in the band the red is the left half of rows
         * 560-574 (975 px); with D's ask 16 rows lower the red is row 575 alone, full width (128 px).  So a
         * neighbour's y rewrote which scanlines of this triangle rasterised, which is either a real
         * cross-group coupling or an artifact of the instrument -- opposite cures, and nothing in 12o can
         * tell them apart.
         *
         * So this rung changes the delivery and nothing else: D's session still opens, its group is still
         * captured and decoded, and the sink returns before `ps2_gs_gif_send()`, so the GIF receives two of
         * the census's three recorder groups and the page never sees D's bytes.  Two questions, both answerable
         * from one page: did anything survive at the two places 12o read (the band's block and the moved
         * rect), and did B's red come back?  They are printed as two verdicts, because they are two
         * measurements -- an ink answer says who laid a picture, and a B answer says whether one transfer's
         * DELIVERY can move another's raster, or whether the EE-side recorder state did it before the sink
         * ever saw a byte.  Both are graded only when the dial is armed and the sink held exactly one group:
         * a run that withheld nothing, or withheld three, is not this rung's run. */
        {
            static const char *const p_tag[4] = {
                "BOTH VANISH: ONE AUTHOR WAS D",
                "BLOCK WENT, INK STAYED: D DRAWS ITS ASK",
                "INK WENT, BLOCK STAYED: 12n CONFIRMED",
                "NEITHER IS D'S: STRANGERS EVERYWHERE"
            };
            static const char *const p_body[4][2] = {
                { "with D's bytes gone, the band's 1408 px and the moved 384 are both",
                  "empty: both pictures were this transfer's, so 12n graded its own ink" },
                { "the band block empties with the bytes and the moved rect still",
                  "fills: the ink follows the ask, and 12n's HELD was a coincidence" },
                { "the moved rect empties -- those 384 px WERE this transfer's --",
                  "while the band block stands, so that block is a foreign writer's" },
                { "both pictures stand without this group's bytes: band block AND",
                  "moved ink are other writers', and no per-box row is a recorder row" }
            };
            static const char *const p_btag[3] = {
                "B'S RED IS BACK (975): D'S BYTES MOVED IT",
                "B STAYS RED-LESS: NOT THE GIF",
                "B IS A THIRD STATE: READ THE PICTURE"
            };
            static const char *const p_bbody[3][2] = {
                { "the only difference is whether D's words reached the GS, and B's left",
                  "half returns: one transfer's delivery rules another's scanlines" },
                { "B still has 128 red on row 575 without D on the bus, so the coupling",
                  "is EE-side (the recorder's own state) or a foreign writer, not the GIF" },
                { "B matches neither 975 nor 128 red, so this run put the page somewhere",
                  "no rung has described; 12k's picture of B is the row to read next" }
            };
            const uint32_t od_p = s_sh_sess_first[3u];
            const int captured = (od_p < GSLIVE_CAP_SENDS && s_cap_send_words[od_p] >= 8u);
            /* Rung 12v: a session is now two groups, so withholding D's packet withholds two transfers.
             * A run that held one of the two has D's chain without its PRIM write, which is 12u's V0, not
             * 12p's dial -- so the law counts 2 and says so rather than grading a different experiment. */
            const int armed_p = (s_hold_gif_mask == (1u << 3) && s_sh_held == 2u);
            const uint32_t bp_p = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t pd_p = sh_tot[3u] - sh_same[3u];
            const uint32_t pb_p = sh_tot[1u] - sh_same[1u];
            const int band_kept = (pd_p != 0u);
            const int ink_kept = (mv_painted != 0u);
            /* B is classified by its OWN red count and not by `painted=`, because the blue 945 is a foreign
             * rect in both baselines: red=975 is 12n/12k's B and red=128 is 12o's, and either of them can
             * coexist with any blue.  A third number is a third state and says so rather than being read
             * as one of the two. */
            const int b_state = (sh_r[1u] == 975u) ? 0
                              : ((sh_r[1u] == 128u && pb_p == 1073u) ? 1 : 2);
            int case_p;

            ps2_kprintf("[GSLIVE] 12p dial: D's group is captured and decoded, and NOT handed to the\n");
            ps2_kprintf("[GSLIVE]   GIF.  ask x%u-%u y%u-%u, held=%u, to the bus=%u group(s).\n",
                        (unsigned int)s_sh_ask_x0[3u], (unsigned int)s_sh_ask_x1[3u] - 1u,
                        (unsigned int)s_sh_ask_y0[3u], (unsigned int)s_sh_ask_y1[3u] - 1u,
                        (unsigned int)s_sh_held, (unsigned int)s_sh_groups);
            ps2_kprintf("[GSLIVE]   12o, with those bytes SENT: band 1408, moved ink 384 red,\n");
            ps2_kprintf("[GSLIVE]     B red=975 (left half, rows 560-574) -> red=128 (row 575).\n");
            ps2_kprintf("[GSLIVE] 12p band D: top=%u/%u painted=%u   moved %c: tot=%u painted=%u\n",
                        (unsigned int)sh_top[3u], (unsigned int)(s_sh_x1[3u] - s_sh_x0[3u]),
                        (unsigned int)pd_p, s_sh_label[mv_box < GSLIVE_SH_N ? mv_box : 3u][0],
                        (unsigned int)mv_tot, (unsigned int)mv_painted);
            ps2_kprintf("[GSLIVE]   moved ink R=%u G=%u B=%u field=%u bbox x%u-%u y%u-%u\n",
                        (unsigned int)mv_r, (unsigned int)mv_g, (unsigned int)mv_b,
                        (unsigned int)mv_other,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[0] : 0u,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[1] : 0u,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[2] : 0u,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[3] : 0u);
            ps2_kprintf("[GSLIVE] 12p control B: top=%u/%u painted=%u red=%u blue=%u  (12n 975,\n",
                        (unsigned int)sh_top[1u], (unsigned int)bp_p, (unsigned int)pb_p,
                        (unsigned int)sh_r[1u], (unsigned int)sh_b[1u]);
            ps2_kprintf("[GSLIVE]   12o 128; the blue 945 is a foreign rect in both)\n");
            if (!armed_p || !captured) {
                ps2_kprintf("[GSLIVE] 12p verdict: SKIP -- the dial is %s, the sink held %u group(s)\n",
                            ((s_hold_gif_mask & (1u << 3)) != 0u) ? "D-armed" : "NOT D-armed",
                            (unsigned int)s_sh_held);
                ps2_kprintf("[GSLIVE]   of D's %u, and its packet %s.  This is not 12p's run.\n",
                            (unsigned int)s_sh_sess_groups[3u],
                            captured ? "is captured" : "is missing or short");
            } else {
                case_p = (band_kept ? 2 : 0) + (ink_kept ? 1 : 0);
                ps2_kprintf("[GSLIVE] 12p verdict INK: %s\n", p_tag[case_p]);
                ps2_kprintf("[GSLIVE]   %s\n", p_body[case_p][0]);
                ps2_kprintf("[GSLIVE]   %s\n", p_body[case_p][1]);
                ps2_kprintf("[GSLIVE] 12p verdict B: %s\n", p_btag[b_state]);
                ps2_kprintf("[GSLIVE]   %s\n", p_bbody[b_state][0]);
                ps2_kprintf("[GSLIVE]   %s\n", p_bbody[b_state][1]);
            }
        }

        /* Rung 12q: the mask, because 12p answered "nobody" and left a worse question behind.  Withholding
         * ONE transfer's bytes emptied a box whose own transfer went out -- B, from `painted=1073` (its red
         * left half plus the foreign blue rect) to `painted=0`, blue included -- while the two windows that
         * were supposedly B's neighbour, D's band block and D's moved rect, kept their 12o counts exactly
         * (1408 and 384).  A per-box reading of this census cannot survive that pair of facts: it means the
         * number in one box's slot is answered by a different box's delivery.  So this rung holds the mask
         * to ALL THREE recorder sessions (bits 0, 1 and 3: A, B and D) and asks the only question that can
         * re-base the instrument: is there ANY census ink left when no census triangle reaches the GS?
         * The hand shapes C/E/F do not pass through this sink, so they are the within-frame witness -- C
         * moved between 12n and 12o with nothing but D's ask changed, which is why it is read here rather
         * than assumed constant.  The law is per box against this page's OWN 12o baselines (A 891, B 1073
         * with red 128, D's band 1408, D's moved rect 384): four STAND says every rung-12 count has been
         * the app's stream and the census must be re-based on a window the app never paints; four FELL says
         * the band really is this file's ink and the cross-box coupling of 12p is a GS-side mechanism worth
         * chasing. Anything in between names itself. */
        {
            static const char *const q_tag[5] = {
                "ALL FELL: THE BAND IS THIS FILE'S INK",
                "1 OF 4 STOOD: ONE WINDOW IS NOT OURS",
                "SPLIT: D STANDS, A AND B FALL",
                "3 OF 4 STOOD: ALMOST NONE OF IT IS OURS",
                "ALL STOOD: NO CENSUS TRIANGLE RASTERISED"
            };
            static const char *const q_body[5][2] = {
                { "every one of the four empties with its transfer, so 12a-on's",
                  "counts really were this census's own rasterisation" },
                { "one window keeps its 12o number with no bytes and three do",
                  "not, so that one is the foreign writer -- the rows name it" },
                { "the two D windows stand while A and B empty: the instrument",
                  "works, and D's 1408-px block is another writer's ink" },
                { "three of the four stand with nothing sent: at most one box",
                  "here is ours, and every law built on the others graded ink" },
                { "all three sessions withheld and all four windows stand, so",
                  "rung 12 never measured its own ink -- re-base the read" }
            };
            const uint32_t qa = sh_tot[0u] - sh_same[0u];
            const uint32_t qb = sh_tot[1u] - sh_same[1u];
            const uint32_t qc = sh_tot[2u] - sh_same[2u];
            const uint32_t qe = sh_tot[4u] - sh_same[4u];
            const uint32_t qf = sh_tot[5u] - sh_same[5u];
            const uint32_t qd = sh_tot[3u] - sh_same[3u];
            const int armed_q = (s_hold_gif_mask == ((1u << 0) | (1u << 1) | (1u << 3)) &&
                                 s_sh_held == 6u);   /* 12v: three sessions, two groups each */
            const int a_stands = (qa == 891u);
            const int b_stands = (qb == 1073u);
            const int d_stands = (qd == 1408u);
            const int m_stands = (mv_painted == 384u);
            const int stand_count = a_stands + b_stands + d_stands + m_stands;

            ps2_kprintf("[GSLIVE] 12q dial: mask=0x%x, held=%u, to the bus=%u group(s); the hand\n",
                        (unsigned int)s_hold_gif_mask, (unsigned int)s_sh_held,
                        (unsigned int)s_sh_groups);
            ps2_kprintf("[GSLIVE]   shapes C/E/F and the magenta control still go out.  12o, all SENT:\n");
            ps2_kprintf("[GSLIVE]   A 891, B 1073 (red 128), D band 1408, D moved 384, C 1221.\n");
            if (!armed_q) {
                ps2_kprintf("[GSLIVE] 12q verdict: SKIP -- mask=0x%x held %u of %u session group(s),\n",
                            (unsigned int)s_hold_gif_mask, (unsigned int)s_sh_held,
                            (unsigned int)(s_sh_groups + s_sh_held));
                ps2_kprintf("[GSLIVE]   %u send(s) captured.  This is not 12q's run.\n",
                            (unsigned int)s_cap_sends);
            } else {
                ps2_kprintf("[GSLIVE] 12q A band: painted=%u, 12o 891 -> %s\n",
                            (unsigned int)qa, a_stands ? "STOOD" : "FELL");
                ps2_kprintf("[GSLIVE] 12q B band: painted=%u, 12o 1073 -> %s (red %u)\n",
                            (unsigned int)qb, b_stands ? "STOOD" : "FELL",
                            (unsigned int)sh_r[1u]);
                ps2_kprintf("[GSLIVE] 12q D band: painted=%u, 12o 1408 -> %s\n",
                            (unsigned int)qd, d_stands ? "STOOD" : "FELL");
                ps2_kprintf("[GSLIVE] 12q D moved: painted=%u, 12o 384 -> %s (red %u)\n",
                            (unsigned int)mv_painted, m_stands ? "STOOD" : "FELL",
                            (unsigned int)mv_r);
                ps2_kprintf("[GSLIVE] 12q witness C: painted=%u, 12o 1221 -> %s\n",
                            (unsigned int)qc, (qc == 1221u) ? "SAME" : "MOVED");
                ps2_kprintf("[GSLIVE]   E: %u vs 1145 %s   F: %u vs 442 %s\n",
                            (unsigned int)qe, (qe == 1145u) ? "same" : "moved",
                            (unsigned int)qf, (qf == 442u) ? "same" : "moved");
                ps2_kprintf("[GSLIVE] 12q verdict: %d of 4 stand -- %s\n",
                            stand_count, q_tag[stand_count]);
                ps2_kprintf("[GSLIVE]   %s\n", q_body[stand_count][0]);
                ps2_kprintf("[GSLIVE]   %s\n", q_body[stand_count][1]);
            }
        }

        /* Rung 12s: the two pre-draw reads, printed as the CONTROL on the finding rather than as a verdict
         * about D.  12r's first look was real -- the magenta control came back empty -- and its conclusion was
         * still wrong: the block it found in D is the desktop's own Colour Test Bar (the note at GSLIVE_X0 has
         * the arithmetic), and the corner-word key counted that chrome as ink because it tests colours BEFORE
         * the canvas compare.  So this rung reads the strip the instrument now lives in AND the strip it used
         * to live in, and asks each of them the same question: where the page holds a corner word, is it the
         * canvas's own word?  Where the answer is yes the ink is chrome and no rung may claim it; where it is
         * no, a GS-side writer really does exist and 12s has not named it either.
         *
         * The pass number WAS the thing that made that reading decidable -- the census re-runs on every app
         * open, VRAM is not cleared between opens, and from pass 2 onward a dirty window is equally this
         * file's own triangle left over from pass 1, so only census #1 could carry a verdict.  Rung 12t took
         * the limitation away by putting the band back to its canvas before the look (see the upload above),
         * which makes every census's first look a real one and lets the verdict be graded on the censuses that
         * run the grid instead of on whichever census happened to be first.  The old strip at 560..575 is NOT
         * uploaded, so its page-vs-canvas agreement stays a measurement of the desktop rather than a restatement
         * of this file's own copy. */
        {
            static const char *const s_tag[4] = {
                "BAND CLEAN, OLD BLOCK IS THE DESKTOP'S OWN COLOUR BAR",
                "BAND CLEAN, A GS-SIDE WRITER STAYS UNNAMED",
                "BAND CLEAN, OLD STRIP EMPTY TOO",
                "THE NEW BAND IS NOT CLEAN EITHER"
            };
            static const char *const s_body[4][2] = {
                { "the six band windows hold nothing before the draws, and the old strip's",
                  "words ARE the canvas's -- 12a..12r graded the colour bar, not the GS" },
                { "the band is clean, but the old strip holds corner words the canvas does",
                  "not hold at those positions: a GS-side writer is still unnamed" },
                { "the band is clean and the old strip is empty, so every block the rungs",
                  "saw came from THIS census's draws and chrome was never the answer" },
                { "12t's canvas upload did NOT take: a band window still disagrees with its own",
                  "canvas, or came back full of sentinels, before this census sent anything" }
            };
            uint32_t post[GSLIVE_SH_N], pre_paint[GSLIVE_SH_N];
            uint32_t band_dirty = 0u, sent_band = 0u, sent_old = 0u;
            uint32_t band_canvas = 0u, old_page = 0u, old_canvas = 0u;
            uint32_t old_match = 0u, old_tot = 0u;
            int k_s, b_s, case_s;

            for (k_s = 0; k_s < (int)GSLIVE_SH_N; k_s++) {
                post[k_s] = sh_tot[k_s] - sh_same[k_s];
                pre_paint[k_s] = s_pre[k_s].tot - s_pre[k_s].same;
                if (pre_paint[k_s] != 0u || s_pre[k_s].sent != 0u) band_dirty++;
                sent_band += s_pre[k_s].sent;
                sent_old += s_pre_old[k_s].sent;
                band_canvas += s_pre[k_s].cv_r + s_pre[k_s].cv_g + s_pre[k_s].cv_b;
                old_page += s_pre_old[k_s].r + s_pre_old[k_s].g + s_pre_old[k_s].b;
                old_canvas += s_pre_old[k_s].cv_r + s_pre_old[k_s].cv_g + s_pre_old[k_s].cv_b;
                old_match += s_pre_old[k_s].match;
                old_tot += s_pre_old[k_s].tot;
            }
            ps2_kprintf("[GSLIVE] 12s pre-draw: the band (rows %u..%u) and the old strip\n",
                        (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   (rows %u..%u) read with the GIF drained BEFORE this census\n",
                        (unsigned int)GSLIVE_OLD_BAND_Y0,
                        (unsigned int)(GSLIVE_OLD_BAND_Y0 + GSLIVE_PIC_ROWS - 1u));
            ps2_kprintf("[GSLIVE]   sent anything: census #%u of this process, band=%s old=%s\n",
                        (unsigned int)s_pre_pass, s_pre_ok ? "OK" : "FAILED",
                        s_pre_old_ok ? "OK" : "FAILED");
            ps2_kprintf("[GSLIVE]   words never filled: band=%u old=%u\n",
                        (unsigned int)sent_band, (unsigned int)sent_old);
            ps2_kprintf("[GSLIVE] 12s self-check: control x%u-%u y%u-%u held magenta=%u\n",
                        (unsigned int)GSLIVE_X0, (unsigned int)GSLIVE_X1 - 1u,
                        (unsigned int)GSLIVE_Y0, (unsigned int)GSLIVE_Y1 - 1u,
                        (unsigned int)s_pre_ctrl_mag);
            ps2_kprintf("[GSLIVE]   painted=%u BEFORE its only sender ran: magenta>0 means this read\n",
                        (unsigned int)s_pre_ctrl_paint);
            ps2_kprintf("[GSLIVE]   is not a first look, and it voids every number below.\n");
            if (!s_pre_ok || !s_pre_old_ok) {
                ps2_kprintf("[GSLIVE] 12s verdict: SKIP -- %s did not come back GS_READ_BUS_OK,\n",
                            (!s_pre_ok) ? "the band read" : "the old-strip read");
                ps2_kprintf("[GSLIVE]   so there is no first look to grade and no chrome to name.\n");
            } else if (s_pre_ctrl_mag != 0u) {
                ps2_kprintf("[GSLIVE] 12s verdict: VOID -- %u of the control's own %u px were already\n",
                            (unsigned int)s_pre_ctrl_mag,
                            (unsigned int)((GSLIVE_X1 - GSLIVE_X0) * GSLIVE_PIC_ROWS));
                ps2_kprintf("[GSLIVE]   magenta before the control was sent, so this read saw the page\n");
                ps2_kprintf("[GSLIVE]   AFTER the draws.  Nothing on this page is attributable.\n");
            } else {
                ps2_kprintf("[GSLIVE]   band pre/post painted: A %u/%u  B %u/%u  C %u/%u\n",
                            (unsigned int)pre_paint[0u], (unsigned int)post[0u],
                            (unsigned int)pre_paint[1u], (unsigned int)post[1u],
                            (unsigned int)pre_paint[2u], (unsigned int)post[2u]);
                ps2_kprintf("[GSLIVE]                    D %u/%u  E %u/%u  F %u/%u\n",
                            (unsigned int)pre_paint[3u], (unsigned int)post[3u],
                            (unsigned int)pre_paint[4u], (unsigned int)post[4u],
                            (unsigned int)pre_paint[5u], (unsigned int)post[5u]);
                ps2_kprintf("[GSLIVE]   the band's CANVAS holds %u corner words across the six\n",
                            (unsigned int)band_canvas);
                ps2_kprintf("[GSLIVE]   windows (0 = the post-draw `same` column cannot be chrome).\n");
                for (b_s = 0; b_s < (int)GSLIVE_SH_N; b_s++) {
                    ps2_kprintf("[GSLIVE] 12s old %c: page r/g/b=%u/%u/%u canvas r/g/b=%u/%u/%u\n",
                                s_sh_label[b_s][0],
                                (unsigned int)s_pre_old[b_s].r, (unsigned int)s_pre_old[b_s].g,
                                (unsigned int)s_pre_old[b_s].b,
                                (unsigned int)s_pre_old[b_s].cv_r, (unsigned int)s_pre_old[b_s].cv_g,
                                (unsigned int)s_pre_old[b_s].cv_b);
                    ps2_kprintf("[GSLIVE]        %c: match=%u/%u field=%u top=%u/%u\n",
                                s_sh_label[b_s][0], (unsigned int)s_pre_old[b_s].match,
                                (unsigned int)s_pre_old[b_s].tot,
                                (unsigned int)s_pre_old[b_s].other,
                                (unsigned int)s_pre_old[b_s].top,
                                (unsigned int)(s_sh_x1[b_s] - s_sh_x0[b_s]));
                }
                if (s_census_only < GSLIVE_SH_N) {
                    ps2_kprintf("[GSLIVE] 12s verdict: SKIP for the grid -- census #%u runs 12t's LONE mode,\n",
                                (unsigned int)s_pre_pass);
                    ps2_kprintf("[GSLIVE]   box D's session is the only send in it, so the six pre/post pairs\n");
                    ps2_kprintf("[GSLIVE]   describe one box and five windows nobody drew in.  The cleanliness\n");
                    ps2_kprintf("[GSLIVE]   still counts: band_dirty=%u of 6 is 12t's upload checking itself.\n",
                                (unsigned int)band_dirty);
                } else {
                    case_s = (band_dirty != 0u) ? 3
                           : (old_page == 0u) ? 2
                           : ((old_match == old_tot && old_page == old_canvas) ? 0 : 1);
                    ps2_kprintf("[GSLIVE] 12s verdict: %s\n", s_tag[case_s]);
                    ps2_kprintf("[GSLIVE]   %s\n", s_body[case_s][0]);
                    ps2_kprintf("[GSLIVE]   %s\n", s_body[case_s][1]);
                    if (case_s != 3) {
                        ps2_kprintf("[GSLIVE]   the shading grid therefore grades this census's own ink:\n");
                        ps2_kprintf("[GSLIVE]   read 12m's INTERPOLATES/FLAT and 12l's shape word per box.\n");
                    }
                }
            }
        }

        /* Rung 12t: one send on the bus, one look at the page.
         *
         * 12s moved the instrument onto a strip this census can certify empty, and what it certified is the
         * sharpest pair in the lane.  Box A's recorder group -- sent LAST, behind the hand chain -- lays 891 px
         * whose every cell reads `f`, an apex-up triangle with a real gradient in it (12k's picture, 12m's
         * INTERPOLATES).  Box D's group -- sent FIRST, and captured by 12i as differing from A's in exactly three
         * position words and no colour word -- lays 660 px of pure GSQUAD_RED: `other=0`, no ramp anywhere, and
         * an apex row holding 44 of 88 columns.  A triangle asked with its apex at the TOP does not have a 44-px
         * top row, so that answer is wrong twice over -- flat AND mis-shaped.  Worse, it is not even D's alone:
         * 12j's spill says the same red runs from x384 to x579, which is box B's right edge through box C's whole
         * window, both 16-px gutters and D's left half -- 
... [truncated for diff preview]