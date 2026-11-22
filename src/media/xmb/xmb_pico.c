/*
 * xmb_pico.c — XMB surface for the PicoCalc image.
 *
 * The menu is the PS3 cross from xmb.c (xb_layout, xb_draw_band,
 * xb_item_y): one horizontal category row, the selected category pinned
 * at margin_left + spacing_h, and that category's items in a vertical
 * column on the same x. The sheet is the ribbon from xmb_wave.c
 * (wv_sheet + wv_project): full-width, screen y = (1 + vy) * h / 2,
 * with vy the three cosines. The lattice is 8×6, half of 16×12 on each
 * axis. This file does not link xmb.c, xmb_wave.c, or the atlas.
 * On the device the wave is screen-space lift triangles on core 1.
 * Icons, labels, and the gradient stay on core 0.
 */
#include "xmb_pico.h"
#include "xmb_pico_icons.h"

#include <stdint.h>

#if defined(PICO_REAL_HW)
#include "picogl.h"
extern uint16_t *dp_pico_band_row(int y);
#elif defined(SEGUI_HOST_XMB)
#include <btron/dp.h>
#else
extern uint16_t *dp_pico_band_row(int y);
#endif

/* Same glyphs as the SegUI window titles (troncode + JIS). The anchor
 * test does not paint, so it does not pull the font in. */
#if !defined(XMB_PICO_TEST)
#include <btron/troncode.h>
#endif

#define INK_R 31
#define INK_G 62
#define INK_B 31

typedef struct {
    int icon;
    const char *label;
    int go;
} xmb_item_t;

typedef struct {
    int icon;
    const char *label;
    const xmb_item_t *items;
    int n;
} xmb_cat_t;

/* Same strings as the SegUI window titles in segui_palm.c. */
static const xmb_item_t s_items_apps[] = {
    { IC_APP_TERM,   "端末 (Terminal)", XMB_GO_TERMINAL },
    { IC_APP_EDITOR, "文書 (Editor)",   XMB_GO_EDITOR },
    { IC_APP_TAD,    "閲覧 (Browser)",  XMB_GO_BROWSER },
    { IC_APP_DRIVE,  "書庫 (Cabinet)",  XMB_GO_CABINET },
    { IC_GEAR,       "歯車 (GLGears)",  XMB_GO_GEARS },
    { IC_APP_CHAT,   "対話 (Chat)",     XMB_GO_CHAT }
};

static const xmb_item_t s_items_settings[] = {
    { IC_SLIDER, "Screen",  XMB_GO_PREFS },
    { IC_INFO,   "About",   XMB_GO_PREFS },
    { IC_PROMPT, "Console", XMB_GO_TERMINAL }
};

static const xmb_item_t s_items_games[] = {
    { IC_APP_QUAKE, "Quake", XMB_GO_NONE }
};

static const xmb_item_t s_items_commands[] = {
    { IC_PROMPT, "ls",   XMB_GO_TERMINAL },
    { IC_PROMPT, "info", XMB_GO_TERMINAL }
};

static const xmb_cat_t s_cats[] = {
    { IC_CAT_APPS,     "Applications", s_items_apps,     6 },
    { IC_CAT_SETTINGS, "Settings",     s_items_settings, 3 },
    { IC_CAT_GAMES,    "Games",        s_items_games,    1 },
    { IC_CAT_COMMANDS, "Commands",     s_items_commands, 2 }
};

static int s_cat;
static int s_item;
/* Effect time in 1/256. Eleven per tick is the original dt/1600 at ~14 fps. */
static int s_phase;
#if defined(PICO_REAL_HW)
static int s_core;
#endif

#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
/* Screen-sized BE RGB565. The painter indexes row[x] with absolute x,
 * the same way the Pico strip does. Flushed into the GDEV after the rect. */
static uint16_t s_host_be[320 * 320];
static COLOR *s_host_px;
static int s_host_w;
static int s_host_h;

uint16_t *dp_pico_band_row(int y)
{
    if (y < 0 || y >= 320) return NULL;
    return s_host_be + y * 320;
}

void xmb_pico_host_bind(void *pixels, int w, int h)
{
    s_host_px = (COLOR *)pixels;
    s_host_w = w;
    s_host_h = h;
}
#endif

