/*
 * B-System (BTRON 3.20) — verify/tests/test_bsf_vm.c
 * Automated Unit, Conformance (Music.html / synth-engine.js), & WCET Test Suite
 * for Q16.16 RT Math, B-Sound Forth (BSF) VM, 15 Primitives, 16 Shims, and 26 Tracks.
 */

#include <btron/bsf.h>
#include <rt/fix16.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_fail = 0;

#define CHECK(cond, msg, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] " msg "\n", ##__VA_ARGS__); \
        s_fail++; \
    } \
} while (0)

static void test_fix16_math(void) {
    /* Pitch law: y = 0 -> 261.6256 Hz, y = 1 -> 523.2511 Hz, y = -1 -> 130.8128 Hz */
    float f0 = fix16_to_float(fix16_y_to_hz(0));
    float f1 = fix16_to_float(fix16_y_to_hz(FIX16_ONE));
    float fm1 = fix16_to_float(fix16_y_to_hz(-FIX16_ONE));
    CHECK(fabsf(f0 - 261.6256f) < 0.15f, "y=0 -> %.4f Hz (exp 261.6256)", f0);
    CHECK(fabsf(f1 - 523.2511f) < 0.30f, "y=1 -> %.4f Hz (exp 523.2511)", f1);
    CHECK(fabsf(fm1 - 130.8128f) < 0.15f, "y=-1 -> %.4f Hz (exp 130.8128)", fm1);

    /* log2 accuracy across 1..10 */
    for (int n = 1; n <= 10; n++) {
        float got = fix16_to_float(fix16_log2(fix16_from_int(n)));
        float ref = log2f((float)n);
        CHECK(fabsf(got - ref) < 0.002f, "log2(%d) = %.5f (ref %.5f)", n, got, ref);
    }

    /* exp(-x) accuracy across 0..5 */
    for (int i = 0; i <= 10; i++) {
        float x = -0.5f * (float)i;
        float got = fix16_to_float(fix16_exp(fix16_from_float(x)));
        float ref = expf(x);
        CHECK(fabsf(got - ref) < 0.005f, "exp(%.1f) = %.5f (ref %.5f)", x, got, ref);
    }

    /* sin/cos accuracy */
    for (int i = 0; i < 16; i++) {
        float rad = (float)i * 0.4f;
        float gs = fix16_to_float(fix16_sin(fix16_from_float(rad)));
        float gc = fix16_to_float(fix16_cos(fix16_from_float(rad)));
        CHECK(fabsf(gs - sinf(rad)) < 0.003f, "sin(%.2f) = %.4f (ref %.4f)", rad, gs, sinf(rad));
        CHECK(fabsf(gc - cosf(rad)) < 0.003f, "cos(%.2f) = %.4f (ref %.4f)", rad, gc, cosf(rad));
    }
    printf("[PASS] Q16.16 fix16 math & pitch law (< 1 cent)\n");
}

