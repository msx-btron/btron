/*
 * Host check for the Pico XMB anchors. The selected category stays on the
 * PS3 cross, the category row is horizontal, and the sheet is the cosine
 * ribbon rather than a floor pinned to the top of the body.
 */
#include "xmb_pico.h"

#include <stdint.h>
#include <stdio.h>

int xmb_pico_test_cat_cx(int left, int top, int right, int bottom, int index);
int xmb_pico_test_cat_cy(int left, int top, int right, int bottom, int index);
void xmb_pico_test_wave(int i, int j, int left, int top, int right, int bottom,
                        int *x, int *y);

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
    {
        int w = 320;
        int icon = (128 * w / 1920) * 2;
        int pitch = (192 * w / 1920) * 2;
        int cross = 336 * w / 1920 + pitch;
        int x, y;
        expect(cat_x == cross - icon / 2, "selected category is the PS3 cross");
        expect(xmb_pico_test_cat_cy(0, 40, 320, 300, 0)
               == xmb_pico_test_cat_cy(0, 40, 320, 300, 3),
               "category row is horizontal");
        expect(xmb_pico_test_cat_cx(0, 40, 320, 300, 1)
               - xmb_pico_test_cat_cx(0, 40, 320, 300, 0)
               == pitch,
               "categories use twice the PS3 horizontal pitch");
        xmb_pico_test_wave(0, 0, 0, 40, 320, 300, &x, &y);
        expect(x == 0 && y == 224, "sheet row 0 is the cosine ribbon");
        xmb_pico_test_wave(4, 3, 0, 40, 320, 300, &x, &y);
        expect(x == 160 && y == 199, "sheet center sits in the lower band");
        xmb_pico_test_wave(8, 6, 0, 40, 320, 300, &x, &y);
        expect(x == 320 && y == 223, "sheet spans the body width");
        expect(y > 40 + (300 - 40) / 3, "sheet is not pinned to the top");
    }

    xmb_pico_move(+1, 0);
    xmb_pico_anchors(0, 40, 320, 300, &cat_x2, &item_x2, &cat2, &item2);
    expect(cat2 == 1 && item2 == 0, "right moves category and resets the item");
    expect(cat_x2 == cat_x && item_x2 == cat_x2, "category anchor stays on the cross");

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
