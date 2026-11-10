/*
 * B-System (BTRON 3.20) Window Manager: wnd.c
 * Sakamura style retro double-bordered windows and client region management.
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/fast_blit.h>
#include <btron/fused_row.h>
#include "pmc.h"
#include <btron/libc_shim.h>

__attribute__((weak)) void btron_cooperative_input_pump(void) {}

static WND *g_wnd_head = NULL;
static ID g_next_wnd_id = 1;
static GDEV *g_screen_dev = NULL;

/* Accumulated app invalidation damage for the pending composite.  Apps render
 * into their offscreen GDEV and call inval_wnd(); without this the UI plane had
 * no way to learn what a click changed and repainted the whole top window (or
 * the whole desktop) on every button edge. */
static RECT s_wnd_inval_damage = { 0, 0, 0, 0 };
static BOOL s_wnd_inval_valid = FALSE;

/* The EE/GS layer's two facts (doc/txt/PS2.txt, increment 5c).  A layer is a property of the
 * window (`wnd->gl_layer`, set by egl_create_window_surface()), so the layer is found by
 * walking the stack rather than remembered here -- one truth, and no second copy of the z-order
 * that can go out of date when a window closes or a second GL app opens.  The topmost wins when
 * more than one is visible: the environment packet names one rect, so a second layer draws
 * inside the first one's, which is increment 5c's documented limit rather than a secret.
 *
 * `s_gl_pending` is set by the composite walk when it runs a layer window's paint callback, i.e.
 * when a GIF stream now exists that nobody has put on the bus yet; `s_gl_client` is that window's
 * client rect in screen coordinates, which is where the environment packet's XYOFFSET and SCISSOR
 * come from.  Both are cleared by wnd_gl_layer_take(), so a present that does not happen cannot
 * make the next one draw a stale stream -- the staging buffer is rewritten by the next frame
 * that records. */
static RECT s_gl_client = { 0, 0, 0, 0 };
static BOOL s_gl_pending = FALSE;
/* The last rect the window that is the layer NOW had, kept for the passes where that window is on
 * the screen but its device is momentarily gone.  The GS owns those pixels either way, so the
 * present path must keep cutting them out of the canvas upload; a stale rect here costs one frame of
 * desktop, while a missing one costs the picture.  Keyed by window, so a different layer can never
 * inherit it.  See wnd_gl_layer_rect(). */
static RECT s_gl_rect_last = { 0, 0, 0, 0 };
static WND *s_gl_rect_last_wnd;
/* Task #165: the layer's one-paint-per-pass latch, and it is a LATCH and not a policy.
 *
 * A layer window is the only window in this walk whose staleness is not a fact about its image:
 * `wnd_owes_callback()` gives it the callback whenever a band reaches it, because its picture is
 * the stream the callback records and there is no pixmap to blit.  That is right for the band that
 * carries the stream and wasteful for every later band of the same pass, which today re-records
 * an identical stream and sends it again -- and it is why the latch has to outrank `img_valid`
 * rather than sit beside it: an app that animates dirties its own client rect every frame, so an
 * `img_valid` term would re-run the callback on a pass that just refused to afford it.  Only the
 * presentation plane knows which passes already have a frame of this window on the page, so the
 * decision lives there and this flag is the seam: a caller that has put the layer's frame on the GS
 * -- or has decided this frame cannot pay for one -- raises it, and the rest of the walk behaves
 * exactly as it would for a window whose image is valid.
 *
 * It is clear at boot and nothing outside the PS2 core's paint pass ever raises it, so every other
 * target compiles the same two boolean tests into the same answer they always gave.  See
 * wnd_gl_freeze(). */
static BOOL s_gl_frozen = FALSE;
static RECT s_gl_cover[WND_GL_COVER_MAX];
static int s_gl_cover_n;
static int s_gl_cover_dropped;
static int s_gl_cover_merged;   /* adds the list refused because an earlier rect already covered them */
/* -1 = machine default (PS2: 1 TinyGL per composite, round-robin;
 * hosted: unlimited).  s_gl_pick is the window this pass may rasterize;
 * s_gl_rr_last is where the next pass resumes so closing a window
 * advances the remaining ones instead of freezing them. */
static int s_gl_pass_quota = -1;
static WND *s_gl_pick;
static WND *s_gl_rr_last;
static int s_gl_layer_recorded; /* first EEGS stream has landed; freeze may apply */
/* Grip-held resize: chrome updates per MOVE, GDEV realloc waits for flush.
 * Declared here because rsz_wnd (above the event dispatcher) reads it to
 * decide GDEV slack. */
static BOOL s_wnd_resizing = FALSE;
static WND *s_gdev_oom_wnd;
static H    s_gdev_oom_w;
static H    s_gdev_oom_h;

static void wnd_mgr_window_destroyed(WND *wnd);
static void wnd_inval_rect(const RECT *r);

/* The GL surface bridge's binder, installed by wnd_set_gl_bind().  NULL on every target and
 * suite that links no OpenGL stack, which is why this is a registered pointer and not a call:
 * see the header comment on WndGLBind. */
static WndGLBind s_gl_bind = NULL;

void wnd_set_gl_bind(WndGLBind bind) { s_gl_bind = bind; }

/* The window whose paint callback currently owns the GL context (NULL outside a paint),
 * and how many nested paints the scope refused.  Single-threaded compositor state. */
static WND *s_gl_scope_owner;
static uint32_t s_gl_nest_refused;
uint32_t wnd_gl_nest_refused(void) { return s_gl_nest_refused; }
WND *wnd_gl_scope_owner(void) { return s_gl_scope_owner; }

ER init_wnd_mgr(GDEV *screen_dev) { if (!screen_dev) return E_PAR;
    g_screen_dev = screen_dev;
    s_gl_pick = NULL;
    s_gl_rr_last = NULL;
    s_gl_layer_recorded = 0;
    /* Preserve existing windows if any were opened before screen attachment */
    return E_OK;
}

GDEV* wnd_mgr_get_screen(void) {
    return g_screen_dev;
}

/* Width of the title as it actually renders, using the same per-glyph advance
 * as drw_tc_string so the compact tab always fits its caption text exactly. */
static H calculate_title_display_width(const char *s) {
    if (!s) return 0;
    return tc_calc_string_width(s, 64 /* WND title buffer */);
}

#define WND_TITLE_HEIGHT 28

ER wget_tab_rect(const WND *wnd, RECT *tab_rect) {
    if (!wnd || !tab_rect) return E_PAR;
    if (!(wnd->attr & WND_ATTR_TITLE)) {
        tab_rect->left = tab_rect->right = tab_rect->top = tab_rect->bottom = 0;
        return E_OK;
    }
    H w = wnd->bounds.right - wnd->bounds.left;
    H tw = wnd->tab_width;
    if (tw <= 0 || tw > w || !(wnd->attr & WND_ATTR_COMPACT_TAB)) {
        tw = w;
    }
    H off_x = wnd->tab_offset_x;
    if (off_x < 0) off_x = 0;
    if (off_x + tw > w) {
        off_x = w - tw;
        if (off_x < 0) off_x = 0;
    }

    tab_rect->left = wnd->bounds.left + off_x;
    tab_rect->top = wnd->bounds.top;
    tab_rect->right = tab_rect->left + tw;
    tab_rect->bottom = wnd->bounds.top + WND_TITLE_HEIGHT;
    return E_OK;
}

BOOL whit_test_tab(const WND *wnd, H x, H y) {
    if (!wnd || !(wnd->attr & WND_ATTR_TITLE)) return FALSE;
    if (g_wm_style == WM_STYLE_CHOKANJI) {
        return pmc_hit_test_title(wnd, x, y);
    }
    RECT tr;
    if (wget_tab_rect(wnd, &tr) != E_OK) return FALSE;
    return (x >= tr.left && x < tr.right && y >= tr.top && y < tr.bottom);
}

BOOL whit_test_close_btn(const WND *wnd, H x, H y) {
    if (!wnd || !(wnd->attr & WND_ATTR_TITLE) || !(wnd->attr & WND_ATTR_CLOSE)) return FALSE;
    if (g_wm_style == WM_STYLE_CHOKANJI) {
        return pmc_hit_test_close(wnd, x, y);
    }
    RECT tr;
    if (wget_tab_rect(wnd, &tr) != E_OK) return FALSE;
    H btn_right = tr.right - 6;
    H btn_left = tr.right - 24;
    H btn_top = tr.top + 6;
    H btn_bottom = tr.top + 22;
    return (x >= btn_left && x < btn_right && y >= btn_top && y < btn_bottom);
}

ER wset_tab_offset(WND *wnd, H offset_x) {
    if (!wnd) return E_PAR;
    H w = wnd->bounds.right - wnd->bounds.left;
    H tw = wnd->tab_width;
    if (tw <= 0 || tw > w || !(wnd->attr & WND_ATTR_COMPACT_TAB)) {
        tw = w;
    }
    H max_off = (w > tw) ? (w - tw) : 0;
    if (offset_x < 0) offset_x = 0;
    if (offset_x > max_off) offset_x = max_off;
    wnd->tab_offset_x = offset_x;
    return E_OK;
}

WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr) {
    WND *wnd = (WND*)calloc(1, sizeof(WND));
    if (!wnd) return NULL;

    wnd->id = g_next_wnd_id++;
    wnd->paint = NULL;
    wnd->event_handler = NULL;
    wnd->menu_open = NULL;
    wnd->user_data = 0;

    const char *src_t = title ? title : "BTRON Window";
    int ti = 0;
    while (src_t[ti] != '\0') {
        unsigned char c = (unsigned char)src_t[ti];
        int step = 1;
        if ((c & 0x80) == 0) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        if (ti + step >= 64) break;
        for (int k = 0; k < step; k++) {
            wnd->title[ti + k] = src_t[ti + k];
        }
        ti += step;
    }
    wnd->title[ti] = '\0';
    wnd->bounds.left = x;
    wnd->bounds.top = y;
    wnd->bounds.right = x + w;
    wnd->bounds.bottom = y + h;

    H title_h = (attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
    H border = (attr & WND_ATTR_BORDER) ? 4 : 0;
    wnd->client.left = x + border;
    wnd->client.top = y + title_h + border;
    wnd->client.right = x + w - border;
    wnd->client.bottom = y + h - border;

    /* BTRON3 3.20 Conformance: Windows with borders/titles support corner resize and sliding tabs */
    if (attr & (WND_ATTR_TITLE | WND_ATTR_BORDER)) {
        attr |= WND_ATTR_RESIZE | WND_ATTR_COMPACT_TAB | WND_ATTR_SLIDING_TAB;
    }
    wnd->attr = attr;
    wnd->visible = TRUE;
    wnd->focused = TRUE;
    wnd->tab_offset_x = 0;

    /* Calculate compact tab width from the title content. It is a pure
     * function of the caption text and is deliberately NOT clamped to the
     * window width here (wget_tab_rect clamps at draw time), so resizing a
     * window never permanently grows or shrinks the title tab. */
    H text_w = calculate_title_display_width(wnd->title);
    H tw = text_w + 48; /* text + left margin/grip + close box + padding */
    if (tw < 100) tw = 100;
    wnd->tab_width = tw;

    H dev_w = (w - border * 2) & ~3;
    wnd->dev = opn_dev(dev_w, h - title_h - border * 2);

    /* Insert at head of z-stack */
    wnd->next = g_wnd_head;
    wnd->prev = NULL;
    if (g_wnd_head) {
        g_wnd_head->focused = FALSE;
        g_wnd_head->prev = wnd;
    }
    g_wnd_head = wnd;

    return wnd;
}

ER cls_wnd(WND *wnd) {
    if (!wnd) return E_PAR;
    if (!wnd_mgr_contains(wnd)) return E_PAR;

    wnd_mgr_window_destroyed(wnd);
    if (wnd->destroy) {
        wnd->destroy(wnd);
    }

    if (wnd->prev) wnd->prev->next = wnd->next;
    if (wnd->next) wnd->next->prev = wnd->prev;
    if (g_wnd_head == wnd) g_wnd_head = wnd->next;

    if (s_gl_rr_last == wnd) s_gl_rr_last = NULL;
    if (s_gl_pick == wnd) s_gl_pick = NULL;

    if (g_wnd_head) {
        g_wnd_head->focused = TRUE;
        /* Remaining top window owes a frame: the slot the closed window
         * occupied in the round-robin is now this one's. */
        g_wnd_head->img_valid = FALSE;
        wnd_inval_rect(&g_wnd_head->bounds);
    }

    /* The vacated rect is desktop (and possibly lower windows) from now on. */
    wnd_inval_rect(&wnd->bounds);

    /* Closing the layer window ends the layer: the next composite draws desktop into that rect,
     * and a present path that still believed a stream was pending would draw it over desktop. */
    if (wnd->gl_layer) {
        s_gl_pending = FALSE;
        s_gl_layer_recorded = 0;
        s_gl_cover_n = 0;
        s_gl_cover_dropped = 0;
        s_gl_cover_merged = 0;
        /* The rect cache is keyed by window, and this window is about to be freed -- a later layer
         * allocated at the same address must not inherit the dead one's rect. */
        if (s_gl_rect_last_wnd == wnd) s_gl_rect_last_wnd = NULL;
    }

    if (s_gdev_oom_wnd == wnd) s_gdev_oom_wnd = NULL;
    if (wnd->dev) cls_dev(wnd->dev);
    free(wnd);
    return E_OK;
}

ER top_wnd(WND *wnd) {
    if (!wnd) return E_PAR;
    if (g_wnd_head == wnd) {
        wnd->focused = TRUE;
        return E_OK;
    }

    /* Verify wnd is actually present in active window list */
    BOOL found = FALSE;
    for (WND *c = g_wnd_head; c; c = c->next) {
        if (c == wnd) {
            found = TRUE;
            break;
        }
    }
    if (!found) return E_PAR;

    /* Remove from current position */
    if (wnd->prev) wnd->prev->next = wnd->next;
    if (wnd->next) wnd->next->prev = wnd->prev;

    /* Move to top */
    if (g_wnd_head) g_wnd_head->focused = FALSE;
    wnd->next = g_wnd_head;
    wnd->prev = NULL;
    if (g_wnd_head) g_wnd_head->prev = wnd;
    g_wnd_head = wnd;
    wnd->focused = TRUE;
    /* New focus owes a frame; round-robin resumes at the head. */
    wnd->img_valid = FALSE;
    s_gl_rr_last = NULL;
    wnd_inval_rect(&wnd->bounds);

    return E_OK;
}

ER mov_wnd(WND *wnd, H x, H y) {
    if (!wnd) return E_PAR;

    RECT old_r = wnd->bounds;
    H w = wnd->bounds.right - wnd->bounds.left;
    H h = wnd->bounds.bottom - wnd->bounds.top;

    wnd->bounds.left = x;
    wnd->bounds.top = y;
    wnd->bounds.right = x + w;
    wnd->bounds.bottom = y + h;

    H title_h = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
    wnd->client.left = x + 4;
    wnd->client.top = y + title_h + 4;
    wnd->client.right = x + w - 4;
    wnd->client.bottom = y + h - 4;

    wnd_inval_rect(&old_r);
    wnd_inval_rect(&wnd->bounds);

    return E_OK;
}

static H gdev_cap_w(const GDEV *d)
{
    return (d && d->cap_w > 0) ? d->cap_w : (d ? d->width : 0);
}
static H gdev_cap_h(const GDEV *d)
{
    return (d && d->cap_h > 0) ? d->cap_h : (d ? d->height : 0);
}
static void gdev_set_view(GDEV *d, H w, H h)
{
    if (!d) return;
    d->width = w;
    d->height = h;
    d->clip.left = 0;
    d->clip.top = 0;
    d->clip.right = w;
    d->clip.bottom = h;
}
/* Grow slack while the grip is held so OHCI MOVE bursts do not realloc
 * the pixmap every report.  64 px on hosted; 16 px on PS2 so a just-EEGS
 * client (321×221) asks ~301 kB instead of 384×256 = 384 kB, which is
 * larger than the typical first-fit hole after the eight-app set. */
#if defined(BTRON_PS2_TARGET)
#define GDEV_SLACK 16
#else
#define GDEV_SLACK 64
#endif

static void gdev_slack_cap(H need_w, H need_h, int grow_slack, H *cap_w, H *cap_h)
{
    *cap_w = need_w;
    *cap_h = need_h;
    if (!grow_slack) return;
    *cap_w = (H)((need_w + (GDEV_SLACK - 1)) & ~(GDEV_SLACK - 1));
    *cap_h = (H)((need_h + (GDEV_SLACK - 1)) & ~(GDEV_SLACK - 1));
    if (*cap_w < need_w) *cap_w = need_w;
    if (*cap_h < need_h) *cap_h = need_h;
}

static GDEV *gdev_acquire(H need_w, H need_h, int grow_slack)
{
    H cap_w, cap_h;
    GDEV *d;
    gdev_slack_cap(need_w, need_h, grow_slack, &cap_w, &cap_h);
    d = opn_dev(cap_w, cap_h);
    if (!d && grow_slack && (cap_w != need_w || cap_h != need_h))
        d = opn_dev(need_w, need_h);
    if (!d) return NULL;
    gdev_set_view(d, need_w, need_h);
    return d;
}

/* Replace the pixmap.  Alloc the new buffer first so a failure keeps the
 * old one.  If that fails, free the old buffer and retry so first-fit can
 * coalesce.  Never leaves d->pixels NULL when the old pixmap existed. */
static int gdev_fit(GDEV *d, H need_w, H need_h, int grow_slack)
{
    H cap_w, cap_h, old_cw, old_ch, old_vw, old_vh;
    COLOR *npx;
    size_t n;
    if (!d || d->is_vram) return 0;
    gdev_slack_cap(need_w, need_h, grow_slack, &cap_w, &cap_h);
    old_cw = gdev_cap_w(d);
    old_ch = gdev_cap_h(d);
    old_vw = d->width;
    old_vh = d->height;
    if (old_cw >= cap_w && old_ch >= cap_h) {
        gdev_set_view(d, need_w, need_h);
        if (!grow_slack) {
            d->cap_w = need_w;
            d->cap_h = need_h;
        }
        return 1;
    }
    n = (size_t)cap_w * (size_t)cap_h;
    npx = (COLOR *)calloc(n, sizeof(COLOR));
    if (!npx && grow_slack && (cap_w != need_w || cap_h != need_h)) {
        cap_w = need_w;
        cap_h = need_h;
        n = (size_t)cap_w * (size_t)cap_h;
        npx = (COLOR *)calloc(n, sizeof(COLOR));
    }
    /* During a drag, keep the old pixmap.  Free-old-and-retry was the
     * UART storm: fail 459 kB, free 416, land 427, hole shrinks, next
     * MOVE asks more. */
    if (!npx && grow_slack)
        return 0;
    if (!npx) {
        COLOR *oldp = d->pixels;
        d->pixels = NULL;
        free(oldp);
        n = (size_t)cap_w * (size_t)cap_h;
        npx = (COLOR *)calloc(n, sizeof(COLOR));
        if (!npx && (cap_w != need_w || cap_h != need_h)) {
            cap_w = need_w;
            cap_h = need_h;
            n = (size_t)cap_w * (size_t)cap_h;
            npx = (COLOR *)calloc(n, sizeof(COLOR));
        }
        if (!npx) {
            n = (size_t)old_vw * (size_t)old_vh;
            if (n == 0) n = 1;
            npx = (COLOR *)calloc(n, sizeof(COLOR));
            if (!npx) return 0;
            cap_w = old_vw;
            cap_h = old_vh;
            need_w = old_vw;
            need_h = old_vh;
        }
    } else {
        free(d->pixels);
    }
#if defined(BTRON_PS2_TARGET)
    {
        COLOR *px = npx;
        int i, nn = (int)cap_w * (int)cap_h;
        for (i = 0; i < nn; i++) px[i] = COLOR_LTGRAY;
    }
#endif
    d->pixels = npx;
    d->cap_w = cap_w;
    d->cap_h = cap_h;
    gdev_set_view(d, need_w, need_h);
    return 1;
}

static void wnd_apply_chrome(WND *wnd, H w, H h)
{
    wnd->bounds.right = wnd->bounds.left + w;
    wnd->bounds.bottom = wnd->bounds.top + h;

    H title_h = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
    H border = (wnd->attr & WND_ATTR_BORDER) ? 4 : 0;
    wnd->client.left = wnd->bounds.left + border;
    wnd->client.top = wnd->bounds.top + title_h + border;
    wnd->client.right = wnd->bounds.left + w - border;
    wnd->client.bottom = wnd->bounds.top + h - border;
    wset_tab_offset(wnd, wnd->tab_offset_x);
}

ER wnd_resize_chrome(WND *wnd, H w, H h) {
    RECT old_r;
    if (!wnd) return E_PAR;
    if (w < 160) w = 160;
    if (h < 64) h = 64;
    old_r = wnd->bounds;
    if (old_r.right - old_r.left == w && old_r.bottom - old_r.top == h)
        return E_OK;
    wnd_apply_chrome(wnd, w, h);
    wnd_inval_rect(&old_r);
    wnd_inval_rect(&wnd->bounds);
    return E_OK;
}

ER rsz_wnd(WND *wnd, H w, H h) {
    if (!wnd) return E_PAR;
    if (w < 160) w = 160;
    if (h < 64) h = 64;

    RECT old_r = wnd->bounds;
    int slack = s_wnd_resizing ? 1 : 0;

    wnd_apply_chrome(wnd, w, h);

    H title_h = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
    H border = (wnd->attr & WND_ATTR_BORDER) ? 4 : 0;
    H new_dev_w = (w - border * 2) & ~3;
    H new_dev_h = h - title_h - border * 2;
    if (new_dev_w < 10) new_dev_w = 10;
    if (new_dev_h < 10) new_dev_h = 10;

    if (wnd->dev) {
        H cap_w = gdev_cap_w(wnd->dev);
        H cap_h = gdev_cap_h(wnd->dev);
        int fits = (new_dev_w <= cap_w && new_dev_h <= cap_h);
        int exact = (cap_w == new_dev_w && cap_h == new_dev_h);
        if (wnd->dev->width == new_dev_w && wnd->dev->height == new_dev_h &&
            (slack || exact)) {
            /* view already matches; keep slack until button-up */
        } else if (fits && (slack || exact)) {
            gdev_set_view(wnd->dev, new_dev_w, new_dev_h);
            if (!slack) {
                wnd->dev->cap_w = new_dev_w;
                wnd->dev->cap_h = new_dev_h;
            }
            wnd->img_valid = FALSE;
        } else if (s_gdev_oom_wnd == wnd) {
            /* This window already missed a grow; keep the pixmap.
             * `new <= latched` used to miss a growing drag: each MOVE
             * asked a larger size and stormed calloc (0x267b3c). */
        } else if (!gdev_fit(wnd->dev, new_dev_w, new_dev_h, slack)) {
            s_gdev_oom_wnd = wnd;
            s_gdev_oom_w = new_dev_w;
            s_gdev_oom_h = new_dev_h;
        } else {
            if (wnd->dev->width < new_dev_w || wnd->dev->height < new_dev_h) {
                s_gdev_oom_wnd = wnd;
                s_gdev_oom_w = new_dev_w;
                s_gdev_oom_h = new_dev_h;
            } else {
                s_gdev_oom_wnd = NULL;
            }
            wnd->img_valid = FALSE;
        }
    } else {
        wnd->dev = gdev_acquire(new_dev_w, new_dev_h, slack);
        wnd->img_valid = FALSE;
    }

    wnd_inval_rect(&old_r);
    wnd_inval_rect(&wnd->bounds);

    return E_OK;
}

ER wrsz_wnd(WND *wnd, const RECT *r) {
    if (!wnd || !r) return E_PAR;
    H w = r->right - r->left;
    H h = r->bottom - r->top;
    mov_wnd(wnd, r->left, r->top);
    return rsz_wnd(wnd, w, h);
}

static void wnd_inval_rect(const RECT *r) {
    if (!r || r->right <= r->left || r->bottom <= r->top) return;
    if (!s_wnd_inval_valid) {
        s_wnd_inval_damage = *r;
        s_wnd_inval_valid = TRUE;
        return;
    }
    if (r->left < s_wnd_inval_damage.left)   s_wnd_inval_damage.left = r->left;
    if (r->top < s_wnd_inval_damage.top)     s_wnd_inval_damage.top = r->top;
    if (r->right > s_wnd_inval_damage.right) s_wnd_inval_damage.right = r->right;
    if (r->bottom > s_wnd_inval_damage.bottom) s_wnd_inval_damage.bottom = r->bottom;
}

void wnd_inval_damage_rect(const RECT *r) {
    wnd_inval_rect(r);
}

BOOL wnd_take_inval_damage(RECT *out) {
    if (!s_wnd_inval_valid) return FALSE;
    *out = s_wnd_inval_damage;
    s_wnd_inval_damage.left = s_wnd_inval_damage.top = 0;
    s_wnd_inval_damage.right = s_wnd_inval_damage.bottom = 0;
    s_wnd_inval_valid = FALSE;
    return TRUE;
}

/* The topmost visible layer window, or NULL. */
static WND *find_gl_layer(void) {
    for (WND *c = g_wnd_head; c; c = c->next)
        if (c->visible && c->gl_layer) return c;
    return NULL;
}

WND* wnd_gl_layer(void) { return find_gl_layer(); }

int wnd_gl_layer_claim(WND *wnd)
{
    WND *cur;
    if (!wnd) return 0;
    cur = find_gl_layer();
    if (cur && cur != wnd) return 0;
    wnd->gl_layer = TRUE;
    s_gl_layer_recorded = 0;
    s_gl_pending = FALSE;
    return 1;
}

void wnd_gl_layer_release(WND *wnd)
{
    if (!wnd || !wnd->gl_layer) return;
    wnd->gl_layer = FALSE;
    s_gl_pending = FALSE;
    s_gl_layer_recorded = 0;
    if (s_gl_rect_last_wnd == wnd) s_gl_rect_last_wnd = NULL;
}

/* The screen rect a layer window's client covers -- the one rect the GS owns.  It is computed
 * from the window the way the composite computes it, in one place, because the present path has
 * to ask the same question on a pass that did NOT re-record the layer: the composite's copy is
 * only written when the paint callback runs, and a band that reaches the layer's rows without
 * repainting it is exactly the pass that must be told where the canvas has no picture. */
static BOOL gl_client_rect(const WND *wnd, RECT *out) {
    if (!wnd || !wnd->visible || !wnd->gl_layer) return FALSE;
    if (!wnd->dev || !wnd->dev->pixels) return FALSE;
    const H title_h = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
    const H border  = (wnd->attr & WND_ATTR_BORDER) ? 4 : 0;
    const H dest_x  = wnd->bounds.left + border;
    const H dest_y  = wnd->bounds.top + title_h + border;

    out->left   = dest_x;
    out->top    = dest_y;
    out->right  = dest_x + wnd->dev->width;
    out->bottom = dest_y + wnd->dev->height;
    return TRUE;
}

BOOL wnd_gl_layer_rect(RECT *out) {
    WND *top_gl = find_gl_layer();
    if (top_gl) {
        RECT cr;
        if (gl_client_rect(top_gl, &cr)) {
            if (out) *out = cr;
            s_gl_rect_last = cr;
            s_gl_rect_last_wnd = top_gl;
            return TRUE;
        }
    }
    if (s_gl_rect_last_wnd) {
        if (out) *out = s_gl_rect_last;
        return TRUE;
    }
    return FALSE;
}

BOOL wnd_gl_layer_take(RECT *client_out) {
    if (!s_gl_pending) return FALSE;
    if (client_out) *client_out = s_gl_client;
    s_gl_pending = FALSE;
    return TRUE;
}

void wnd_gl_cover_add(const RECT *r) {
    RECT c;
    int i;
    if (!r || r->right <= r->left || r->bottom <= r->top) return;
    /* No layer: nothing was buried, so there is nothing to re-upload and the list must not
     * collect a rect no present path will ever ask for. */
    if (!find_gl_layer()) return;
    /* Clamped to the screen: each rect is re-uploaded on its own, and a caller that adds a
     * rect partly off-screen would otherwise ask the DMA for rows outside the canvas. */
    c = *r;
    if (g_screen_dev) {
        if (c.left < 0) c.left = 0;
        if (c.top < 0) c.top = 0;
        if (c.right > g_screen_dev->width) c.right = g_screen_dev->width;
        if (c.bottom > g_screen_dev->height) c.bottom = g_screen_dev->height;
    }
    if (c.right <= c.left || c.bottom <= c.top) return;
    /* An opaque rect already on the list that contains this one re-lays these very pixels after
     * the stream, so a second upload of the same rows would cost the bus and change the page not
     * at all.  Measured cost of not having this test: one open Tracker menu put the same rect on
     * the list twice per pass -- `global_menu_get_open_rect()` answers the Tracker's rect while
     * the Tracker menu is open, and workbench_cover_overlays() asks both getters -- and the census
     * counted 383,748 px laid where 95,937 distinct were needed. */
    for (i = 0; i < s_gl_cover_n; i++) {
        const RECT *e = &s_gl_cover[i];
        if (e->left <= c.left && e->top <= c.top && e->right >= c.right && e->bottom >= c.bottom) {
            s_gl_cover_merged++;
            return;
        }
    }
    if (s_gl_cover_n >= WND_GL_COVER_MAX) { s_gl_cover_dropped++; return; }
    s_gl_cover[s_gl_cover_n++] = c;
}

int wnd_gl_cover_take(wnd_gl_cover *out) {
    int n = s_gl_cover_n;
    if (out) {
        out->r = s_gl_cover; out->n = n;
        out->dropped = s_gl_cover_dropped;
        out->merged = s_gl_cover_merged;
    }
    s_gl_cover_n = 0;
    s_gl_cover_dropped = 0;
    s_gl_cover_merged = 0;
    return n;
}

ER inval_wnd(WND *wnd) {
    if (!wnd) return E_PAR;
    /* BTRON semantics: the app declares its client image stale, so the next
     * composite over this window must re-run its paint callback. */
    wnd->img_valid = FALSE;
    wnd_inval_rect(&wnd->bounds);
    return E_OK;
}

ER wnd_set_title(WND *wnd, const char *title) {
    if (!wnd || !title) return E_PAR;

    int i = 0;
    for (; i < 63 && title[i]; i++) wnd->title[i] = title[i];
    wnd->title[i] = '\0';

    /* Grow-only tab width: reserve for the widest caption seen so far.  A live
     * FPS readout pads its number to a fixed field, so the very first update
     * (which already carries the "999 FPS" worst case) sets the width once and
     * every later update fits it -- the title bar never re-flows frame to frame. */
    H need = calculate_title_display_width(wnd->title) + 48;
    if (need < 100) need = 100;
    if (need > wnd->tab_width) wnd->tab_width = need;

    /* Repaint the tab only; the client image is untouched. */
    RECT tr;
    if (wget_tab_rect(wnd, &tr) == E_OK) wnd_inval_rect(&tr);
    return E_OK;
}

int wnd_gl_is_costly(const WND *w)
{
    /* TinyGL software raster is the expensive one.  The live EEGS layer
     * records a GIF stream and must not consume PS2_TILE_GL_PER_PASS:
     * counting it left the demoted window unpainted (GDEV never written)
     * while the focused layer animated.  POSIX quota is 0, so this
     * answer is unused there. */
    if (!w || !w->gl_surf) return 0;
    if (w->gl_layer && w == find_gl_layer()) return 0;
    return 1;
}

int wnd_gl_count(void)
{
    int n = 0;
    for (WND *w = g_wnd_head; w; w = w->next) {
        if (w->visible && w->gl_surf) n++;
    }
    return n;
}

int wnd_gl_focused_owes(void)
{
    WND *top = get_top_wnd();
    WND *lay;
    if (!top || !top->visible || !top->paint) return 0;
    lay = find_gl_layer();
    if (top->gl_layer && top == lay)
        return (!s_gl_frozen || !s_gl_layer_recorded) ? 1 : 0;
    if (!top->gl_surf) return 0;
    return top->img_valid ? 0 : 1;
}

void wnd_gl_set_pass_quota(int n) { s_gl_pass_quota = n; }

static int gl_quota_now(void)
{
    if (s_gl_pass_quota >= 0) return s_gl_pass_quota;
#if defined(BTRON_PS2_TARGET)
    return PS2_TILE_GL_PER_PASS;
#else
    return 0;
#endif
}

/* Next visible TinyGL window that still owes a software raster, wrapping
 * after `after`.  The EEGS layer is not costly: it paints through
 * wnd_owes_callback without taking this slot, so a demoted window can
 * fill its GDEV in the same composite.  Focused TinyGL outranks
 * round-robin.  An unrecorded layer still paints: owes_callback is true
 * while !s_gl_layer_recorded even under freeze. */
static WND *gl_next_owed(WND *after)
{
    WND *start;
    WND *w;
    WND *top;
    if (!g_wnd_head) return NULL;
    top = get_top_wnd();
    if (top && top->visible && top->paint && wnd_gl_is_costly(top) && !top->img_valid)
        return top;
    start = (after && after->next) ? after->next : g_wnd_head;
    w = start;
    do {
        if (w->visible && w->paint && wnd_gl_is_costly(w) && !w->img_valid)
            return w;
        w = w->next ? w->next : g_wnd_head;
    } while (w != start);
    return NULL;
}

void wnd_gl_pass_begin(void)
{
    s_gl_pick = NULL;
    if (gl_quota_now() <= 0) return;
    s_gl_pick = gl_next_owed(s_gl_rr_last);
    if (s_gl_pick) s_gl_rr_last = s_gl_pick;
}

/* The one place that answers "does this window's callback have to run in this composite".
 *
 * Two different facts, and conflating them is what made a paint budget do nothing.  For a pixmap
 * window staleness IS a fact about its image, so `img_valid` is the whole answer and the walk has
 * always used it.  For a layer window it is not: its picture is the stream its callback records, the
 * pixmap is never blitted, and the application dirties its own client rect every frame it animates
 * -- so `img_valid` says "stale" on a pass where the page already carries a better picture than the
 * one the callback would make.  A layer's schedule is therefore the PASS's, and the pass says it with
 * `wnd_gl_freeze()`: raised either because this pass already sent the layer's stream (one recording
 * per pass, however many bands reach it) or because the presentation plane decided the frame cannot
 * afford a second one (the app keeps its last picture and the desktop takes the frame).
 *
 * Nothing outside the PS2 core's paint pass ever raises the freeze, so on every other target this
 * reduces to the `!img_valid` it has always answered.
 *
 * PS2 (quota 1): one TinyGL software raster per composite.  The EEGS
 * layer is not in that quota (GIF record is cheap); a demoted window
 * therefore still fills its GDEV.  Focused TinyGL is picked whenever it
 * owes; remaining slots round-robin.  Five TinyGL paints in one pass
 * were ~140 ms (~7 FPS each).  A skipped window stays invalid. */
static BOOL wnd_owes_callback(const WND *w) {
    BOOL owes;
    if (!w->paint) return FALSE;
    {
        WND *lay = find_gl_layer();
        owes = (w->gl_layer && w == lay)
            ? (!s_gl_frozen || !s_gl_layer_recorded)
            : !w->img_valid;
    }
    if (!owes) return FALSE;
    if (gl_quota_now() <= 0) return TRUE;
    if (!wnd_gl_is_costly(w)) return TRUE;
    return w == s_gl_pick;
}

/* TRUE when a composite over `damage` cannot be a pure pixmap blit. */
BOOL wnd_damage_needs_paint(const RECT *damage) {
    if (!damage) return FALSE;
    for (WND *curr = g_wnd_head; curr; curr = curr->next) {
        if (!curr->visible || !curr->paint) continue;
        if (curr->bounds.right <= damage->left || curr->bounds.left >= damage->right ||
            curr->bounds.bottom <= damage->top || curr->bounds.top >= damage->bottom)
            continue;
        if (wnd_owes_callback(curr)) return TRUE;
        /* Quota deferred this GL: it still owes a later pass, so this
         * composite cannot be blit-only or the pick never runs.  A frozen
         * layer is the exception: its stream is the page's, not stale. */
        if (gl_quota_now() > 0 && wnd_gl_is_costly(curr) && !curr->img_valid) {
            if (curr->gl_layer && s_gl_frozen && s_gl_layer_recorded) continue;
            return TRUE;
        }
    }
    return FALSE;
}

/* Task #165's seam: see the comment on `s_gl_frozen`.  Set from a paint pass, cleared by the same
 * pass before it ends; nothing else in the tree raises it, which is what lets the two tests above
 * be free for every other target.  Reading it back is for the caller that has to decide whether a
 * band it is about to composite is the one that owns the stream. */
void wnd_gl_freeze(int frozen) { s_gl_frozen = frozen ? TRUE : FALSE; }
int  wnd_gl_is_frozen(void) { return s_gl_frozen ? 1 : 0; }

/* A visible layer window counts as invalid for both of these: its image is the GIF stream, not
 * the pixmap, and a band that overlaps it has to re-record the stream or it uploads a canvas
 * over a region nothing draws into. */
BOOL wnd_any_image_invalid(void) {
    WND *lay = find_gl_layer();
    for (WND *curr = g_wnd_head; curr; curr = curr->next)
        if (curr->visible && curr->paint && (!curr->img_valid || (curr->gl_layer && curr == lay))) return TRUE;
    return FALSE;
}

void wnd_get_invalid_image_bounds(RECT *out) {
    if (!out) return;
    WND *lay = find_gl_layer();
    RECT u = { 0, 0, 0, 0 };
    for (WND *curr = g_wnd_head; curr; curr = curr->next) {
        if (!curr->visible || !curr->paint) continue;
        if (curr->img_valid && (!curr->gl_layer || curr != lay)) continue;
        if (u.right <= u.left || u.bottom <= u.top) { u = curr->bounds; continue; }
        if (curr->bounds.left < u.left)   u.left = curr->bounds.left;
        if (curr->bounds.top < u.top)     u.top = curr->bounds.top;
        if (curr->bounds.right > u.right) u.right = curr->bounds.right;
        if (curr->bounds.bottom > u.bottom) u.bottom = curr->bounds.bottom;
    }
    *out = u;
}

/* The body fill's area, summed over the walk in progress.  draw_retro_window_frame() owns the
 * rectangle, so it owns the count -- a second copy of the geometry in the walk would be a number
 * that can disagree with the pixels.  The walk publishes it as `frame_body_px` because frame= is
 * a microsecond maximum and cannot say whether the stage was slow or simply enormous, and because
 * a GS layer window covers exactly this rect: that is the difference between a fill that is a
 * window's background and a fill that paints nothing the screen will ever show. */
static uint32_t s_frame_body_px;
/* The complement: px of a layer window's body this pass chose not to write, counted at the moment
 * of the decision rather than derived afterwards, so a row cannot report a saving the code did not
 * take.  Published as `frame_body_skip_px`; body+skip is the area the fill used to move. */
static uint32_t s_frame_body_skip_px;

static void fill_body_part(GDEV *dev, H l, H t, H r, H b) {
    if (r <= l || b <= t) return;
    const RECT part = { l, t, r, b };
    fill_rec(dev, &part, COLOR_LTGRAY);
}

static void draw_retro_window_frame(GDEV *dev, WND *wnd, BOOL layer_records) {
    if (!dev || !wnd) return;
    if (!(wnd->attr & (WND_ATTR_TITLE | WND_ATTR_BORDER))) return;

    if (g_wm_style == WM_STYLE_CHOKANJI) {
        pmc_draw_window_frame(dev, wnd);
        return;
    }

    H title_h = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;

    /* Main Window Body: fills from bounds.top + title_h to bounds.bottom */
    RECT body;
    body.left = wnd->bounds.left;
    body.top = wnd->bounds.top + title_h;
    body.right = wnd->bounds.right;
    body.bottom = wnd->bounds.bottom;

    /* A GS layer window's client rect belongs to the stream, not to the canvas: the pass that
     * records it (layer_records) is the pass whose environment packet scissors that rect, and the
     * present cuts that rect from the canvas upload so wallpaper and windows underneath never
     * DMA into the GS hole (WHY_NOCUT used to upload them, then the stream buried them a few
     * microseconds later -- flicker of those windows through focused Quake).  Skipping the
     * fill there is the same refusal increment 5c already made for the client blit -- the
     * unconditional `if (wnd->gl_layer) continue` further down this walk -- charged on the other
     * side of the ledger, where a measured full-desktop pass spends essentially all of frame= at
     * the rate `body=` now prices.  The rect the stream owns is the client rect, not the body, so
     * the fill keeps the ring between them: four strips that tile body-minus-rect with no gap and
     * no overlap, the border and the grip still canvas-drawn.
     *
     * layer_records is the walk's own predicate, passed in instead of recomputed here, because a
     * skip on a pass that sends no stream would put desktop art where a grey plate belongs. */
    RECT buried = { 0, 0, 0, 0 };
    if (layer_records) {
        const H border = (wnd->attr & WND_ATTR_BORDER) ? 4 : 0;
        buried.left = body.left + border;
        buried.top = body.top + border;
        buried.right = buried.left + wnd->dev->width;
        buried.bottom = buried.top + wnd->dev->height;
        if (buried.left < body.left) buried.left = body.left;
        if (buried.top < body.top) buried.top = body.top;
        if (buried.right > body.right) buried.right = body.right;
        if (buried.bottom > body.bottom) buried.bottom = body.bottom;
    }

    const uint32_t body_px = (uint32_t)(body.right - body.left) *
                             (uint32_t)(body.bottom - body.top);
    if (buried.right > buried.left && buried.bottom > buried.top) {
        const uint32_t buried_px = (uint32_t)(buried.right - buried.left) *
                                   (uint32_t)(buried.bottom - buried.top);
        s_frame_body_px += body_px - buried_px;
        s_frame_body_skip_px += buried_px;
        fill_body_part(dev, body.left, body.top, body.right, buried.top);
        fill_body_part(dev, body.left, buried.bottom, body.right, body.bottom);
        fill_body_part(dev, body.left, buried.top, buried.left, buried.bottom);
        fill_body_part(dev, buried.right, buried.top, body.right, buried.bottom);
    } else {
        s_frame_body_px += body_px;
        fill_rec(dev, &body, COLOR_LTGRAY);
    }
    drw_rec(dev, &body);

    RECT inner_b;
    inner_b.left = body.left + 2;
    inner_b.top = body.top + 2;
    inner_b.right = body.right - 2;
    inner_b.bottom = body.bottom - 2;
    drw_rec(dev, &inner_b);

    if (wnd->attr & WND_ATTR_TITLE) {
        RECT tab_r;
        wget_tab_rect(wnd, &tab_r);

        /* BeOS / CWM Minimalist Bold Palette: Iconic Gold when focused, Clean Silver when unfocused */
        COLOR tab_bg = wnd->focused ? COLOR_GOLD : COLOR_LTGRAY;
        COLOR title_col = wnd->focused ? COLOR_BLACK : COLOR_DKGRAY;

        fill_rec(dev, &tab_r, tab_bg);
        drw_rec(dev, &tab_r);

        /* Replicate canonical Window Body double-line border with intermediary space */
        RECT inner_tab;
        inner_tab.left = tab_r.left + 2;
        inner_tab.top = tab_r.top + 2;
        inner_tab.right = tab_r.right - 2;
        inner_tab.bottom = tab_r.bottom - 2;
        drw_rec(dev, &inner_tab);

        /* Minimalist BeOS/CWM double-bar vertical grip on left */
        H text_start_x = tab_r.left + 7;
        if ((wnd->attr & WND_ATTR_SLIDING_TAB) && (wnd->tab_width < (wnd->bounds.right - wnd->bounds.left - 16))) {
            drw_lin(dev, tab_r.left + 5, tab_r.top + 5, tab_r.left + 5, tab_r.top + 22);
            drw_lin(dev, tab_r.left + 8, tab_r.top + 5, tab_r.left + 8, tab_r.top + 22);
            text_start_x = tab_r.left + 13;
        }

        /* Bold & crisp title string - positioned 1px up at tab_r.top + 6 */
        drw_tc_string(dev, text_start_x, tab_r.top + 6, wnd->title, title_col, 0x00000000);

        if (wnd->attr & WND_ATTR_CLOSE) {
            RECT close_btn;
            close_btn.left = tab_r.right - 24;
            close_btn.top = tab_r.top + 6;
            close_btn.right = tab_r.right - 6;
            close_btn.bottom = tab_r.top + 22;

            fill_rec(dev, &close_btn, wnd->focused ? COLOR_LTGRAY : COLOR_GRAY);
            drw_rec(dev, &close_btn);
            /* Centered cross inside close button shifted 2px up (tab_r.top + 6) */
            drw_tc_string(dev, close_btn.left + 5, tab_r.top + 6, "x", COLOR_BLACK, 0x00000000);
        }
    }

    /* BTRON3 3.20 Standard Bottom-Right Corner Resize Handle (右下角枠リサイズ) */
    if (wnd->attr & WND_ATTR_RESIZE) {
        H rx = body.right - 14;
        H ry = body.bottom - 14;
        RECT grip_r = { rx, ry, body.right - 2, body.bottom - 2 };
        fill_rec(dev, &grip_r, COLOR_LTGRAY);

        /* 3 Diagonal hatch grip lines */
        drw_lin(dev, body.right - 12, body.bottom - 3, body.right - 3, body.bottom - 12);
        drw_lin(dev, body.right - 8,  body.bottom - 3, body.right - 3, body.bottom - 8);
        drw_lin(dev, body.right - 4,  body.bottom - 3, body.right - 3, body.bottom - 4);
    }
}

void redraw_all_windows(void) {
    if (!g_screen_dev) return;

    /* The scalar client-area composite this used to do cost ~100 ms per full
     * desktop (the documented mouse-freeze culprit, and the 'f' 99 ms the HUD
     * reports for a click or a drag start).  The clipped walk is the same
     * painter's order with the same transparency rule, but row-at-a-time. */
    RECT full = { 0, 0, g_screen_dev->width, g_screen_dev->height };
    redraw_all_windows_clip(&full, FALSE);
}

/* The client-area row rule lives in <btron/fused_row.h>, which holds both measured
 * shapes and the dial that picks one; the copy this function used to wrap is reached
 * there, by the scan-then-copy shape every non-PS2 port still runs. */

void redraw_all_windows_clip(const RECT *damage, BOOL blit_only) {
    if (!g_screen_dev || !damage) return;
    wnd_gl_pass_begin();

    /* The row copies below are raw memory writes, so the span must already be
     * on-device: mov_wnd() does not clamp, and a window dragged past a screen
     * edge would otherwise write outside the backbuffer. */
    RECT vis = *damage;
    if (vis.left < 0) vis.left = 0;
    if (vis.top < 0) vis.top = 0;
    if (vis.right > g_screen_dev->width) vis.right = g_screen_dev->width;
    if (vis.bottom > g_screen_dev->height) vis.bottom = g_screen_dev->height;
    if (vis.right <= vis.left || vis.bottom <= vis.top) return;
    damage = &vis;

    WND *stack[32];
    int count = 0;
    int walked = 0;
    for (WND *curr = g_wnd_head; curr; curr = curr->next) {
        walked++;
        if (count < 32) stack[count++] = curr;
    }

    /* Stage split for the on-device profile: the HUD only says that a
     * composite cost N ms, so the walk reports the decoration, the application
     * paint callbacks and the client composite separately. */
    uint32_t t_frame = 0, t_paint = 0, t_blit = 0, t_blit_px = 0, t_mark;
    int drawn = 0;
    s_frame_body_px = 0u;
    s_frame_body_skip_px = 0u;

    set_clip(g_screen_dev, damage);
    WND *gl_wnd = find_gl_layer();
    int gl_idx = -1;
    if (gl_wnd) {
        for (int k = 0; k < count; k++) {
            if (stack[k] == gl_wnd) { gl_idx = k; break; }
        }
    }

    for (int i = count - 1; i >= 0; i--) {
        WND *wnd = stack[i];
        if (!wnd || !wnd->visible) continue;

        /* If this window is in front of the active GL layer in z-order,
         * its opaque regions (compact title tab and main body) cover the GL layer.
         * Register them before the damage clip so partial-band streams restore
         * all overlapping front windows without uploading the transparent strip
         * to the right of a compact title tab. */
        if (gl_idx >= 0 && i < gl_idx && gl_wnd) {
            H cov_th = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
            if (cov_th > 0) {
                RECT tr, ti;
                if (wget_tab_rect(wnd, &tr) == E_OK) {
                    ti.left   = tr.left   > gl_wnd->bounds.left   ? tr.left   : gl_wnd->bounds.left;
                    ti.top    = tr.top    > gl_wnd->bounds.top    ? tr.top    : gl_wnd->bounds.top;
                    ti.right  = tr.right  < gl_wnd->bounds.right  ? tr.right  : gl_wnd->bounds.right;
                    ti.bottom = tr.bottom < gl_wnd->bounds.bottom ? tr.bottom : gl_wnd->bounds.bottom;
                    if (ti.right > ti.left && ti.bottom > ti.top) wnd_gl_cover_add(&ti);
                }
            }
            RECT isect;
            H body_top = wnd->bounds.top + cov_th;
            isect.left   = wnd->bounds.left   > gl_wnd->bounds.left   ? wnd->bounds.left   : gl_wnd->bounds.left;
            isect.top    = body_top           > gl_wnd->bounds.top    ? body_top           : gl_wnd->bounds.top;
            isect.right  = wnd->bounds.right  < gl_wnd->bounds.right  ? wnd->bounds.right  : gl_wnd->bounds.right;
            isect.bottom = wnd->bounds.bottom < gl_wnd->bounds.bottom ? wnd->bounds.bottom : gl_wnd->bounds.bottom;
            if (isect.right > isect.left && isect.bottom > isect.top) {
                wnd_gl_cover_add(&isect);
            }
        }

        if (wnd->bounds.right <= damage->left || wnd->bounds.left >= damage->right ||
            wnd->bounds.bottom <= damage->top || wnd->bounds.top >= damage->bottom)
            continue;

        drawn++;
        /* Staleness is a property of one window's image, not of the composite: the
         * walk's caller has already decided that some window in this band has to be
         * re-rendered, and repainting the valid ones with it cost 55 ms of every XMB
         * frame on the PS2 ([XMBT] cycle's comp= column, doc/txt/PS2.txt).  A layer window
         * is the exception the rule cannot cover, and `wnd_owes_callback()` is that exception
         * stated once -- its picture is the stream, so its schedule belongs to the pass, not
         * to a pixmap nobody blits from.  Reading it here rather than spelling the two terms
         * again is what keeps this walk and `wnd_damage_needs_paint()` from disagreeing about
         * a window this band is about to draw. */
        BOOL must_repaint = wnd_owes_callback(wnd);
        /* Quota skipped this GL: blit the last pixmap and leave it
         * invalid so the next pass's round-robin picks it. */
        const BOOL records = !blit_only && wnd->paint && must_repaint &&
                             wnd->dev && wnd->dev->pixels;

        t_mark = btron_render_perf_us();
        draw_retro_window_frame(g_screen_dev, wnd, records && wnd->gl_layer && wnd == gl_wnd);
        t_frame += btron_render_perf_us() - t_mark;
        if (!wnd->dev || !wnd->dev->pixels) continue;
        H title_h = (wnd->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
        H border = (wnd->attr & WND_ATTR_BORDER) ? 4 : 0;
        H dest_x = wnd->bounds.left + border;
        H dest_y = wnd->bounds.top + title_h + border;

        if (records) {
            t_mark = btron_render_perf_us();
            /* Bind this window's own GL context before handing it to the app, so the callback
             * cannot inherit the previous window's.  Keyed on `gl_surf`, not on `gl_layer`:
             * the software backends need a per-window context exactly as much as the GS does,
             * and a window with no surface has nothing to bind.  Cheap by construction -- the
             * virgl backend swaps a pointer and TinyGL's resize already returns when the array
             * and size are unchanged, so a window that paints alone pays nothing extra and only
             * a genuine switch between two GL windows re-opens a raster buffer. */
            /* Scoped GL ownership: exactly one window owns the current context for the
             * span of its paint callback.  A paint that is entered while another window's
             * scope is open (re-entry through an input pump or a delay) is refused and
             * counted instead of drawing into the wrong context or target; the window
             * stays invalid and repaints on the next pass. */
            if (s_gl_scope_owner) {
                s_gl_nest_refused++;      /* image stays invalid: repaints next pass */
            } else {
                s_gl_scope_owner = wnd;
                if (s_gl_bind && wnd->gl_surf) s_gl_bind(wnd);
                btron_cooperative_input_pump();   /* once, before draw: pointer latency */
                wnd->paint(wnd, wnd->dev);
                s_gl_scope_owner = NULL;
                t_paint += btron_render_perf_us() - t_mark;
                wnd->img_valid = TRUE;
                if (wnd->gl_layer && wnd == gl_wnd) {
                    RECT cr;
                    if (gl_client_rect(wnd, &cr)) {
                        s_gl_client = cr;
                        s_gl_pending = TRUE;
                        s_gl_layer_recorded = 1;
                    }
                }
            }
        }
        /* The GS draws these pixels.  Present cuts this client from the canvas
         * upload, so skipping the blit here does not leave wallpaper in VRAM. */
        if (wnd->gl_layer && wnd == gl_wnd) continue;

        H cx0 = damage->left - dest_x;
        H cy0 = damage->top - dest_y;
        H cx1 = damage->right - dest_x;
        H cy1 = damage->bottom - dest_y;
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 > wnd->dev->width) cx1 = wnd->dev->width;
        if (cy1 > wnd->dev->height) cy1 = wnd->dev->height;
        if (cx1 <= cx0 || cy1 <= cy0) continue;

        H span = cx1 - cx0;
        t_blit_px += (uint32_t)span * (uint32_t)(cy1 - cy0);
        t_mark = btron_render_perf_us();
        for (H cy = cy0; cy < cy1; cy++) {
            const COLOR *src = &wnd->dev->pixels[cy * wnd->dev->width + cx0];
            COLOR *dst = &g_screen_dev->pixels[(dest_y + cy) * g_screen_dev->width + dest_x + cx0];

            /* The same rule the two passes in this loop used to spell, in the shape this
             * port has measured: read the row once, decide sixteen pixels at a time, store
             * what is not the sentinel.  The walk paid for a SECOND read of bytes the
             * decision had already brought in: 39.84 ns/px for a row whose copy floor is
             * 15.96, of which the fused shape leaves 4.99 on the table as the price of the
             * decision and none of it for the read.  include/btron/fused_row.h carries
             * both shapes and the ladder that ranks them.
             *
             * It is the port's dial that picks, not a judgement made here: on every other
             * target the call is the code that used to be in this loop, scan-then-copy,
             * because the copy it goes through is aarch64's 64-byte NEON loop and a
             * per-pixel decision inside that loop would take the benefit away. */
            btron_row_copy_skip_zero(dst, src, (int)span);
        }
        t_blit += btron_render_perf_us() - t_mark;
    }
    set_clip(g_screen_dev, NULL);
    btron_render_stat_max(&g_render_stats.frame_us, t_frame);
    btron_render_stat_max(&g_render_stats.frame_body_px, s_frame_body_px);
    btron_render_stat_max(&g_render_stats.frame_body_skip_px, s_frame_body_skip_px);
    btron_render_stat_max(&g_render_stats.paint_us, t_paint);
    btron_render_stat_worst(&g_render_stats.blit_us, &g_render_stats.blit_worst_px,
                            t_blit, t_blit_px);
    g_render_stats.wins_walked = (uint32_t)walked;
    g_render_stats.wins_drawn = (uint32_t)drawn;
}

void redraw_top_window(void) {
    WND *wnd = get_top_wnd();
    if (!g_screen_dev || !wnd || !wnd->visible) return;

    /* Bounded to the top window's rect; NEON rows instead of the scalar loop
     * that cost ~100 ms per call.  Windows under it that overlap the rect are
     * re-composited first, which is the correct painter's order. */
    redraw_all_windows_clip(&wnd->bounds, FALSE);
}

void wnd_get_union_bounds(RECT *out) {
    if (!out) return;
    out->left = 32767; out->top = 32767; out->right = -32768; out->bottom = -32768;
    WND *curr = g_wnd_head;
    while (curr) {
        if (curr->visible) {
            if (curr->bounds.left   < out->left)   out->left   = curr->bounds.left;
            if (curr->bounds.top    < out->top)    out->top    = curr->bounds.top;
            if (curr->bounds.right  > out->right)  out->right  = curr->bounds.right;
            if (curr->bounds.bottom > out->bottom) out->bottom = curr->bounds.bottom;
        }
        curr = curr->next;
    }
    if (out->right < out->left || out->bottom < out->top) {
        out->left = out->top = out->right = out->bottom = 0;
    }
}

WND* find_wnd_at(H x, H y) {
    WND *curr = g_wnd_head;
    while (curr) {
        if (curr->visible) {
            H title_h = (curr->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
            /* 1. Main window body (below title tab row) */
            if (x >= curr->bounds.left && x <= curr->bounds.right &&
                y >= curr->bounds.top + title_h && y <= curr->bounds.bottom) {
                return curr;
            }
            /* 2. Compact title tab area (only inside tab rect).
             *    The PMC/Cho-Kanji close switch sits outside the titleband's
             *    drag region (pmc_hit_test_title excludes it by contract), so
             *    it is tested here as well; otherwise find_wnd_at returns NULL
             *    over the close box and the click never reaches cls_wnd. */
            if ((curr->attr & WND_ATTR_TITLE) &&
                (whit_test_tab(curr, x, y) || whit_test_close_btn(curr, x, y))) {
                return curr;
            }
        }
        curr = curr->next;
    }
    return NULL;
}

WND* get_top_wnd(void) {
    return g_wnd_head;
}

BOOL wnd_mgr_contains(const WND *wnd) {
    for (WND *curr = g_wnd_head; curr; curr = curr->next) {
        if (curr == wnd) return TRUE;
    }
    return FALSE;
}

WND* get_wnd_list(void) {
    return g_wnd_head;
}

void wnd_cascade_all(void) {
    H base_x = 40;
    H base_y = 42;
    H step = 28;
    H def_w = 580;
    H def_h = 380;

    int idx = 0;
    WND *curr = g_wnd_head;
    while (curr) {
        if (curr->visible) {
            RECT r = {
                (H)(base_x + (idx % 10) * step),
                (H)(base_y + (idx % 10) * step),
                (H)(base_x + (idx % 10) * step + def_w),
                (H)(base_y + (idx % 10) * step + def_h)
            };
            wrsz_wnd(curr, &r);
            inval_wnd(curr);
            idx++;
        }
        curr = curr->next;
    }
    if (g_screen_dev) {
        RECT full = { 0, 0, g_screen_dev->width, g_screen_dev->height };
        wnd_inval_damage_rect(&full);
    }
}

void wnd_tile_all(void) {
    int count = 0;
    WND *curr = g_wnd_head;
    while (curr) {
        if (curr->visible) count++;
        curr = curr->next;
    }
    if (count == 0) return;

    H scr_w = g_screen_dev ? g_screen_dev->width : 1280;
    H scr_h = g_screen_dev ? g_screen_dev->height : 800;
    H top_margin = 32;
    H bot_margin = 10;
    H left_margin = 10;
    H right_margin = 10;

    H avail_w = scr_w - left_margin - right_margin;
    H avail_h = scr_h - top_margin - bot_margin;

    int cols = 1, rows = 1;
    if (count == 2) { cols = 2; rows = 1; }
    else if (count <= 4) { cols = 2; rows = 2; }
    else if (count <= 6) { cols = 3; rows = 2; }
    else { cols = 3; rows = (count + 2) / 3; }

    H tile_w = avail_w / cols;
    H tile_h = avail_h / rows;

    int idx = 0;
    curr = g_wnd_head;
    while (curr) {
        if (curr->visible) {
            int c = idx % cols;
            int r_idx = idx / cols;
            H w = tile_w - 4;
            w = ((w - 8) & ~3) + 8;
            H h = tile_h - 4;
            RECT r = {
                (H)(left_margin + c * tile_w),
                (H)(top_margin + r_idx * tile_h),
                (H)(left_margin + c * tile_w + w),
                (H)(top_margin + r_idx * tile_h + h)
            };
            wrsz_wnd(curr, &r);
            inval_wnd(curr);
            idx++;
        }
        curr = curr->next;
    }
    if (g_screen_dev) {
        RECT full = { 0, 0, g_screen_dev->width, g_screen_dev->height };
        wnd_inval_damage_rect(&full);
    }
}

void wnd_hide_all(void) {
    WND *curr = g_wnd_head;
    while (curr) {
        curr->visible = FALSE;
        curr = curr->next;
    }
}

void wnd_cycle_focus(void) {
    if (!g_wnd_head || !g_wnd_head->next) return;

    /* Find last window in list and bring it to front */
    WND *curr = g_wnd_head;
    while (curr->next) {
        curr = curr->next;
    }
    if (curr && curr != g_wnd_head) {
        curr->visible = TRUE;
        top_wnd(curr);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * BTRON 3.20 Window Manager Interaction & Event Dispatcher
 * ═══════════════════════════════════════════════════════════════════ */

static BOOL s_wnd_dragging = FALSE;
static WND *s_wnd_drag_target = NULL;
static H    s_wnd_drag_off_x = 0;
static H    s_wnd_drag_off_y = 0;

static BOOL s_wnd_sliding_tab = FALSE;
static WND *s_wnd_slide_target = NULL;
static H    s_wnd_slide_start_x = 0;
static H    s_wnd_slide_orig_off = 0;

static WND *s_wnd_resize_target = NULL;
static H    s_wnd_resize_orig_w = 0;
static H    s_wnd_resize_orig_h = 0;
static H    s_wnd_resize_start_x = 0;
static H    s_wnd_resize_start_y = 0;
static H    s_wnd_resize_pending_w = 0;
static H    s_wnd_resize_pending_h = 0;

static void wnd_mgr_window_destroyed(WND *wnd) {
    if (s_wnd_drag_target == wnd) {
        s_wnd_dragging = FALSE;
        s_wnd_drag_target = NULL;
    }
    if (s_wnd_slide_target == wnd) {
        s_wnd_sliding_tab = FALSE;
        s_wnd_slide_target = NULL;
    }
    if (s_wnd_resize_target == wnd) {
        s_wnd_resizing = FALSE;
        s_wnd_resize_target = NULL;
    }
}

BOOL wnd_mgr_is_interacting(void) {
    return (s_wnd_dragging || s_wnd_sliding_tab || s_wnd_resizing);
}

WND* wnd_mgr_get_drag_target(void) {
    return s_wnd_drag_target;
}

WND* wnd_mgr_get_interaction(int *kind) {
    if (kind) {
        if (s_wnd_resizing && s_wnd_resize_target) *kind = WND_INTERACT_RESIZE;
        else if (s_wnd_sliding_tab && s_wnd_slide_target) *kind = WND_INTERACT_SLIDE;
        else if (s_wnd_dragging && s_wnd_drag_target) *kind = WND_INTERACT_DRAG;
        else *kind = WND_INTERACT_NONE;
    }
    if (s_wnd_resizing) return s_wnd_resize_target;
    if (s_wnd_sliding_tab) return s_wnd_slide_target;
    if (s_wnd_dragging) return s_wnd_drag_target;
    return NULL;
}

BOOL wnd_mgr_flush_resize(void) {
    WND *target = s_wnd_resize_target;
    BOOL changed = FALSE;
    H need_w, need_h, title_h, border, dev_w, dev_h;
    int slack;

    if (!target) return FALSE;
    need_w = s_wnd_resize_pending_w;
    need_h = s_wnd_resize_pending_h;
    title_h = (target->attr & WND_ATTR_TITLE) ? WND_TITLE_HEIGHT : 0;
    border = (target->attr & WND_ATTR_BORDER) ? 4 : 0;
    dev_w = (need_w - border * 2) & ~3;
    dev_h = need_h - title_h - border * 2;
    if (dev_w < 10) dev_w = 10;
    if (dev_h < 10) dev_h = 10;
    slack = s_wnd_resizing ? 1 : 0;

    /* Chrome may already match (MOVE applied it); GDEV still owes a
     * realloc or a view, and button-up drops slack to the exact size. */
    if (target->bounds.right - target->bounds.left != need_w ||
        target->bounds.bottom - target->bounds.top != need_h ||
        !target->dev ||
        target->dev->width != dev_w || target->dev->height != dev_h ||
        (!slack && (gdev_cap_w(target->dev) != dev_w ||
                    gdev_cap_h(target->dev) != dev_h))) {
        rsz_wnd(target, need_w, need_h);
        changed = TRUE;
    }
    if (!s_wnd_resizing)
        s_wnd_resize_target = NULL;
    return changed;
}

BOOL wnd_mgr_handle_event(const EVT *ev) {
    if (!ev) return FALSE;

    if (ev->type == EV_BUT_DOWN) {
        WND *clicked = find_wnd_at(ev->pos.x, ev->pos.y);
        if (!clicked) {
            return FALSE;
        }

        if (get_top_wnd() != clicked) {
            top_wnd(clicked);
        }

        /* 1. Resize: 28x28 bottom-right corner, plus 10 px right and bottom
         * edges below the title so a half-screen terminal can grow up. */
        if (clicked->attr & WND_ATTR_RESIZE) {
            H grip_top = clicked->bounds.top;
            int in_br, in_right, in_bottom;
            if (clicked->attr & WND_ATTR_TITLE)
                grip_top = (H)(clicked->bounds.top + WND_TITLE_HEIGHT);
            in_br = (ev->pos.x >= clicked->bounds.right - 28 &&
                     ev->pos.x <= clicked->bounds.right &&
                     ev->pos.y >= clicked->bounds.bottom - 28 &&
                     ev->pos.y <= clicked->bounds.bottom);
            in_right = (ev->pos.x >= clicked->bounds.right - 10 &&
                        ev->pos.x <= clicked->bounds.right &&
                        ev->pos.y >= grip_top &&
                        ev->pos.y <= clicked->bounds.bottom);
            in_bottom = (ev->pos.y >= clicked->bounds.bottom - 10 &&
                         ev->pos.y <= clicked->bounds.bottom &&
                         ev->pos.x >= clicked->bounds.left &&
                         ev->pos.x <= clicked->bounds.right);
            if (in_br || in_right || in_bottom) {
            s_wnd_resizing = TRUE;
            s_wnd_resize_target = clicked;
            s_wnd_resize_orig_w = clicked->bounds.right - clicked->bounds.left;
            s_wnd_resize_orig_h = clicked->bounds.bottom - clicked->bounds.top;
            s_wnd_resize_start_x = ev->pos.x;
            s_wnd_resize_start_y = ev->pos.y;
            s_wnd_resize_pending_w = s_wnd_resize_orig_w;
            s_wnd_resize_pending_h = s_wnd_resize_orig_h;
            return TRUE;
            }
        }

        /* 2. Titlebar & Compact Sliding Tab Check */
        if (clicked->attr & WND_ATTR_TITLE) {
            RECT tab_r;
            wget_tab_rect(clicked, &tab_r);
            if (ev->pos.y >= clicked->bounds.top && ev->pos.y < tab_r.bottom) {
                /* Close button check */
                if (whit_test_close_btn(clicked, ev->pos.x, ev->pos.y)) {
                    cls_wnd(clicked);
                    return TRUE;
                }

                /* Compact Tab Check */
                if (whit_test_tab(clicked, ev->pos.x, ev->pos.y)) {
                    /* Left grip on tab triggers sliding tab */
                    if ((ev->pos.x >= tab_r.left && ev->pos.x < tab_r.left + 14) && (clicked->attr & WND_ATTR_SLIDING_TAB)) {
                        s_wnd_sliding_tab = TRUE;
                        s_wnd_slide_target = clicked;
                        s_wnd_slide_start_x = ev->pos.x;
                        s_wnd_slide_orig_off = clicked->tab_offset_x;
                    } else {
                        s_wnd_dragging = TRUE;
                        s_wnd_drag_target = clicked;
                        s_wnd_drag_off_x = ev->pos.x - clicked->bounds.left;
                        s_wnd_drag_off_y = ev->pos.y - clicked->bounds.top;
                    }
                } else {
                    /* Clicked outside tab on top rail -> drag whole window */
                    s_wnd_dragging = TRUE;
                    s_wnd_drag_target = clicked;
                    s_wnd_drag_off_x = ev->pos.x - clicked->bounds.left;
                    s_wnd_drag_off_y = ev->pos.y - clicked->bounds.top;
                }
                return TRUE;
            }
        }

        /* 3. Dispatch to window client area */
        if (clicked->event_handler) {
            clicked->event_handler(clicked, ev);
        }
        return TRUE;
    }

    if (ev->type == EV_MOUSE_MOVE) {
        if (s_wnd_sliding_tab && s_wnd_slide_target) {
            H new_off = s_wnd_slide_orig_off + (ev->pos.x - s_wnd_slide_start_x);
            wset_tab_offset(s_wnd_slide_target, new_off);
            return TRUE;
        }
        if (s_wnd_resizing && s_wnd_resize_target) {
            s_wnd_resize_pending_w = s_wnd_resize_orig_w + (ev->pos.x - s_wnd_resize_start_x);
            s_wnd_resize_pending_h = s_wnd_resize_orig_h + (ev->pos.y - s_wnd_resize_start_y);
            if (s_wnd_resize_pending_w < 160) s_wnd_resize_pending_w = 160;
            if (s_wnd_resize_pending_h < 64) s_wnd_resize_pending_h = 64;
            /* Chrome follows the pointer on every report; GDEV realloc waits
             * for the compositor pass (OHCI bursts after a long GL paint
             * must not cls_dev/opn_dev per MOVE). */
            wnd_resize_chrome(s_wnd_resize_target,
                              s_wnd_resize_pending_w, s_wnd_resize_pending_h);
            return TRUE;
        }
        if (s_wnd_dragging && s_wnd_drag_target) {
            mov_wnd(s_wnd_drag_target, ev->pos.x - s_wnd_drag_off_x, ev->pos.y - s_wnd_drag_off_y);
            return TRUE;
        }

        WND *top = get_top_wnd();
        if (top && top->focused && top->event_handler) {
            top->event_handler(top, ev);
        }
        return FALSE;
    }

    if (ev->type == EV_BUT_UP) {
        BOOL was_interacting = (s_wnd_dragging || s_wnd_sliding_tab || s_wnd_resizing);
        if (s_wnd_resizing) {
            s_wnd_resizing = FALSE;
            /* One button-up try at the exact size; drag latch stays off. */
            if (s_gdev_oom_wnd == s_wnd_resize_target)
                s_gdev_oom_wnd = NULL;
            wnd_mgr_flush_resize();
        }
        s_wnd_dragging = FALSE;
        s_wnd_drag_target = NULL;
        s_wnd_sliding_tab = FALSE;
        s_wnd_slide_target = NULL;
        s_wnd_resizing = FALSE;
        s_wnd_resize_target = NULL;

        if (was_interacting) return TRUE;
        WND *top = get_top_wnd();
        if (top && top->focused && top->event_handler) {
            top->event_handler(top, ev);
        }
        return FALSE;
    }

    if (ev->type == EV_KEY_DOWN || ev->type == EV_KEY_UP) {
        WND *top = get_top_wnd();
        if (top && top->event_handler) {
            top->focused = TRUE;
            top->event_handler(top, ev);
            return TRUE;
        }
        return FALSE;
    }

    return FALSE;
}