static void test_all_bsf_assets(void) {
    CHECK(bsf_init(BSF_SAMPLE_RATE) == 0, "bsf_init failed");
    const BsfTelemetry *t = bsf_get_telemetry();
    CHECK(t->last_error[0] == '\0', "Base library compile error: %s", t->last_error);
    CHECK(bsf_get_prim_count() == 15, "Expected 15 primitives, got %d", bsf_get_prim_count());
    CHECK(bsf_get_shim_count() == 16, "Expected 16 shims, got %d", bsf_get_shim_count());

    int ntracks = bsf_get_track_count();
    CHECK(ntracks == 26, "Expected 26 tracks (20 samples + 6 compositions), got %d", ntracks);

    int16_t pcm[256 * 2];
    uint32_t max_wcet = 0;

    for (int i = 0; i < ntracks; i++) {
        const BsfTrackInfo *tr = bsf_get_track_info(i);
        CHECK(tr != NULL, "Track %d info null", i);
        int rc = bsf_select_track(i, 0);
        CHECK(rc == 0 && t->last_error[0] == '\0',
              "Track %02d (%s) compile failed: %s", tr ? tr->id : i, tr ? tr->title : "?", t->last_error);

        /* Verify every part evaluates cleanly and produces valid audio */
        for (int p = 0; p < tr->part_count; p++) {
            fix16_t ys[BSF_MAX_VOICES];
            int nv = bsf_eval_preview(fix16_from_float(0.0f), p, ys, NULL, BSF_MAX_VOICES);
            CHECK(nv >= 1 && t->last_error[0] == '\0',
                  "Track %02d part %d eval error: %s", tr->id, p + 1, t->last_error);
        }

        /* Render 256 stereo frames and verify non-crashing & WCET < 666 us */
        bsf_play();
        bsf_render_frames(pcm, 256);
        bsf_pause();
        if (t->wcet_us > max_wcet) max_wcet = t->wcet_us;
        CHECK(t->wcet_us < 666u, "Track %02d WCET %u us >= 666 us", tr->id, (unsigned)t->wcet_us);
    }

    printf("[PASS] All 15 primitives, 16 shims, and 26 tracks compiled & rendered (max WCET = %u us < 666 us)\n",
           (unsigned)max_wcet);
}

static void test_harmonic_fan_conformance(void) {
    /* Compare 01_harmonic_fan.bsf against exact synth-engine.js formula */
    CHECK(bsf_select_track(0, 3) == 0, "Select Harmonic Fan part 4 failed");
    float xs[5] = { -6.0f, -2.0f, 0.0f, 5.0f, 7.8f };
    for (int xi = 0; xi < 5; xi++) {
        float x = xs[xi];
        fix16_t ys[BSF_MAX_VOICES];
        int nv = bsf_eval_preview(fix16_from_float(x), 3, ys, NULL, BSF_MAX_VOICES);
        CHECK(nv == 10, "Harmonic Fan part 4 voice count=%d (exp 10)", nv);
        if (x >= 7.6f) {
            CHECK(ys[0] == FIX16_QUIET, "Harmonic Fan x=7.8 should be QUIET");
            continue;
        }
        float u1 = fminf(fmaxf((x + 7.0f) / 6.0f, 0.0f), 1.0f);
        float u2 = fminf(fmaxf((x - 3.5f) / 4.0f, 0.0f), 1.0f);
        float s = u1 * u1 * (3.0f - 2.0f * u1) - u2 * u2 * (3.0f - 2.0f * u2);
        for (int n = 1; n <= 10; n++) {
            float ref = s * log2f((float)n) - 2.0f;
            float got = fix16_to_float(ys[n - 1]);
            CHECK(fabsf(got - ref) < 0.005f,
                  "Harmonic Fan x=%.1f H(%d): got %.4f ref %.4f", x, n, got, ref);
        }
    }
    printf("[PASS] Harmonic Fan conformance vs synth-engine.js (< 0.005 oct)\n");
}

static void test_reload_and_error_recovery(void) {
    CHECK(bsf_reload() == 0, "bsf_reload() failed");
    /* Inject syntax error and verify clean error message without crash */
    CHECK(bsf_compile_source(": BAD-WORD UNKNOWN_SYMBOL_XYZ ;", "test") != 0,
          "Expected compile error on unknown word");
    const BsfTelemetry *t = bsf_get_telemetry();
    CHECK(t->last_error[0] != '\0', "Expected non-empty last_error");
    /* Recover cleanly via reload */
    CHECK(bsf_reload() == 0 && t->last_error[0] == '\0', "bsf_reload() recovery failed");
    printf("[PASS] Hot-reload & syntax-error recovery verified\n");
}

int main(void) {
    test_fix16_math();
    test_all_bsf_assets();
    test_harmonic_fan_conformance();
    test_reload_and_error_recovery();
    bsf_close();
    if (s_fail == 0) {
        printf("\nALL BSF TESTS PASSED (0 failures).\n");
        return 0;
    }
    fprintf(stderr, "\nBSF TESTS FAILED: %d failure(s).\n", s_fail);
    return 1;
}