static uint16_t be_swap(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

static uint16_t pack_be(int r, int g, int b)
{
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
    return be_swap(v);
}

static void unpack(uint16_t be, int *r, int *g, int *b)
{
    uint16_t v = be_swap(be);
    *r = (v >> 11) & 31;
    *g = (v >> 5) & 63;
    *b = v & 31;
}

/* SRC_OVER. a == 0 keeps the destination. Ink is the cell's white. */
static uint16_t src_over(uint16_t dst, int a)
{
    int dr, dg, db;
    unpack(dst, &dr, &dg, &db);
    dr += ((INK_R - dr) * a) >> 8;
    dg += ((INK_G - dg) * a) >> 8;
    db += ((INK_B - db) * a) >> 8;
    if (dr < 0) dr = 0;
    if (dg < 0) dg = 0;
    if (db < 0) db = 0;
    if (dr > 31) dr = 31;
    if (dg > 63) dg = 63;
    if (db > 31) db = 31;
    return pack_be(dr, dg, db);
}

#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
static void host_flush(int left, int top, int right, int bottom)
{
    int y, x;
    if (!s_host_px || s_host_w <= 0 || s_host_h <= 0) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > s_host_w) right = s_host_w;
    if (bottom > s_host_h) bottom = s_host_h;
    if (right > 320) right = 320;
    if (bottom > 320) bottom = 320;
    for (y = top; y < bottom; y++) {
        uint16_t *row = s_host_be + y * 320;
        COLOR *dst = s_host_px + y * s_host_w;
        for (x = left; x < right; x++) {
            uint16_t v = be_swap(row[x]);
            int r5 = (v >> 11) & 31;
            int g6 = (v >> 5) & 63;
            int b5 = v & 31;
            int r8 = (r5 << 3) | (r5 >> 2);
            int g8 = (g6 << 2) | (g6 >> 4);
            int b8 = (b5 << 3) | (b5 >> 2);
            dst[x] = 0xFF000000u | ((COLOR)r8 << 16) | ((COLOR)g8 << 8) | (COLOR)b8;
        }
    }
}
#endif

/* xb_gradient() deep blue, corners BL BR TL TR, drawn as TL TR BR BL.
 * Channels are RGB565. Red quantizes to 0. */
static void fill_gradient(int left, int top, int right, int bottom)
{
    int y, h, x0, x1, span;
    if (right > 320) right = 320;
    if (left < 0) left = 0;
    h = bottom - top;
    if (h < 1 || left >= right) return;
    x0 = left;
    x1 = right;
    span = x1 - x0;
    for (y = top; y < bottom; y++) {
        uint16_t *row = dp_pico_band_row(y);
        int t, lg, lb, rg, rb, ag, ab, sg, sb, x;
        if (!row) continue;
        t = (y - top) * 256 / h;
        if (t < 0) t = 0;
        if (t > 256) t = 256;
        lg = 23 + ((0 - 23) * t) / 256;
        lb = 24 + ((8 - 24) * t) / 256;
        rg = 40 + ((18 - 40) * t) / 256;
        rb = 31 + ((22 - 31) * t) / 256;
        ag = lg << 8;
        ab = lb << 8;
        sg = ((rg - lg) << 8) / span;
        sb = ((rb - lb) << 8) / span;
        for (x = x0; x < x1; x++) {
            row[x] = pack_be(0, ag >> 8, ab >> 8);
            ag += sg;
            ab += sb;
        }
    }
}

/* alpha_q8 is 0..256. 256 is the active icon, 192 is the passive 0.75. */
static void blit_cell(int cell, int x, int y, int size, int alpha_q8)
{
    const unsigned char *px = xmb_pico_cell(cell);
    int row, col, step;
    if (!px || size < 8 || alpha_q8 <= 0) return;
    /* The cell is 32. Twice the PS3-on-320 icon is about 42, so scale up. */
    if (size > 64) size = 64;
    step = (XMB_PICO_CELL << 8) / size;
    for (row = 0; row < size; row++) {
        uint16_t *dst = dp_pico_band_row(y + row);
        int sy, a_row;
        if (!dst) continue;
        sy = (row * step) >> 8;
        if (sy >= XMB_PICO_CELL) sy = XMB_PICO_CELL - 1;
        a_row = sy * XMB_PICO_CELL;
        for (col = 0; col < size; col++) {
            int sx = (col * step) >> 8;
            int a, dx;
            if (sx >= XMB_PICO_CELL) sx = XMB_PICO_CELL - 1;
            a = px[a_row + sx];
            dx = x + col;
            if (a == 0 || dx < 0 || dx >= 320) continue;
            a = (a * alpha_q8) >> 8;
            if (a == 0) continue;
            dst[dx] = src_over(dst[dx], a);
        }
    }
}

