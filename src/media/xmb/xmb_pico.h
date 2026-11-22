/*
 * xmb_pico.h — PicoCalc XMB surface.
 *
 * The selected category stays at the body center. Neighbours are smaller
 * and higher. Items are a column on that same x. Does not link xmb.c,
 * xmb_wave.c, or the 64px atlas.
 */
#ifndef XMB_PICO_H
#define XMB_PICO_H

enum {
    XMB_GO_NONE = 0,
    XMB_GO_TERMINAL,
    XMB_GO_EDITOR,
    XMB_GO_BROWSER,
    XMB_GO_CABINET,
    XMB_GO_GEARS,
    XMB_GO_CHAT,
    XMB_GO_PREFS
};

void xmb_pico_tick(void);
void xmb_pico_move(int dcat, int ditem);
int  xmb_pico_go(void);
/* Selected cell origins. cat_x and item_x are the left edge of the 32px
 * cell. Both stay on the body center when the selection moves. */
void xmb_pico_anchors(int left, int top, int right, int bottom,
                      int *cat_x, int *item_x, int *cat, int *item);
const char *xmb_pico_label(void);
void xmb_pico_paint(int left, int top, int right, int bottom);

/* Host capture writes the same RGB565 painter into a GDEV. Pico images
 * do not define SEGUI_HOST_XMB, so this door is not in the elf. */
#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
void xmb_pico_host_bind(void *pixels, int w, int h);
#endif

#endif /* XMB_PICO_H */
