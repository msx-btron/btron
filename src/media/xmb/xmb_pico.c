/*
 * xmb_pico.c — XMB page for the PicoCalc image.
 *
 * Gradient fill, a DST_COLOR_ONE ribbon, 32x32 P8 cells as SRC_OVER, and
 * A1 labels. Rectangles stay on core 0. This file does not link xmb.c,
 * xmb_wave.c, or the atlas.
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

static void ribbon(int left, int right, int y0, int y1)
{
    int span = right - left;
    int head, y, x;
    if (span < 1) return;
    if (left < 0) left = 0;
    if (right > 320) right = 320;
    head = (s_phase % (span + 64)) - 32;
    for (y = y0; y < y1; y++) {
        uint16_t *row = dp_pico_band_row(y);
        if (!row) continue;
        for (x = left; x < right; x++) {
            row[x] = brighten(row[x], 1, 4);
            if (x >= left + head && x < left + head + 40)
                row[x] = brighten(row[x], 1, 2);
        }
    }
}

static void pad(int x, int y)
{
    int py, px;
    for (py = y - 3; py < y + XMB_PICO_CELL + 3; py++) {
        uint16_t *row = dp_pico_band_row(py);
        if (!row) continue;
        for (px = x - 3; px < x + XMB_PICO_CELL + 3; px++) {
            if (px < 0 || px >= 320) continue;
            row[px] = brighten(row[px], 1, 3);
        }
    }
}

static void blit_cell(int cell, int x, int y, int selected)
{
    const unsigned char *px = xmb_pico_cell(cell);
    int row, col;
    if (!px) return;
    for (row = 0; row < XMB_PICO_CELL; row++) {
        uint16_t *dst = dp_pico_band_row(y + row);
        const unsigned char *src;
        if (!dst) continue;
        src = px + row * XMB_PICO_CELL;
        for (col = 0; col < XMB_PICO_CELL; col++) {
            int a = src[col];
            int dx = x + col;
            if (a == 0 || dx < 0 || dx >= 320) continue;
            if (!selected) a = (a * 160) >> 8;
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

static int row_x(int n, int left, int right, int index, int *pitch)
{
    int width = right - left;
    int span, x0;
    *pitch = 44;
    if (n < 1) return left;
    span = 32 + (n - 1) * (*pitch);
    if (span > width && n > 1) {
        *pitch = (width - 32) / (n - 1);
        if (*pitch < 32) *pitch = 32;
        span = 32 + (n - 1) * (*pitch);
    }
    x0 = left + (width - span) / 2;
    if (x0 < left) x0 = left;
    return x0 + index * (*pitch);
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
    s_phase += 8;
    if (s_phase > 100000) s_phase = 0;
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

void xmb_pico_paint(int left, int top, int right, int bottom)
{
    const xmb_cat_t *cat;
    int nc = (int)(sizeof s_cats / sizeof s_cats[0]);
    int pitch = 44;
    int i;
    int cat_y, item_y;
    if (right <= left || bottom <= top) return;
    if (s_cat < 0 || s_cat >= nc) s_cat = 0;
    cat = &s_cats[s_cat];
    if (s_item < 0 || s_item >= cat->n) s_item = 0;

    fill_gradient(left, top, right, bottom);
    cat_y = top + 36;
    item_y = top + 112;
    ribbon(left, right, cat_y - 8, cat_y + XMB_PICO_CELL + 8);

    for (i = 0; i < nc; i++) {
        int x = row_x(nc, left, right, i, &pitch);
        if (i == s_cat) pad(x, cat_y);
        blit_cell(s_cats[i].icon, x, cat_y, i == s_cat);
    }
    label((left + right) / 2, cat_y + XMB_PICO_CELL + 4, cat->label);

    for (i = 0; i < cat->n; i++) {
        int x = row_x(cat->n, left, right, i, &pitch);
        if (i == s_item) pad(x, item_y);
        blit_cell(cat->items[i].icon, x, item_y, i == s_item);
    }
    if (cat->n > 0)
        label((left + right) / 2, item_y + XMB_PICO_CELL + 4, cat->items[s_item].label);
#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
    host_flush(left, top, right, bottom);
#endif
}