/* Left edge at x. ASCII is 8×16 and kanji is 16×16, the window-title
 * cell. Only the ink bits are written, so the wave shows through.
 * alpha_q8 fades the pen in two steps. Rows outside this band are skipped. */
static void label_at(int x, int y, const char *s, int alpha_q8)
{
#if defined(XMB_PICO_TEST)
    (void)x;
    (void)y;
    (void)s;
    (void)alpha_q8;
#else
    uint16_t pen;
    TC prev = 0;
    if (!s || !s[0] || alpha_q8 < 64) return;
    pen = alpha_q8 >= 220 ? pack_be(INK_R, INK_G, INK_B) : pack_be(18, 40, 18);
    while (*s) {
        int consumed = 0;
        int row, gw, gh, adv;
        TC code = utf8_to_tc(s, &consumed);
        H w = 8, h = 16;
        const unsigned char *bmp;
        if (consumed < 1) consumed = 1;
        s += consumed;
        bmp = get_glyph_bitmap(code, &w, &h);
        gw = (int)w;
        gh = (int)h;
        if (gh > 16) gh = 16;
        if (!bmp || gw < 1 || gh < 1) {
            prev = code;
            continue;
        }
        adv = (int)tc_get_char_advance(code, prev);
        if (adv < 1) adv = gw > 8 ? 16 : 8;
        for (row = 0; row < gh; row++) {
            uint16_t *dst = dp_pico_band_row(y + row);
            int col, cols;
            if (!dst) continue;
            cols = gw > 8 ? 16 : 8;
            for (col = 0; col < cols; col++) {
                unsigned bit;
                int dx;
                if (gw <= 8)
                    bit = (unsigned)bmp[row] & (0x80u >> col);
                else
                    bit = (unsigned)bmp[row * 2 + (col >> 3)]
                        & (0x80u >> (col & 7));
                if (!bit) continue;
                dx = x + col;
                if (dx < 0 || dx >= 320) continue;
                dst[dx] = pen;
            }
        }
        x += adv;
        prev = code;
    }
#endif
}

/* xb_layout(): PS3 metrics, scale = body width / 1920.
 * Icon and both pitches are twice that scale. A 320-wide body would
 * otherwise draw a ~20px icon. Margins stay on the 1920 scale. */
static int xmb_sc(int k, int w)
{
    return (k * w) / 1920;
}

/* Cross at the selected category. cx, cy are its icon center.
 * icon, sph, spv are the drawn size and the two pitches. */
static void xmb_metrics(int left, int top, int width,
                        int *icon, int *sph, int *spv, int *cx, int *cy)
{
    int ic, sh, sv, mt, ml;
    if (width < 1) width = 1;
    ic = xmb_sc(128, width) * 2;
    sh = xmb_sc(192, width) * 2;
    sv = xmb_sc(64, width) * 2;
    mt = xmb_sc(272, width);
    ml = xmb_sc(336, width);
    if (ic < 8) ic = 8;
    if (sh < 1) sh = 1;
    if (sv < 1) sv = 1;
    *icon = ic;
    *sph = sh;
    *spv = sv;
    *cx = left + ml + sh;
    *cy = top + mt + ic / 2;
}

/* xb_item_y() coefficient: above_item -1, active_factor 3, under_item 5. */
static int item_k(int i, int current)
{
    if (i < current) return i - current - 1;
    if (i == current) return 3;
    return i - current + 5;
}

/* One ease-out follower per value. Each tick covers half the remainder,
 * which is the cheap stand-in for XB_EASE_OUT_QUAD over XB_DELAY.
 * Retargeting is changing the target; the follower keeps its place. */
#define FOLLOW_N 8
static int s_follow;
static int s_vis_cat;             /* Q8 category index */
static int s_col;                 /* Q8, 0 off to the side, 256 seated */
static int s_col_dir = 1;
static int s_k[FOLLOW_N];         /* Q8 item_k */
static int s_z[FOLLOW_N];         /* Q8 zoom, 128 passive, 256 active */

static void approach(int *v, int target)
{
    int d = target - *v;
    if (d > -2 && d < 2) {
        *v = target;
        return;
    }
    *v += d / 2;
}

