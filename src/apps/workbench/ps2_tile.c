/*
 * ps2_tile.c — Open the PS2 boot desktop: Quake at 80% of the screen,
 * terminal command line under it.
 *
 * Same rule as a retail PS2 game: the disc holds PAK/ROMs; RDRAM holds
 * the live working set.  Rects come from include/btron/ps2_tile.h.
 * Quake is the first GL window and is EEGS-sized (client 632x444), so
 * the GS layer is live from the first paint.
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
    WND *quake = 0;

    r = ps2_tile_rect(PS2_TILE_TERM);
    if (r)
        n += tile_got(PS2_TILE_TERM,
                      open_gterm_window_rect(r->x, r->y, r->w, r->h, k_tile_attr));

    r = ps2_tile_rect(PS2_TILE_QUAKE);
    if (r) {
        /* First GL window: claims the GS layer.  640x480 window → client
         * 632x444, above the EEGS water of 320x220. */
        quake = open_quake_window(r->x, r->y, r->w, r->h);
        n += tile_got(PS2_TILE_QUAKE, quake);
    }

    if (quake)
        top_wnd(quake);

    ps2_kprintf("[TILE] standard set %d/%d (PAK/ROMs stay on disc)\n",
                n, PS2_TILE_N);
    return n;
}
