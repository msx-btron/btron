/*
 * verify/tests/test_ps2_vu0_kernel.c — Unit tests for VU0 Procedural Geometry Kernel (L3)
 *
 * Validates S³ → ℝ³ Hopf fibration projection and XMB ribbon lattice synthesis.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "../../src/drivers/playstation2/ps2_vu0_kernel.h"
#include "../../src/drivers/playstation2/ps2_vu0_kernel.c"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  [FAIL] %s\n", what);
    }
}

static void check_float(const char *what, float got, float want, float eps)
{
    s_checks++;
    if (fabsf(got - want) > eps) {
        s_failures++;
        printf("  [FAIL] %s got=%f want=%f (diff=%f)\n", what, got, want, fabsf(got - want));
    }
}

static void test_hopf_projection(void)
{
    vu0_hopf_params_t params;
    memset(&params, 0, sizeof(params));

    /* Identity rotation */
    params.m[0][0] = 1.0f;
    params.m[1][1] = 1.0f;
    params.m[2][2] = 1.0f;
    params.scale = 1.8f;
    params.z_offset = -1.2f;
    params.min_denom = 0.08f;

    /* Test 1: Equatorial point (1, 0, 0, 0) on S³ */
    vu0_vec4_t in_pts[2];
    vu0_vec4_t out_pts[2];

    in_pts[0].x = 1.0f;
    in_pts[0].y = 0.0f;
    in_pts[0].z = 0.0f;
    in_pts[0].w = 0.0f;

    /* Test 2: Point near pole where x3 = 1.0 (clamping test) */
    in_pts[1].x = 0.0f;
    in_pts[1].y = 0.0f;
    in_pts[1].z = 0.0f;
    in_pts[1].w = 1.05f;

    ps2_vu0_hopf_project_batch(in_pts, 2, &params, out_pts);

    /* denom = 1.08 - 0 = 1.08; X3d = 1.0 / 1.08 = 0.9259259f */
    /* final x = 0.9259259 * 1.8 = 1.6666666f */
    check_float("hopf pt 0 X matches analytical stereographic projection",
                out_pts[0].x, 1.6666666f, 1e-4f);
    check_float("hopf pt 0 Y is zero", out_pts[0].y, 0.0f, 1e-5f);
    check_float("hopf pt 0 Z is z_offset", out_pts[0].z, -1.2f, 1e-5f);
    check_float("hopf pt 0 W is 1.0f", out_pts[0].w, 1.0f, 1e-5f);

    /* denom for pt 1 is 1.08 - 1.05 = 0.03 < min_denom (0.08) -> clamped to 0.08 */
    check("hopf pt 1 does not produce NaN/Inf",
          !isnan(out_pts[1].x) && !isinf(out_pts[1].x));
    check_float("hopf pt 1 Z is z_offset", out_pts[1].z, -1.2f, 1e-5f);
}

static void test_ribbon_lattice(void)
{
    const uint32_t cols = 4;
    const uint32_t rows = 4;
    vu0_vec4_t grid[16];

    ps2_vu0_ribbon_lattice_batch(0.0f, 0.5f, 1.0f, cols, rows, grid);

    /* Corner (0, 0): u=0, v=0 -> x = -1.0, y = -1.0 */
    check_float("grid corner 0 X is -1.0", grid[0].x, -1.0f, 1e-5f);
    check_float("grid corner 0 Y is -1.0", grid[0].y, -1.0f, 1e-5f);
    check_float("grid corner 0 W is 1.0", grid[0].w, 1.0f, 1e-5f);

    /* Center (row 3, col 3): u=1, v=1 -> x = 1.0, y = 1.0 */
    check_float("grid corner 15 X is 1.0", grid[15].x, 1.0f, 1e-5f);
    check_float("grid corner 15 Y is 1.0", grid[15].y, 1.0f, 1e-5f);
    check_float("grid corner 15 W is 1.0", grid[15].w, 1.0f, 1e-5f);

    /* Verify amplitude bounds for all vertices */
    int bounded = 1;
    for (uint32_t i = 0; i < cols * rows; i++) {
        if (fabsf(grid[i].z) > 0.50001f) bounded = 0;
    }
    check("all ribbon Z coordinates stay strictly within amplitude", bounded);
}

static void test_quake_vector_offload(void)
{
    float v[3] = {3.0f, 0.0f, 4.0f};
    float c[3];
    float a[3] = {1.0f, 0.0f, 0.0f};
    float b[3] = {0.0f, 1.0f, 0.0f};
    float fwd[3], right[3], up[3];
    float ang[3] = {0.0f, 90.0f, 0.0f};
    float mvp[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        1.0f, 2.0f, 3.0f, 1.0f
    };
    vu0_vec4_t in, out;

    ps2_vu0_v3_normalize(v);
    check_float("normalize (3,0,4) x", v[0], 0.6f, 1e-5f);
    check_float("normalize (3,0,4) z", v[2], 0.8f, 1e-5f);

    ps2_vu0_v3_cross(a, b, c);
    check_float("i × j = k x", c[0], 0.0f, 1e-6f);
    check_float("i × j = k y", c[1], 0.0f, 1e-6f);
    check_float("i × j = k z", c[2], 1.0f, 1e-6f);

    ps2_vu0_angle_vectors(ang, fwd, right, up);
    check_float("yaw 90 forward x", fwd[0], 0.0f, 1e-5f);
    check_float("yaw 90 forward y", fwd[1], 1.0f, 1e-5f);

    in.x = 1.0f; in.y = 1.0f; in.z = 1.0f; in.w = 1.0f;
    ps2_vu0_transform_batch(mvp, &in, 1, &out);
    check_float("mvp * (1,1,1,1) x", out.x, 2.0f * 1.0f + 1.0f, 1e-5f);
    check_float("mvp * (1,1,1,1) y", out.y, 3.0f * 1.0f + 2.0f, 1e-5f);
    check_float("mvp * (1,1,1,1) z", out.z, 4.0f * 1.0f + 3.0f, 1e-5f);
    check_float("mvp * (1,1,1,1) w", out.w, 1.0f, 1e-5f);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 VU0 Procedural Geometry Kernel (L3) Unit Tests\n");
    printf("==========================================================\n");

    test_hopf_projection();
    test_ribbon_lattice();
    test_quake_vector_offload();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - VU0 procedural kernel contract verified\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - VU0 procedural kernel failed check\n");
    return 1;
}