static void follow_boot(void)
{
    int i;
    s_vis_cat = s_cat * 256;
    s_col = 256;
    for (i = 0; i < FOLLOW_N; i++) {
        s_k[i] = item_k(i, s_item) * 256;
        s_z[i] = (i == s_item) ? 256 : 128;
    }
    s_follow = 1;
}

static void follow_step(void)
{
    int i;
    int nitem = s_cats[s_cat].n;
    if (!s_follow) follow_boot();
    approach(&s_vis_cat, s_cat * 256);
    approach(&s_col, 256);
    for (i = 0; i < FOLLOW_N; i++) {
        int sel = (i < nitem && i == s_item) ? 256 : 128;
        approach(&s_k[i], item_k(i, s_item) * 256);
        approach(&s_z[i], sel);
    }
}

void xmb_pico_tick(void)
{
#if defined(PICO_REAL_HW)
    if (!s_core) {
        pico_core1_launch();
        s_core = 1;
    }
    picogl_reset_stats();
#endif
    s_phase += 11;
    if (s_phase > 180000) s_phase = 0;
    follow_step();
}

void xmb_pico_move(int dcat, int ditem)
{
    int nc = (int)(sizeof s_cats / sizeof s_cats[0]);
    int i;
    if (!s_follow) follow_boot();
    if (dcat) {
        int next = s_cat + dcat;
        if (next < 0) next = nc - 1;
        if (next >= nc) next = 0;
        if (next != s_cat) {
            /* xb_anim_band_move slides the row. xb_anim_list_switch
             * brings the new column in from this side. */
            s_col_dir = dcat < 0 ? -1 : 1;
            s_cat = next;
            s_item = 0;
            s_col = 0;
            for (i = 0; i < FOLLOW_N; i++) {
                s_k[i] = item_k(i, 0) * 256;
                s_z[i] = (i == 0) ? 256 : 128;
            }
        }
    }
    if (ditem) {
        int n = s_cats[s_cat].n;
        int next;
        if (n < 1) return;
        next = s_item + ditem;
        if (next < 0) next = n - 1;
        if (next >= n) next = 0;
        s_item = next;
    }
}

int xmb_pico_go(void)
{
    const xmb_cat_t *c = &s_cats[s_cat];
    if (s_item < 0 || s_item >= c->n) return XMB_GO_NONE;
    return c->items[s_item].go;
}

void xmb_pico_anchors(int left, int top, int right, int bottom,
                      int *cat_x, int *item_x, int *cat, int *item)
{
    int nc = (int)(sizeof s_cats / sizeof s_cats[0]);
    int icon, sph, spv, cx, cy;
    (void)bottom;
    if (s_cat < 0 || s_cat >= nc) s_cat = 0;
    if (s_item < 0 || s_item >= s_cats[s_cat].n) s_item = 0;
    xmb_metrics(left, top, right - left, &icon, &sph, &spv, &cx, &cy);
    (void)sph;
    (void)spv;
    (void)cy;
    if (cat_x) *cat_x = cx - icon / 2;
    if (item_x) *item_x = cx - icon / 2;
    if (cat) *cat = s_cat;
    if (item) *item = s_item;
}

/* wv_sheet + wv_project. gx and gz are Q10 on [-1024, 1024].
 * One turn of cosine is 256 steps. turn256 converts Q10 radians:
 * 256/(2*pi)/1024 = 2608/65536. The iq noise term (±1/15) is not
 * evaluated; the three cosines are the ribbon, within about 2px of
 * wv_sheet at this size. 8×6 is half of 16×12 on each axis. */
#define WAVE_NX 8
#define WAVE_NY 6
static const int16_t s_cos256[256] = {
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757
};

static int icos(int idx)
{
    return (int)s_cos256[idx & 255];
}

/* Q10 radians to a 256-step turn. */
static int turn256(int q10)
{
    return (q10 * 2608) >> 16;
}

static void wave_point(int i, int j, int left, int top, int width, int height,
                       int *ox, int *oy)
{
    int gx = (i * 2048) / WAVE_NX - 1024;
    int gz = (j * 2048) / WAVE_NY - 1024;
    int t10 = (s_phase * 1043) >> 16;
    int th = (s_phase * 5215) >> 16;
    int row = icos(turn256(gz * 4));
    int lc = icos(turn256(gx + gz) + t10);
    int col = icos(turn256(gx * 2) - th);
    int term1 = (row * lc) >> 23;
    int term2 = (col / 5) >> 5;
    int vy = term1 - term2 + 307;
    *ox = left + ((gx + 1024) * width) / 2048;
    *oy = top + ((1024 + vy) * height) / 2048;
}

