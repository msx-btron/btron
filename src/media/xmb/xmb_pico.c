/*
 * xmb_pico.c — XMB surface for the PicoCalc image.
 *
 * Gradient, a perspective wave of flat triangles, 32x32 P8 cells as
 * SRC_OVER, and A1 labels. The selected category is locked to the center.
 * On the device the wave is screen-space triangles on core 1. Icons,
 * labels, and the gradient stay on core 0. This file does not link
 * xmb.c, xmb_wave.c, or the atlas.
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

#define GLbyte signed char
#include "../../desktop/graphics/tinygl/font8x8_basic.h"
#undef GLbyte

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

static const xmb_item_t s_items_apps[] = {
    { IC_APP_TERM,   "Terminal", XMB_GO_TERMINAL },
    { IC_APP_EDITOR, "Editor",   XMB_GO_EDITOR },
    { IC_APP_TAD,    "Browser",  XMB_GO_BROWSER },
    { IC_APP_DRIVE,  "Cabinet",  XMB_GO_CABINET },
    { IC_GEAR,       "GLGears",  XMB_GO_GEARS },
    { IC_APP_CHAT,   "Chat",     XMB_GO_CHAT }
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
static int s_glow;
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

/* DST_COLOR_ONE: move the destination toward white by num/den. */
static uint16_t brighten(uint16_t be, int num, int den)
{
    int r, g, b;
    unpack(be, &r, &g, &b);
    r += (31 - r) * num / den;
    g += (63 - g) * num / den;
    b += (31 - b) * num / den;
    return pack_be(r, g, b);
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

static int slen(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void fill_gradient(int left, int top, int right, int bottom)
{
    int y, h, x0, x1;
    if (right > 320) right = 320;
    if (left < 0) left = 0;
    h = bottom - top;
    if (h < 1 || left >= right) return;
    x0 = left;
    x1 = right;
    for (y = top; y < bottom; y++) {
        uint16_t *row = dp_pico_band_row(y);
        int t, r, g, b, x;
        uint16_t px;
        if (!row) continue;
        t = (y - top) * 256 / h;
        if (t < 0) t = 0;
        if (t > 256) t = 256;
        r = 4 + ((1 - 4) * t) / 256;
        g = 12 + ((3 - 12) * t) / 256;
        b = 18 + ((6 - 18) * t) / 256;
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
        px = pack_be(r, g, b);
        for (x = x0; x < x1; x++) row[x] = px;
    }
}

#if !defined(PICO_REAL_HW)
static void hline_dim(int y, int x0, int x1, int num)
{
    uint16_t *row;
    int x;
    if (y < 0 || y >= 320 || x0 >= x1) return;
    row = dp_pico_band_row(y);
    if (!row) return;
    if (x0 < 0) x0 = 0;
    if (x1 > 320) x1 = 320;
    for (x = x0; x < x1; x++) row[x] = brighten(row[x], num, 8);
}

/* Integer floor. Lines bunch at the horizon and spread toward the bottom.
 * The icons stay put. s_glow only changes how hard the lines catch light. */
static void floor_surface(int left, int right, int bottom, int cx, int horizon)
{
    int depth = bottom - horizon;
    int shine = 1 + ((s_glow >> 4) & 1);
    int i, y;
    if (depth < 8 || left >= right) return;
    for (i = 1; i <= 6; i++) {
        int half;
        y = horizon + (i * i * depth) / 40;
        if (y >= bottom || y < 0) continue;
        half = (y - horizon) * (right - left) / (depth * 2);
        if (half < 6) half = 6;
        hline_dim(y, cx - half, cx + half, shine);
    }
    for (i = -4; i <= 4; i++) {
        int x1;
        if (i == 0) continue;
        x1 = cx + i * ((right - left) / 10);
        for (y = horizon; y < bottom; y += 3) {
            uint16_t *row = dp_pico_band_row(y);
            int x;
            if (!row) continue;
            x = cx + (x1 - cx) * (y - horizon) / depth;
            if (x >= left && x < right && x >= 0 && x < 320)
                row[x] = brighten(row[x], shine, 7);
        }
    }
}
#endif /* !PICO_REAL_HW */

static void pad(int x, int y, int size)
{
    int py, px;
    for (py = y - 2; py < y + size + 2; py++) {
        uint16_t *row = dp_pico_band_row(py);
        if (!row) continue;
        for (px = x - 2; px < x + size + 2; px++) {
            if (px < 0 || px >= 320) continue;
            row[px] = brighten(row[px], 1, 3);
        }
    }
}

static void blit_cell(int cell, int x, int y, int size, int selected)
{
    const unsigned char *px = xmb_pico_cell(cell);
    int row, col, step;
    if (!px || size < 8) return;
    if (size > XMB_PICO_CELL) size = XMB_PICO_CELL;
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
            if (!selected) a = (a * 140) >> 8;
            if (a == 0) continue;
            dst[dx] = src_over(dst[dx], a);
        }
    }
}

