/*
 * ps2_tile.c — Open the eight-app PS2 desktop as a compact-tab cascade.
 *
 * Same rule as a retail PS2 game: the disc holds PAK/ROMs (CD ~700 MiB,
 * DVD ~4 GiB); RDRAM holds the live working set.  Rects come from
 * include/btron/ps2_tile.h (tools/ps2_tile_budget.py): every window
 * shares y=PANEL so the title tabs form a horizontal click-strip;
 * XMB is flush-right and EEGS-sized (client >= 320x220) so the GS
 * layer is live from the first paint.  After the set is open, XMB
 * is raised so EEGS is the visible window and the other tabs peek
 * from the left (minimal mouse travel).
 */
#include <stddef.h>
#include <btron/ps2_tile.h>
#include <btron/apps.h>
#include <btron/wnd.h>
#include "ps2_console.h"
#include "ps2_usb.h"

static const UW k_tile_attr =
    WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE;

static int tile_got(int id, const WND *wnd)
{
    const ps2_tile_rect_t *r = ps2_tile_rect(id);
    ps2_usb_poll();
    if (wnd)
        return 1;
    if (r)
        ps2_kprintf("[TILE] id=%d %dx%d@%d,%d failed\n",
                    id, r->w, r->h, r->x, r->y);
    else
        ps2_kprintf("[TILE] id=%d failed\n", id);
    return 0;
}

int ps2_tile_open_standard(void)
{
    int n = 0;
    const ps2_tile_rect_t *r;
    WND *xmb = 0;

    r = ps2_tile_rect(PS2_TILE_TERM);
    if (r)
        n += tile_got(PS2_TILE_TERM,
                      open_gterm_window_rect(r->x, r->y, r->w, r->h, k_tile_attr));

    r = ps2_tile_rect(PS2_TILE_CABINET);
    if (r)
        n += tile_got(PS2_TILE_CABINET,
                      open_vobj_manager_window_rect(r->x, r->y, r->w, r->h));

    r = ps2_tile_rect(PS2_TILE_EDITOR);
    if (r)
        n += tile_got(PS2_TILE_EDITOR,
                      open_t_editor_window_rect(NULL, r->x, r->y, r->w, r->h,
                                                k_tile_attr));

    r = ps2_tile_rect(PS2_TILE_XMB);
    if (r) {
        /* First GL window: claims the GS layer.  Size is EEGS-water
         * (client >= 320x220) so make_current does not demote to TinyGL. */
        xmb = open_xmb_window_rect(r->x, r->y, r->w, r->h);
        n += tile_got(PS2_TILE_XMB, xmb);
    }

    r = ps2_tile_rect(PS2_TILE_MARS);
    if (r)
        n += tile_got(PS2_TILE_MARS,
                      open_mars_window_rect(r->x, r->y, r->w, r->h));

    r = ps2_tile_rect(PS2_TILE_GEARS);
    if (r)
        n += tile_got(PS2_TILE_GEARS,
                      open_glgears_window_rect(r->x, r->y, r->w, r->h));

    r = ps2_tile_rect(PS2_TILE_CU);
    if (r)
        n += tile_got(PS2_TILE_CU,
                      open_lilcu64_demo_window_rect(r->x, r->y, r->w, r->h));

    r = ps2_tile_rect(PS2_TILE_QUAKE);
    if (r)
        n += tile_got(PS2_TILE_QUAKE,
                      open_quake_window(r->x, r->y, r->w, r->h));

    /* Later opens sit on top of the z-stack.  Raise XMB so the EEGS
     * layer is the visible window and the other compact tabs peek from
     * the left — a short horizontal mouse move switches any of them. */
    if (xmb)
        top_wnd(xmb);

    ps2_kprintf("[TILE] standard set %d/%d (PAK/ROMs stay on disc)\n",
                n, PS2_TILE_N);
    return n;
}