/* 9×7 points. The phase and the body rectangle pick the sheet.
 * Later bands of the same wave read these and post only the cells
 * that cross the strip. */
#define WAVE_PTS_X (WAVE_NX + 1)
#define WAVE_PTS_Y (WAVE_NY + 1)
static int16_t s_wx[WAVE_PTS_X * WAVE_PTS_Y];
static int16_t s_wy[WAVE_PTS_X * WAVE_PTS_Y];
static int s_wphase = -1;
static int s_wleft, s_wtop, s_ww, s_wh;

static void wave_cache(int left, int top, int width, int height)
{
    int i, j;
    if (s_wphase == s_phase && s_wleft == left && s_wtop == top &&
        s_ww == width && s_wh == height)
        return;
    for (j = 0; j <= WAVE_NY; j++) {
        for (i = 0; i <= WAVE_NX; i++) {
            int x, y, k;
            wave_point(i, j, left, top, width, height, &x, &y);
            k = j * WAVE_PTS_X + i;
            s_wx[k] = (int16_t)x;
            s_wy[k] = (int16_t)y;
        }
    }
    s_wphase = s_phase;
    s_wleft = left;
    s_wtop = top;
    s_ww = width;
    s_wh = height;
}

#if !defined(PICO_REAL_HW)
/* Same dst*(1+src) as paint_span_lift, so a host frame matches the panel. */
static void host_lift_span(int y, int lo, int hi, uint16_t lift)
{
    uint16_t *row;
    unsigned sr, sg, sb;
    int x;
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    if (y < 0 || y >= 320) return;
    row = dp_pico_band_row(y);
    if (!row) return;
    if (lo < 0) lo = 0;
    if (hi > 319) hi = 319;
    sr = (lift >> 11) & 31u;
    sg = (lift >> 5) & 63u;
    sb = lift & 31u;
    for (x = lo; x <= hi; x++) {
        uint16_t d = (uint16_t)((row[x] >> 8) | (row[x] << 8));
        unsigned dr = (d >> 11) & 31u;
        unsigned dg = (d >> 5) & 63u;
        unsigned db = d & 31u;
        unsigned out;
        dr += (dr * sr) >> 4;
        dg += (dg * sg) >> 5;
        db += (db * sb) >> 4;
        if (dr > 31u) dr = 31u;
        if (dg > 63u) dg = 63u;
        if (db > 31u) db = 31u;
        out = (dr << 11) | (dg << 5) | db;
        row[x] = (uint16_t)((out >> 8) | (out << 8));
    }
}

static void host_lift_tri(int x0, int y0, int x1, int y1, int x2, int y2,
                          uint16_t lift)
{
    int y, ymin, ymax;
    ymin = y0;
    if (y1 < ymin) ymin = y1;
    if (y2 < ymin) ymin = y2;
    ymax = y0;
    if (y1 > ymax) ymax = y1;
    if (y2 > ymax) ymax = y2;
    if (ymin < 0) ymin = 0;
    if (ymax > 319) ymax = 319;
    for (y = ymin; y <= ymax; y++) {
        int xs[3], n, k;
        int ax[3], ay[3], bx[3], by[3];
        n = 0;
        ax[0] = x0; ay[0] = y0; bx[0] = x1; by[0] = y1;
        ax[1] = x1; ay[1] = y1; bx[1] = x2; by[1] = y2;
        ax[2] = x2; ay[2] = y2; bx[2] = x0; by[2] = y0;
        for (k = 0; k < 3; k++) {
            int ya = ay[k], yb = by[k], xa = ax[k], xb = bx[k];
            if (ya > yb) {
                int t = ya; ya = yb; yb = t;
                t = xa; xa = xb; xb = t;
            }
            if (y < ya || y >= yb || yb == ya) continue;
            xs[n++] = xa + (xb - xa) * (y - ya) / (yb - ya);
        }
        if (n >= 2) {
            int lo = xs[0], hi = xs[0];
            for (k = 1; k < n; k++) {
                if (xs[k] < lo) lo = xs[k];
                if (xs[k] > hi) hi = xs[k];
            }
            host_lift_span(y, lo, hi, lift);
        }
    }
}
#endif