static void label(int cx, int y, const char *s)
{
    int n = slen(s);
    int x = cx - n * 4;
    int i;
    uint16_t pen = pack_be(INK_R, INK_G, INK_B);
    if (n <= 0) return;
    if (x < 0) x = 0;
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        int r;
        for (r = 0; r < 8; r++) {
            uint16_t *row = dp_pico_band_row(y + r);
            unsigned bits;
            int col;
            if (!row) continue;
            bits = (unsigned)(unsigned char)font8x8_basic[ch][r];
            for (col = 0; col < 8; col++) {
                int dx = x + i * 8 + col;
                if (dx < 0 || dx >= 320) continue;
                if (bits & (1u << col)) row[dx] = pen;
            }
        }
    }
}

static int wrap_d(int i, int sel, int n)
{
    int d = i - sel;
    if (n <= 1) return 0;
    if (d > n / 2) d -= n;
    if (d < -(n / 2)) d += n;
    return d;
}

static int icon_size(int ad)
{
    int size = 32 - ad * 8;
    if (size < 16) size = 16;
    return size;
}

/* Selected category and the selected item share one x: the body center. */
static void selected_cell(int left, int top, int right,
                          int *cat_x, int *cat_y, int *item_x, int *item_y)
{
    int cx = left + (right - left) / 2;
    int x = cx - 16;
    if (cat_x) *cat_x = x;
    if (cat_y) *cat_y = top + 36;
    if (item_x) *item_x = x;
    if (item_y) *item_y = top + 108;
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
    s_glow++;
    if (s_glow > 100000) s_glow = 0;
}

