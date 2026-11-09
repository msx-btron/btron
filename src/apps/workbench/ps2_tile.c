/*
 * ps2_tile.c — Open the eight-app PS2 desktop in one grid.
 *
 * Same rule as a retail PS2 game: the disc holds PAK/ROMs (CD ~700 MiB,
 * DVD ~4 GiB); RDRAM holds the live working set.  Cell sizes come from
 * include/btron/ps2_tile.h (tools/ps2_tile_budget.py).
 */
#include <stddef.h>
#include <btron/ps2_tile.h>
#include <btron/apps.h>
#include <btron/wnd.h>
#include "ps2_console.h"

static const UW k_tile_attr =
    WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE;

static int tile_got(int id, const WND *wnd)
{
    const ps2_tile_rect_t *r = ps2_tile_rect(id);
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
    if (r)
        n += tile_got(PS2_TILE_XMB,
                      open_xmb_window_rect(r->x, r->y, r->w, r->h));

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

    ps2_kprintf("[TILE] standard set %d/%d (PAK/ROMs stay on disc)\n",
                n, PS2_TILE_N);
    return n;
}