#if defined(PICO_REAL_HW)
static int s_lift_org;
#endif

static void emit_lift(int x0, int y0, int x1, int y1, int x2, int y2,
                      uint16_t lift)
{
#if defined(PICO_REAL_HW)
    picogl_screen_lift(x0, y0 - s_lift_org, x1, y1 - s_lift_org,
                       x2, y2 - s_lift_org, lift);
#else
    host_lift_tri(x0, y0, x1, y1, x2, y2, lift);
#endif
}

static void wave_surface(int left, int top, int right, int bottom)
{
    int y, y0, y1, width, height, j, i;
#if defined(PICO_REAL_HW)
    int band_y0;
    uint16_t *row0;
    row0 = 0;
#endif
    y0 = -1;
    y1 = -1;
    width = right - left;
    height = bottom - top;
    if (width < 8 || height < 8) return;
    for (y = 0; y < 320; y++) {
        uint16_t *row = dp_pico_band_row(y);
        if (!row) continue;
        if (y0 < 0) {
            y0 = y;
#if defined(PICO_REAL_HW)
            row0 = row;
#endif
        }
        y1 = y;
    }
    if (y0 < 0) return;
    wave_cache(left, top, width, height);
#if defined(PICO_REAL_HW)
    if (!row0) return;
    band_y0 = y0 & ~15;
    s_lift_org = band_y0;
    picogl_target_push();
    picogl_bind_target(row0 - (y0 - band_y0) * 320, 0, 320, 16, 320);
    picogl_set_depth(0);
#endif
    for (j = 0; j < WAVE_NY; j++) {
        for (i = 0; i < WAVE_NX; i++) {
            int xa, ya, xb, yb, xc, yc, xd, yd;
            int ytop, ybot, k, d0, d1, d2, fold, bri;
            uint16_t lift;
            k = j * WAVE_PTS_X + i;
            xa = s_wx[k];
            ya = s_wy[k];
            xb = s_wx[k + 1];
            yb = s_wy[k + 1];
            xc = s_wx[k + WAVE_PTS_X + 1];
            yc = s_wy[k + WAVE_PTS_X + 1];
            xd = s_wx[k + WAVE_PTS_X];
            yd = s_wy[k + WAVE_PTS_X];
            ytop = ya;
            if (yb < ytop) ytop = yb;
            if (yc < ytop) ytop = yc;
            if (yd < ytop) ytop = yd;
            ybot = ya;
            if (yb > ybot) ybot = yb;
            if (yc > ybot) ybot = yc;
            if (yd > ybot) ybot = yd;
            if (ybot < y0 || ytop > y1) continue;
            d0 = ya - yb;
            if (d0 < 0) d0 = -d0;
            d1 = yd - yc;
            if (d1 < 0) d1 = -d1;
            d2 = ya - yd;
            if (d2 < 0) d2 = -d2;
            fold = d0 + d1 + d2;
            bri = 1;
            if (fold >= 8) bri = 2;
            if (fold >= 20) bri = 3;
            lift = (uint16_t)((bri << 11) | ((bri * 2) << 5) | bri);
            emit_lift(xa, ya, xb, yb, xc, yc, lift);
            emit_lift(xa, ya, xc, yc, xd, yd, lift);
        }
    }
#if defined(PICO_REAL_HW)
    picogl_flush_target();
    picogl_target_pop();
#endif
}

const char *xmb_pico_label(void)
{
    const xmb_cat_t *c;
    int nc = (int)(sizeof s_cats / sizeof s_cats[0]);
    if (s_cat < 0 || s_cat >= nc) return "";
    c = &s_cats[s_cat];
    if (s_item < 0 || s_item >= c->n) return "";
    return c->items[s_item].label;
}

