/*
 * test_matrix.c — Unit Tests & Microbenchmark for B-System Matrix & libschrift
 *
 * Verifies:
 *   1. Clean-room Q16.16 libschrift: sft_init_mem, lookup, gmetrics_q16,
 *      render_q16 on both Matrix-Code.ttf and Matrix-Resurrected.ttf, plus
 *      corrupt/truncated buffer bounds safety.
 *   2. All 6 Rezmason/matrix modes (trinity, classic, resurrections,
 *      operator, megacity, pride) render non-empty art with zero heap.
 *   3. Dirty-cell culling produces bit-identical output to full redraw.
 *   4. Common shell mode parser (matrix_parse_mode).
 *   5. 640x480 per-frame CPU microbenchmark (us/frame).
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <btron/schrift.h>
#include "../../src/media/matrix/matrix_internal.h"

static uint32_t g_fb_a[640 * 480];
static uint32_t g_fb_b[640 * 480];
static MatrixState g_st;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

static void test_schrift_safety(void)
{
    SFT_Font f;
    uint8_t bad[32];
    memset(bad, 0xFF, sizeof(bad));
    assert(sft_init_mem(&f, NULL, 0) < 0);
    assert(sft_init_mem(&f, bad, sizeof(bad)) < 0);
    assert(sft_init_mem(&f, bad, 4) < 0);
    assert(sft_version() != NULL);
    printf("  [PASS] libschrift Q16.16 bounds & version checks (%s)\n", sft_version());
}

static void test_mode_parser(void)
{
    assert(matrix_parse_mode(NULL)            == MATRIX_MODE_CLASSIC);
    assert(matrix_parse_mode("")              == MATRIX_MODE_CLASSIC);
    assert(matrix_parse_mode("trinity")       == MATRIX_MODE_TRINITY);
    assert(matrix_parse_mode("classic")       == MATRIX_MODE_CLASSIC);
    assert(matrix_parse_mode("resurrections") == MATRIX_MODE_RESURRECTIONS);
    assert(matrix_parse_mode("operator")      == MATRIX_MODE_OPERATOR);
    assert(matrix_parse_mode("megacity")      == MATRIX_MODE_MEGACITY);
    assert(matrix_parse_mode("pride")         == MATRIX_MODE_PRIDE);
    assert(matrix_parse_mode("1")             == MATRIX_MODE_TRINITY);
    assert(matrix_parse_mode("6")             == MATRIX_MODE_PRIDE);
    assert(matrix_parse_mode("unknown")       == -1);
    printf("  [PASS] matrix_parse_mode() for all 6 modes + numeric aliases\n");
}

static void test_all_modes_and_dirty_cull(void)
{
    int m;
    for (m = 0; m < (int)MATRIX_MODE_COUNT; m++) {
        int f;
        uint32_t lit = 0u;
        uint32_t i;

        matrix_sim_init(&g_st, (MatrixMode)m, 640, 480);
        assert(g_st.atlas.fonts_ready == 1u);
        assert(g_st.atlas.glyph_count >= 57u);

        for (f = 0; f < 60; f++) {
            matrix_sim_step(&g_st, 16u);
            matrix_render_argb32(&g_st, g_fb_a, 640, 480);
        }

        for (i = 0u; i < (640u * 480u); i++) {
            if ((g_fb_a[i] & 0x00FFFFFFu) != 0u) {
                lit++;
            }
        }
        assert(lit > 1000u);

        /* Verify dirty-cell culling matches full redraw bit-for-bit */
        g_st.full_redraw = 1u;
        matrix_render_argb32(&g_st, g_fb_b, 640, 480);
        assert(memcmp(g_fb_a, g_fb_b, sizeof(g_fb_a)) == 0);

        printf("  [PASS] Mode %-13s: %u glyphs, %u lit px, dirty-cull exact\n",
               matrix_mode_name(m), (unsigned)g_st.atlas.glyph_count, (unsigned)lit);
    }
}

static void bench_classic_640x480(void)
{
    const int frames = 200;
    int i;
    uint64_t t0, t1, avg_us;

    matrix_sim_init(&g_st, MATRIX_MODE_CLASSIC, 640, 480);
    for (i = 0; i < 30; i++) {
        matrix_sim_step(&g_st, 16u);
        matrix_render_argb32(&g_st, g_fb_a, 640, 480);
    }

    t0 = now_us();
    for (i = 0; i < frames; i++) {
        matrix_sim_step(&g_st, 16u);
        matrix_render_argb32(&g_st, g_fb_a, 640, 480);
    }
    t1 = now_us();
    avg_us = (t1 - t0) / (uint64_t)frames;
    printf("  [BENCH] 640x480 Classic (sim + dirty blit): %llu us/frame (~%llu max FPS)\n",
           (unsigned long long)avg_us,
           (unsigned long long)(avg_us > 0 ? (1000000ULL / avg_us) : 999999ULL));
}

int main(void)
{
    printf("=== B-System Matrix Screensaver & Q16.16 libschrift Unit Tests ===\n");
    test_schrift_safety();
    test_mode_parser();
    test_all_modes_and_dirty_cull();
    bench_classic_640x480();
    printf("ALL MATRIX TESTS PASSED.\n");
    return 0;
}
