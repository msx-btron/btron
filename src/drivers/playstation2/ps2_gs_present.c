/*
 * ps2_gs_present.c - PlayStation 2 Graphics Synthesizer Presentation HAL
 *
 * Implements hardware display blitting, dirty band calculations, double-buffering
 * page rotation debt management, and GIF DMA canvas upload blitting for B-System.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/workbench.h>

#include "ps2_gs.h"
#include "ps2_gs_pack.h"
#include "ps2_gs_vram.h"
#include "ps2_gs_present.h"
#include "ps2_gif_ring.h"
#include "ps2_irq.h"
#include "ps2_paint_worker.h"

#include "../desktop/graphics/eegs/eegs_present.h"
#include "../desktop/graphics/eegs/backend_eegs.h"

extern void ps2_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern uint32_t btron_render_perf_us(void);
extern uint32_t ps2_count_read(void);
extern void draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H max_w, H max_h);
extern void get_baremetal_mouse_pos(H *x, H *y);
extern int g_cursor_in_backbuffer;

/* ── Band Geometry Operations ────────────────────────────────────────────── */

int ps2_band_is_full(const ps2_band_t *b)
{
    return b->x0 == 0 && b->x1 == PS2_SCREEN_WIDTH &&
           b->top == 0 && b->end == PS2_SCREEN_HEIGHT;
}

void ps2_bandset_full(ps2_bandset_t *set)
{
    set->b[0].x0 = 0;
    set->b[0].x1 = PS2_SCREEN_WIDTH;
    set->b[0].top = 0;
    set->b[0].end = PS2_SCREEN_HEIGHT;
    set->n = 1u;
    set->overflow = 0;
}

void ps2_bandset_add(ps2_bandset_t *set, int x0, int x1, int y0, int y1)
{
    unsigned i, k;

    if (x0 < 0) x0 = 0;
    if (x1 > PS2_SCREEN_WIDTH) x1 = PS2_SCREEN_WIDTH;
    if (y0 < 0) y0 = 0;
    if (y1 > PS2_SCREEN_HEIGHT) y1 = PS2_SCREEN_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;

    for (i = 0; i < set->n; i++) {
        if (y0 > set->b[i].end) continue;
        if (y1 < set->b[i].top) break;
        if (y0 < set->b[i].top) set->b[i].top = y0;
        if (y1 > set->b[i].end) set->b[i].end = y1;
        if (x0 < set->b[i].x0) set->b[i].x0 = x0;
        if (x1 > set->b[i].x1) set->b[i].x1 = x1;
        while (i + 1u < set->n && set->b[i + 1u].top <= set->b[i].end) {
            if (set->b[i + 1u].end > set->b[i].end) set->b[i].end = set->b[i + 1u].end;
            if (set->b[i + 1u].x0 < set->b[i].x0) set->b[i].x0 = set->b[i + 1u].x0;
            if (set->b[i + 1u].x1 > set->b[i].x1) set->b[i].x1 = set->b[i + 1u].x1;
            for (k = i + 1u; k + 1u < set->n; k++) set->b[k] = set->b[k + 1u];
            set->n--;
        }
        return;
    }
    if (set->n >= PS2_BAND_MAX) {
        set->overflow = 1;
        return;
    }
    for (k = set->n; k > i; k--) set->b[k] = set->b[k - 1u];
    set->b[i].x0 = x0;
    set->b[i].x1 = x1;
    set->b[i].top = y0;
    set->b[i].end = y1;
    set->n++;
}

/* ── Double-Buffering Page Rotation Debt Tracking ────────────────────────── */

static ps2_bandset_t s_page_owed[GS_DISP_PAGES];

void ps2_rot_owe_all(void)
{
    for (unsigned i = 0u; i < GS_DISP_PAGES; i++) ps2_bandset_full(&s_page_owed[i]);
}

void ps2_rot_clear(void)
{
    for (unsigned i = 0u; i < GS_DISP_PAGES; i++) {
        s_page_owed[i].n = 0u;
        s_page_owed[i].overflow = 0;
    }
}