void xmb_pico_move(int dcat, int ditem)
{
    int nc = (int)(sizeof s_cats / sizeof s_cats[0]);
    if (dcat) {
        s_cat += dcat;
        if (s_cat < 0) s_cat = nc - 1;
        if (s_cat >= nc) s_cat = 0;
        s_item = 0;
    }
    if (ditem) {
        int n = s_cats[s_cat].n;
        if (n < 1) return;
        s_item += ditem;
        if (s_item < 0) s_item = n - 1;
        if (s_item >= n) s_item = 0;
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
    int cx, cy, ix, iy;
    (void)bottom;
    if (s_cat < 0 || s_cat >= nc) s_cat = 0;
    if (s_item < 0 || s_item >= s_cats[s_cat].n) s_item = 0;
    selected_cell(left, top, right, &cx, &cy, &ix, &iy);
    (void)cy;
    (void)iy;
    if (cat_x) *cat_x = cx;
    if (item_x) *item_x = ix;
    if (cat) *cat = s_cat;
    if (item) *item = s_item;
}

#if defined(PICO_REAL_HW)
/* Side-on sheet. The viewpoint and the pixel amplitude stay on the 8×5
 * curve (lean 5..20, wobble up to ±13*(2..7)). The lattice is 16×12 so
 * the facets read as a fabric. Two traveling terms plus the cross term
 * are the original ribbon's cos(2*gx), cos(4*gz) and cos(gz+gx), scaled
 * back into that same ±13. The lift is white and steeper folds are
 * brighter, which is glColor(b,b,b) with GL_DST_COLOR, GL_ONE. */
#define WAVE_NX 16
#define WAVE_NY 12
#define WAVE_REF 5
static const int8_t s_wave_sin[16] = {
    0, 5, 9, 12, 13, 12, 9, 5, 0, -5, -9, -12, -13, -12, -9, -5
};

static int wave_idx(int n)
{
    n &= 15;
    return n;
}

static void wave_point(int i, int j, int left, int top, int width, int height,
                       int phase, int *ox, int *oy)
{
    int across = s_wave_sin[wave_idx(i * 2 - phase * 2 + 256)];
    int along = s_wave_sin[wave_idx(j * 3)];
    int cross = s_wave_sin[wave_idx(i + j + phase + 256)];
    int wob = (across * 2 + along + cross) / 4;
    int sway = s_wave_sin[wave_idx(j * 2 + phase + 256)];
    int amp = 2 + (j * WAVE_REF) / WAVE_NY;
    int lean = 5 + (j * 3 * WAVE_REF) / WAVE_NY;
    int y = top + (j * j * height) / (WAVE_NY * WAVE_NY);
    int x = left + width / 2 + (i - WAVE_NX / 2) * (width * lean) / (WAVE_NX * 18);
    y += wob * amp;
    x += sway;
    *ox = x;
    *oy = y;
}

static int wave_base_y(int j, int top, int height)
{
    return top + (j * j * height) / (WAVE_NY * WAVE_NY);
}

static void wave_surface(int left, int top, int right, int bottom)
{
    int y, y0, y1, band_y0, width, height, phase, j, i;
    uint16_t *row0;
    y0 = -1;
    y1 = -1;
    row0 = 0;
    width = right - left;
    height = bottom - top;
    if (width < 8 || height < 8) return;
    for (y = 0; y < 320; y++) {
        uint16_t *row = dp_pico_band_row(y);
        if (!row) continue;
        if (y0 < 0) {
            y0 = y;
            row0 = row;
        }
        y1 = y;
    }
    if (y0 < 0 || !row0) return;
    band_y0 = y0 & ~15;
    phase = (s_glow >> 1) & 15;
    picogl_target_push();
    picogl_bind_target(row0 - (y0 - band_y0) * 320, 0, 320, 16, 320);
    picogl_set_depth(0);
    for (j = 0; j < WAVE_NY; j++) {
        for (i = 0; i < WAVE_NX; i++) {
            int xa, ya, xb, yb, xc, yc, xd, yd;
            int ytop, ybot;
            wave_point(i, j, left, top, width, height, phase, &xa, &ya);
            wave_point(i + 1, j, left, top, width, height, phase, &xb, &yb);
            wave_point(i + 1, j + 1, left, top, width, height, phase, &xc, &yc);
            wave_point(i, j + 1, left, top, width, height, phase, &xd, &yd);
            ytop = ya;
            if (yb < ytop) ytop = yb;
            if (yc < ytop) ytop = yc;
            if (yd < ytop) ytop = yd;
            ybot = ya;
            if (yb > ybot) ybot = yb;
            if (yc > ybot) ybot = yc;
            if (yd > ybot) ybot = yd;
            if (ybot < y0 || ytop > y1) continue;
            {
                int gap = wave_base_y(j + 1, top, height) - wave_base_y(j, top, height);
                int extra = (yc + yd) - (ya + yb) - gap * 2;
                int bri;
                uint16_t lift;
                if (extra < 0) extra = -extra;
                extra >>= 1;
                bri = 1;
                if (extra > 4) bri = 2;
                if (extra > 12) bri = 3;
                lift = (uint16_t)((bri << 11) | ((bri * 2) << 5) | bri);
                picogl_screen_lift(xa, ya - band_y0, xb, yb - band_y0,
                                   xc, yc - band_y0, lift);
                picogl_screen_lift(xa, ya - band_y0, xc, yc - band_y0,
                                   xd, yd - band_y0, lift);
            }
        }
    }
    picogl_flush_target();
    picogl_target_pop();
}
#endif

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
    int i, pass;
    int cat_x, cat_y, item_x, item_y;
    if (right <= left || bottom <= top) return;
    if (s_cat < 0 || s_cat >= nc) s_cat = 0;
    cat = &s_cats[s_cat];
    if (s_item < 0 || s_item >= cat->n) s_item = 0;
    selected_cell(left, top, right, &cat_x, &cat_y, &item_x, &item_y);

    fill_gradient(left, top, right, bottom);
#if defined(PICO_REAL_HW)
    /* Core 1 fills the sheet while the previous band is still on the SPI.
     * Icons are drawn after the join so they sit on the wave. */
    wave_surface(left, top, right, bottom);
#else
    floor_surface(left, right, bottom, cat_x + 16, cat_y - 8);
#endif

    for (pass = 3; pass >= 0; pass--) {
        for (i = 0; i < nc; i++) {
            int d = wrap_d(i, s_cat, nc);
            int ad = d < 0 ? -d : d;
            int size, x, y;
            if (ad != pass) continue;
            size = icon_size(ad);
            x = (cat_x + 16) + d * 40 - size / 2;
            y = cat_y - ad * 10;
            if (i == s_cat) pad(x, y, size);
            blit_cell(s_cats[i].icon, x, y, size, i == s_cat);
        }
    }
    label(cat_x + 16, cat_y + XMB_PICO_CELL + 4, cat->label);

    for (pass = 6; pass >= 0; pass--) {
        for (i = 0; i < cat->n; i++) {
            int d = i - s_item;
            int ad = d < 0 ? -d : d;
            int size, x, y;
            if (ad != pass) continue;
            size = icon_size(ad);
            x = (item_x + 16) - size / 2;
            y = item_y + d * 44;
            if (y + size < top || y >= bottom) continue;
            if (i == s_item) pad(x, y, size);
            blit_cell(cat->items[i].icon, x, y, size, i == s_item);
        }
    }
    if (cat->n > 0)
        label(item_x + 16, item_y + XMB_PICO_CELL + 4, cat->items[s_item].label);
#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
    host_flush(left, top, right, bottom);
#endif
}
