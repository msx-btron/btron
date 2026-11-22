/*
 * Host check for the Pico XMB anchors. The selected category stays at the
 * body center, and the selected item uses that same x.
 */
#include "xmb_pico.h"

#include <stdint.h>
#include <stdio.h>

uint16_t *dp_pico_band_row(int y)
{
    (void)y;
    return 0;
}

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (cond) {
        printf("  ok  %s\n", msg);
        return;
    }
    printf("  FAIL %s\n", msg);
    g_fail++;
}

int main(void)
{
    int cat_x, item_x, cat, item;
    int cat_x2, item_x2, cat2, item2;
    const char *label;

    xmb_pico_anchors(0, 40, 320, 300, &cat_x, &item_x, &cat, &item);
    expect(cat == 0 && item == 0, "boot selection is the first cell");
    expect(cat_x == item_x, "item column starts on the category x");
    expect(cat_x == (0 + 320) / 2 - 16, "selected category is the body center");

    xmb_pico_move(+1, 0);
    xmb_pico_anchors(0, 40, 320, 300, &cat_x2, &item_x2, &cat2, &item2);
    expect(cat2 == 1 && item2 == 0, "right moves category and resets the item");
    expect(cat_x2 == cat_x && item_x2 == cat_x2, "category anchor stays centered");

    xmb_pico_move(0, +1);
    xmb_pico_anchors(0, 40, 320, 300, &cat_x2, &item_x2, &cat2, &item2);
    expect(cat2 == 1 && item2 == 1, "down moves the item");
    expect(item_x2 == cat_x2, "item anchor stays on the category x");
    label = xmb_pico_label();
    expect(label && label[0], "label is the selected item");

    if (g_fail) {
        printf("xmb pico FAIL %d\n", g_fail);
        return 1;
    }
    printf("xmb pico ok\n");
    return 0;
}