void xmb_pico_paint(int left, int top, int right, int bottom)
{
    const xmb_cat_t *cat;
    int nc = (int)(sizeof s_cats / sizeof s_cats[0]);
    int i, pass, width;
    int icon, sph, spv, cx, cy, mt, ll;
    if (right <= left || bottom <= top) return;
    if (s_cat < 0 || s_cat >= nc) s_cat = 0;
    cat = &s_cats[s_cat];
    if (s_item < 0 || s_item >= cat->n) s_item = 0;
    width = right - left;
    xmb_metrics(left, top, width, &icon, &sph, &spv, &cx, &cy);
    mt = xmb_sc(272, width);
    /* xb_layout's label_left is 85*sf, measured from the icon center.
     * The drawn icon is twice 128*sf, so the offset is doubled too.
     * That leaves the original gap, also doubled, past the glyph. */
    ll = xmb_sc(85, width) * 2;

    if (!s_follow) follow_boot();
    fill_gradient(left, top, right, bottom);
    /* Core 1 fills the sheet while the previous band is still on the SPI.
     * Icons are drawn after the join so they sit on the wave. */
    wave_surface(left, top, right, bottom);

    /* Categories share cy. The row slides in Q8 (s_vis_cat).
     * xmb.c keeps the pinned category at zoom 1 while depth is 1, and
     * a nested list (depth > 1) slides that row aside without changing
     * the zoom. This screen has no nested page: the item column is the
     * open submenu, so the whole category row draws at the passive
     * zoom (0.5). Alpha still marks the pinned category. The selected
     * item below stays full size. The row does not wrap on screen. */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < nc; i++) {
            int dist = i * 256 - s_vis_cat;
            int z, aq, size, x, y;
            if (dist < 0) dist = -dist;
            if (dist > 256) dist = 256;
            z = 128;
            if ((pass == 0 && z >= 200) || (pass == 1 && z < 200)) continue;
            aq = 256 - ((dist * 64) >> 8);
            size = (icon * z) >> 8;
            if (size < 8) size = 8;
            x = cx + ((i * 256 - s_vis_cat) * sph) / 256 - size / 2;
            y = cy - size / 2;
            blit_cell(s_cats[i].icon, x, y, size, aq);
        }
    }
    label_at(left + xmb_sc(34, width) + (icon * 2) / 5,
             top + xmb_sc(30, width) + 8, cat->label, 256);

    /* The open column sits on the cross. A category change starts it
     * one pitch to the side (s_col == 0) and eases it in. A row change
     * eases each y and zoom. Depth stays 1. */
    {
        int slide = ((256 - s_col) * s_col_dir * sph) / 256;
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < cat->n; i++) {
                int z = s_z[i];
                int yoff, size, x, y, aq;
                if (z < 128) z = 128;
                if (z > 256) z = 256;
                if ((pass == 0 && z >= 200) || (pass == 1 && z < 200)) continue;
                yoff = (s_k[i] * spv) / 256;
                size = (icon * z) >> 8;
                if (size < 8) size = 8;
                x = cx + slide - size / 2;
                y = top + mt + yoff + icon / 2 - size / 2;
                if (y + size < top || y >= bottom) continue;
                aq = (128 + (z >> 1)) * s_col / 256;
                blit_cell(cat->items[i].icon, x, y, size, aq);
            }
        }
        for (i = 0; i < cat->n; i++) {
            int yoff = (s_k[i] * spv) / 256;
            /* 16px cell, centred on the icon. The icon center is
             * top + mt + yoff + icon/2, same point blit_cell uses. */
            int ty = top + mt + yoff + icon / 2 - 8;
            int aq = (128 + (s_z[i] >> 1)) * s_col / 256;
            if (ty + 16 < top || ty >= bottom) continue;
            label_at(cx + slide + ll, ty, cat->items[i].label, aq);
        }
    }
#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
    host_flush(left, top, right, bottom);
#endif
}

#if defined(XMB_PICO_TEST)
int xmb_pico_test_cat_cx(int left, int top, int right, int bottom, int index)
{
    int icon, sph, spv, cx, cy;
    (void)bottom;
    xmb_metrics(left, top, right - left, &icon, &sph, &spv, &cx, &cy);
    (void)icon;
    (void)spv;
    (void)cy;
    return cx + (index - s_cat) * sph;
}

int xmb_pico_test_cat_cy(int left, int top, int right, int bottom, int index)
{
    int icon, sph, spv, cx, cy;
    (void)bottom;
    (void)index;
    xmb_metrics(left, top, right - left, &icon, &sph, &spv, &cx, &cy);
    (void)icon;
    (void)sph;
    (void)spv;
    (void)cx;
    return cy;
}

void xmb_pico_test_wave(int i, int j, int left, int top, int right, int bottom,
                        int *x, int *y)
{
    wave_point(i, j, left, top, right - left, bottom - top, x, y);
}
#endif