void ps2_rot_owe(uint32_t other, const ps2_bandset_t *d)
{
    ps2_bandset_t *o = &s_page_owed[other % GS_DISP_PAGES];

    if (d->overflow || !d->n) {
        if (d->overflow) ps2_bandset_full(o);
        return;
    }
    for (unsigned i = 0u; i < d->n; i++)
        ps2_bandset_add(o, d->b[i].x0, d->b[i].x1, d->b[i].top, d->b[i].end);
    if (o->overflow) ps2_bandset_full(o);
}

int ps2_rot_take(uint32_t page, ps2_bandset_t *set)
{
    ps2_bandset_t *o = &s_page_owed[page % GS_DISP_PAGES];

    if (o->overflow) ps2_bandset_full(o);
    if (!o->n) return 0;

    for (unsigned i = 0u; i < o->n; i++)
        ps2_bandset_add(set, o->b[i].x0, o->b[i].x1, o->b[i].top, o->b[i].end);
    if (set->overflow) ps2_bandset_full(set);
    o->n = 0u;
    o->overflow = 0;
    return 1;
}

int ps2_rot_debt(uint32_t page)
{
    const ps2_bandset_t *o = &s_page_owed[page % GS_DISP_PAGES];
    if (o->overflow) return -1;
    if (o->n == 1u && ps2_band_is_full(&o->b[0])) return -1;
    return (int)o->n;
}

int ps2_rot_step(ps2_bandset_t *set)
{
    uint32_t drawn, other;

    if (!gs_disp_is_double()) return 0;
    drawn = gs_disp_drawn()->index;
    other = gs_disp_shown()->index;
    ps2_rot_owe(other, set);
    return ps2_rot_take(drawn, set);
}

static uint32_t s_page_double_wanted;

int ps2_page_double_apply(uint32_t on)
{
    if (gs_disp_is_double() == (on ? 1u : 0u)) {
        s_page_double_wanted = on;
        return (int)on;
    }
    if (gs_disp_set_double(on) != 0) {
        if (on) {
            ps2_kprintf("[PAGE] double refused at %s\n",
                        gs_disp_psm() == GS_PSM_CT16 ? "PSMCT16" : "PSMCT32");
        }
        return 0;
    }
    s_page_double_wanted = on;
    if (on) ps2_rot_owe_all();
    ps2_gs_dispfb_store();
    return (int)on;
}

uint32_t ps2_page_double_wanted(void)
{
    return s_page_double_wanted;
}

/* ── Canvas Upload & Presentation Blitting ───────────────────────────────── */

static RECT s_20y_rect;
static uint32_t s_20y_plated;
static uint32_t s_gs_groups, s_gs_qw, s_gs_us, s_gs_cover, s_gs_cover_drop, s_gs_cover_merge;
static uint32_t s_gs_empty_stream;
static eegs_present_stats s_gs_st;

void ps2_upload_cover_in_layer(const wnd_gl_cover *cover, const RECT *layer)
{
    for (int i = 0; i < cover->n; i++) {
        const RECT *r = &cover->r[i];
        const H x0 = r->left   > layer->left   ? r->left   : layer->left;
        const H y0 = r->top    > layer->top    ? r->top    : layer->top;
        const H x1 = r->right  < layer->right  ? r->right  : layer->right;
        const H y1 = r->bottom < layer->bottom ? r->bottom : layer->bottom;

        if (x1 > x0 && y1 > y0) {
            ps2_gs_upload(x0, y0, x1 - x0, y1 - y0);
        }
    }
}

void ps2_upload_piece(int x0, int y0, int x1, int y1, int stream, uint8_t why)
{
    (void)stream;
    (void)why;
    if (x1 <= x0 || y1 <= y0) return;
    ps2_gs_upload(x0, y0, x1 - x0, y1 - y0);
}

