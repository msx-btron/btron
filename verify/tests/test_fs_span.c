/*
 * test_fs_span.c — btron_span_inside, the PAK and BSP length check.
 *
 * A lump at ofs = INT_MAX - 8 and len = 64 used to pass `ofs + len > size`
 * because the signed add wraps. The helper rejects it. A span that fits
 * still passes, so a real pak0 lump is unchanged.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <limits.h>
#include <stdio.h>

#include "btron/span.h"

static int s_checks, s_fail;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_fail++;
        printf("  [FAIL] %s\n", what);
    } else {
        printf("  [OK]   %s\n", what);
    }
}

int main(void)
{
    const int hostile_ofs = INT_MAX - 8;
    const int hostile_len = 64;

    printf("==========================================================\n");
    printf(" btron_span_inside\n");
    printf("==========================================================\n");

    check("hostile lump INT_MAX-8 + 64 is outside a 4096-byte BSP",
          !btron_span_inside(hostile_ofs, hostile_len, 4096));
    check("the same pair is outside a claimed PAK whose size is INT_MAX",
          !btron_span_inside(hostile_ofs, hostile_len, INT_MAX));
    check("a directory entry with that pair is outside a 4096-byte archive",
          !btron_span_inside(hostile_ofs, hostile_len, 4096));

    check("real entry at 12, length 64, archive 4096",
          btron_span_inside(12, 64, 4096));
    check("span that ends on the last byte",
          btron_span_inside(10, 5, 15));
    check("span one byte past the end",
          !btron_span_inside(10, 6, 15));
    check("empty span at the end is inside",
          btron_span_inside(15, 0, 15));
    check("negative offset",
          !btron_span_inside(-1, 4, 15));
    check("negative length",
          !btron_span_inside(0, -1, 15));
    check("negative size",
          !btron_span_inside(0, 1, -1));

    printf("----------------------------------------------------------\n");
    printf(" %d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