void ps2_upload_canvas(int x0, int y0, int x1, int y1, const RECT *hole, int stream, uint8_t why)
{
    int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;

    if (hole) {
        hx0 = hole->left   > x0 ? hole->left   : x0;
        hy0 = hole->top    > y0 ? hole->top    : y0;
        hx1 = hole->right  < x1 ? hole->right  : x1;
        hy1 = hole->bottom < y1 ? hole->bottom : y1;
        if (hx1 <= hx0 || hy1 <= hy0) hole = NULL;
    }

    if (!hole) {
        ps2_upload_piece(x0, y0, x1, y1, stream, why);
        return;
    }

    ps2_upload_piece(x0, y0, x1, hy0, stream, why);
    ps2_upload_piece(x0, hy1, x1, y1, stream, why);
    ps2_upload_piece(x0, hy0, hx0 & ~3, hy1, stream, why);
    ps2_upload_piece((hx1 + 3) & ~3, hy0, x1, hy1, stream, why);
}

static uint64_t s_cur_pkt[192] __attribute__((aligned(16)));

static void ps2_cursor_sprite_post(int mx, int my)
{
    static const uint16_t cur_mask[16] = {
        0x8000, 0xC000, 0xE000, 0xF000,
        0xF800, 0xFC00, 0xFE00, 0xFF00,
        0xFF80, 0xFE00, 0xDF00, 0x8F80,
        0x0780, 0x03C0, 0x0180, 0x0000
    };
    static const uint16_t cur_outline[16] = {
        0xC000, 0xE000, 0xF000, 0xF800,
        0xFC00, 0xFE00, 0xFF00, 0xFF80,
        0xFFC0, 0xFFE0, 0xFF80, 0xDFC0,
        0xCFE0, 0x07E0, 0x03C0, 0x0180
    };
    const gs_disp_page_t *drawn = gs_disp_drawn();
    uint64_t *p = (uint64_t *)((uintptr_t)s_cur_pkt | 0x20000000UL);
    uint64_t *tag = p;
    uint32_t fba = 0u, n = 0u;

    (void)gs_vram_fba(drawn->byte, &fba);
    p += 2;

    *p++ = gs_pack_xyoffset(GS_XYOFFSET_PAGE, GS_XYOFFSET_PAGE);
    *p++ = GS_GP_XYOFFSET_1;
    *p++ = gs_pack_scissor(0u, (uint32_t)PS2_SCREEN_WIDTH - 1u, 0u, (uint32_t)PS2_SCREEN_HEIGHT - 1u);
    *p++ = GS_GP_SCISSOR_1;
    *p++ = gs_pack_frame(fba, drawn->fbw, drawn->psm, 0u);
    *p++ = GS_GP_FRAME_1;
    *p++ = 0u;
    *p++ = GS_GP_TEST_1;
    *p++ = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);
    *p++ = GS_GP_PRIM;
    n += 5u;

    for (int pass = 0; pass < 2; pass++) {
        const uint32_t c = pass ? 0xFFu : 0x00u;
        *p++ = gs_pack_rgbaq(c, c, c, 0x80u, 0x3F800000u);
        *p++ = GS_GP_RGBAQ;
        n++;
        for (int y = 0; y < 16; y++) {
            const int py = my + y;
            if (py < 0 || py >= PS2_SCREEN_HEIGHT) continue;
            uint16_t row = pass ? cur_mask[y] : (uint16_t)(cur_outline[y] & (uint16_t)~cur_mask[y]);
            int x = 0;
            while (x < 16) {
                while (x < 16 && !(row & (0x8000u >> x))) x++;
                if (x >= 16) break;
                int x0 = x;
                while (x < 16 && (row & (0x8000u >> x))) x++;
                int sx0 = mx + x0, sx1 = mx + x;
                if (sx0 < 0) sx0 = 0;
                if (sx1 > PS2_SCREEN_WIDTH) sx1 = PS2_SCREEN_WIDTH;
                if (sx1 > sx0 && n + 2u <= 94u) {
                    *p++ = gs_pack_xyz2(gs_vertex_coord16(sx0), gs_vertex_coord16(py), 0u);
                    *p++ = GS_GP_XYZ2;
                    *p++ = gs_pack_xyz2(gs_vertex_coord16(sx1), gs_vertex_coord16(py + 1), 0u);
                    *p++ = GS_GP_XYZ2;
                    n += 2u;
                }
            }
        }
    }
    tag[0] = gs_pack_giftag(n, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
    tag[1] = gs_pack_regs((const uint32_t[]){ GS_NIB_AD }, 1u);
    (void)ps2_gif_post(s_cur_pkt, n + 1u);
}

void ps2_paint_region(GDEV *screen, int x0, int y0, int x1, int y1, int full,
                      uint32_t *r_us, uint32_t *s_us, uint32_t *u_us)
{
    const uint32_t t0 = ps2_count_read();
    RECT gl_client;
    wnd_gl_cover gl_cover;
    int gs_groups = 0;
    int gs_layer = FALSE;

    eegs_present_begin();

    if (full) {
        workbench_render(screen, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    } else {
        RECT d = { (H)x0, (H)y0, (H)x1, (H)y1 };
        if (wnd_damage_needs_paint(&d)) workbench_render_damage_paint(screen, &d);
        else                            workbench_render_damage(screen, &d);
        if (g_cursor_in_backbuffer) {
            H mx = 0, my = 0;
            get_baremetal_mouse_pos(&mx, &my);
            draw_baremetal_mouse_cursor(screen, mx, my, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
        }
    }

    eegs_present_end();

    const uint32_t t1 = ps2_count_read();
    const uint32_t t2 = ps2_count_read();

    const int gs_took = wnd_gl_layer_take(&gl_client);
    const uint32_t gs_staged = eegs_present_groups_staged();
    const int gs_paints = gs_took && gs_staged != 0u &&
                          gl_client.right > gl_client.left && gl_client.bottom > gl_client.top;
    RECT gl_hole;
    int gl_hole_live = FALSE;
    uint8_t gl_why = PS2_WHY_STREAM;

    if (gs_took && !gs_paints) s_gs_empty_stream++;

    if (!gs_paints) {
        /* Closing the layer window (XMB chrome gone) must not keep
         * replaying the last GIF into the vacated client: that is the
         * body still sitting on the GS after cls_wnd.  No layer means
         * 2D canvas owns the rect. */
        if (!wnd_gl_is_frozen() || !wnd_gl_layer()) s_20y_plated = 0u;
        if (wnd_gl_layer_rect(&gl_hole)) {
            gl_hole_live = TRUE;
            gl_why = PS2_WHY_CUT;
        } else if (wnd_gl_layer() && wnd_gl_is_frozen() && s_20y_plated) {
            gl_hole = s_20y_rect;
            gl_hole_live = TRUE;
            gl_why = PS2_WHY_CUT;
        } else if (wnd_gl_layer()) {
            gl_why = PS2_WHY_NO_RECT;
        } else {
            gl_why = PS2_WHY_NO_LAYER;
        }
    } else {
        /* Live GIF this pass.  Always cut the client out of the 2D
         * canvas.  The compositor skipped the layer GDEV, so that rect
         * is wallpaper and windows underneath; WHY_NOCUT used to DMA
         * them into the GS hole then overwrite with the stream, which
         * flickered those windows through focused Quake. */
        gl_hole = gl_client;
        gl_hole_live = TRUE;
        gl_why = PS2_WHY_CUT;
    }

    ps2_upload_canvas(x0, y0, x1, y1, gl_hole_live ? &gl_hole : NULL, gs_paints, gl_why);

    {
        const uint32_t tg0 = ps2_count_read();
        eegs_present_stats st;

        if (gs_paints) {
            gs_groups = eegs_present_send(&gl_client);
            if (gs_groups > 0) {
                s_gs_groups += (uint32_t)gs_groups;
                eegs_present_get_stats(&st);
                s_gs_st = st;
                s_gs_qw += st.qw;
                s_20y_plated = (st.plate_covers && st.dropped == 0u) ? 1u : 0u;
                s_20y_rect = gl_client;
                gs_layer = TRUE;
            }
        } else if (gl_hole_live && s_20y_plated &&
                   s_20y_rect.left   == gl_hole.left &&
                   s_20y_rect.top    == gl_hole.top &&
                   s_20y_rect.right  == gl_hole.right &&
                   s_20y_rect.bottom == gl_hole.bottom &&
                   x1 > gl_hole.left && x0 < gl_hole.right &&
                   y1 > gl_hole.top  && y0 < gl_hole.bottom) {
            /* Same client as the last plated stream.  A layer steal
             * must not replay the previous holder's GIF into this hole. */
            gs_groups = eegs_present_replay(&gl_hole);
            if (gs_groups > 0) {
                gl_client = gl_hole;
                gs_layer = TRUE;
            }
        }

        int n_cover = wnd_gl_cover_take(&gl_cover);
        s_gs_cover_merge += (uint32_t)gl_cover.merged;
        if (gs_layer || gl_hole_live) {
            ps2_upload_cover_in_layer(&gl_cover, gs_layer ? &gl_client : &gl_hole);
            s_gs_cover += (uint32_t)n_cover;
            s_gs_cover_drop += (uint32_t)gl_cover.dropped;
            if (gs_layer) wnd_gl_freeze(1);
        }

        if (g_cursor_in_backbuffer) {
            H mx = 0, my = 0;
            get_baremetal_mouse_pos(&mx, &my);
            if (gs_layer || (mx + PS2_CURSOR_COLS > x0 && mx < x1 &&
                             my + PS2_CURSOR_ROWS > y0 && my < y1)) {
                ps2_cursor_sprite_post((int)mx, (int)my);
            }
        }

        const uint32_t tg1 = ps2_count_read();
        const uint32_t tpu = btron_ee_ticks_per_us();
        if (tpu) s_gs_us += (tg1 - tg0) / tpu;
    }

    const uint32_t t3 = ps2_count_read();
    const uint32_t tpu = btron_ee_ticks_per_us();
    if (tpu) {
        *r_us = (t1 - t0) / tpu;
        *s_us = (t2 - t1) / tpu;
        *u_us = (t3 - t2) / tpu;
    } else {
        *r_us = *s_us = *u_us = 0u;
    }

#if BTRON_PS2_PAINT_WORKER
    if (s_pw_on && gs_layer && !s_pw_recorded && s_pw_layer &&
        (const void *)wnd_gl_layer() == s_pw_layer) {
        s_pw_recorded = 1;
        ps2_pw_done(s_pw_layer, *r_us, btron_render_perf_us());
        ps2_pw_freeze(1);
    }
#endif
}

extern void btron_cooperative_input_pump(void);

void ps2_paint_bands(GDEV *screen, const ps2_bandset_t *set,
                     uint32_t *r_us, uint32_t *s_us, uint32_t *u_us,
                     uint32_t *px_out)
{
    uint32_t acc_r = 0u, acc_s = 0u, acc_u = 0u, px = 0u;

    for (unsigned i = 0; i < set->n; i++) {
        const ps2_band_t *band = &set->b[i];
        uint32_t r, s, u;

        px += (uint32_t)(band->x1 - band->x0) * (uint32_t)(band->end - band->top);
        ps2_paint_region(screen, band->x0, band->top, band->x1, band->end,
                         ps2_band_is_full(band), &r, &s, &u);
        acc_r += r; acc_s += s; acc_u += u;
        if (i + 1u < set->n) btron_cooperative_input_pump();
    }

    *r_us = acc_r;
    *s_us = acc_s;
    *u_us = acc_u;
    *px_out = px;
}

void ps2_bands_for_pass(ps2_bandset_t *set, int mouse_x, int mouse_y, int shown_x, int shown_y)
{
    RECT dmg;

    if (mouse_x != shown_x || mouse_y != shown_y) {
        const int cx0 = shown_x < mouse_x ? shown_x : mouse_x;
        const int cx1 = (shown_x > mouse_x ? shown_x : mouse_x) + PS2_CURSOR_COLS;

        ps2_bandset_add(set, cx0, cx1, shown_y, shown_y + PS2_CURSOR_ROWS);
        ps2_bandset_add(set, cx0, cx1, mouse_y, mouse_y + PS2_CURSOR_ROWS);
        if (shown_y < PS2_PANEL_ROWS || mouse_y < PS2_PANEL_ROWS)
            ps2_bandset_add(set, 0, PS2_SCREEN_WIDTH, 0, PS2_PANEL_ROWS);
    }

    if (wnd_take_inval_damage(&dmg)) {
        ps2_bandset_add(set, dmg.left, dmg.right, dmg.top, dmg.bottom);
    }
}
