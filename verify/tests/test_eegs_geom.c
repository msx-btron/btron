/*
 * test_eegs_geom.c — host gate for shared EEGS clip / viewport / 2D submit.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../src/desktop/graphics/eegs/eegs_geom.h"
#include "../../src/desktop/graphics/eegs/backend_eegs.h"
#include "../../src/drivers/playstation2/ps2_gs_rec.h"
#include "../../src/drivers/playstation2/ps2_gs_pack.h"

static int s_checks, s_fail;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_fail++;
        printf("  [FAIL] %s\n", what);
    }
}

static uint8_t s_buf[4096];
static uint32_t s_n;
static const gs_tex_t *s_find_tex;

static void rec_sink(void *user, const uint64_t *q, uint32_t nq)
{
    uint32_t bytes = nq * 16u;
    (void)user;
    if (s_n + bytes > sizeof s_buf) return;
    memcpy(s_buf + s_n, q, bytes);
    s_n += bytes;
}

int main(void)
{
    eegs_clip_vert_t v[EEGS_GEOM_CLIP_MAX];
    float m[16];
    int n;

    printf("==========================================================\n");
    printf(" EEGS geom: clip, viewport, world poly\n");
    printf("==========================================================\n");

    /* Identity * frustum is well-formed. */
    eegs_mat_ident(m);
    check("identity m[0]=1", m[0] == 1.0f && m[15] == 1.0f);
    eegs_mat_frustum(m, -0.1f, 0.1f, -0.1f, 0.1f, 0.1f, 1000.0f);
    check("frustum m[11]=-1", m[11] == -1.0f);

    /* A unit square in clip space (all inside) stays 4 verts. */
    memset(v, 0, sizeof v);
    v[0].x = -0.5f; v[0].y = -0.5f; v[0].z = 0.0f; v[0].w = 1.0f;
    v[1].x =  0.5f; v[1].y = -0.5f; v[1].z = 0.0f; v[1].w = 1.0f;
    v[2].x =  0.5f; v[2].y =  0.5f; v[2].z = 0.0f; v[2].w = 1.0f;
    v[3].x = -0.5f; v[3].y =  0.5f; v[3].z = 0.0f; v[3].w = 1.0f;
    n = eegs_clip_poly(v, 4);
    check("inside quad survives clip", n == 4);

    /* Near-plane straddle: one vertex behind z=-w.  Must keep >= 3 verts
     * and snap the clipped z onto -w so 1/w cannot explode. */
    memset(v, 0, sizeof v);
    v[0].x = 0.0f; v[0].y = 0.0f; v[0].z = -2.0f; v[0].w = 0.05f; /* behind near */
    v[1].x = 0.2f; v[1].y = 0.0f; v[1].z =  0.0f; v[1].w = 1.0f;
    v[2].x = 0.0f; v[2].y = 0.2f; v[2].z =  0.0f; v[2].w = 1.0f;
    n = eegs_clip_poly(v, 3);
    check("near-straddle keeps a polygon", n >= 3);
    {
        int i, ok = 1;
        for (i = 0; i < n; i++) {
            if (v[i].w + v[i].z < -1e-4f) ok = 0;
            if (v[i].w <= 1e-4f) ok = 0;
        }
        check("clipped verts sit on or inside the near plane", ok);
    }

    eegs_geom_viewport(640, 480);
    {
        eegs_clip_vert_t c;
        eegs_vertex_t o;
        memset(&c, 0, sizeof c);
        c.x = 0.0f; c.y = 0.0f; c.z = 0.0f; c.w = 1.0f;
        check("origin maps inside GS 12.4", eegs_geom_map(&c, 0xFFFFFFFFu, &o) == 0);
        check("origin x_px near centre", o.x_px > 100 && o.x_px < 540);
    }

    s_n = 0;
    eegs_frame_begin(rec_sink, NULL);
    eegs_geom_fill_rect(10, 20, 30, 40, 0xFF00FF00u);
    eegs_frame_end();
    check("fill_rect recorded GIF bytes", s_n > 0);
    check("fill_rect submitted two triangles", eegs_prims() == 2);

    /* GL 0..1 maps onto texel centres of the uploaded extent, not the pad.
     * A 384×384 atlas in 512×512 with u=1 is the centre of texel 383. */
    check("uv 1 of 384-in-512 is texel 383.5",
          eegs_geom_uv_to_st(1.0f, 384u) == 383.5f);
    check("uv 0 is the centre of texel 0",
          eegs_geom_uv_to_st(0.0f, 384u) == 0.5f);
    check("pad_w 512 is the wrong converter (would be 511.5, not 384)",
          eegs_geom_uv_to_st(1.0f, 512u) == 511.5f);
    {
        float st = eegs_geom_uv_to_st(63.0f / 383.0f, 384u);
        check("xb_uv cell-0 right edge stays inside the 64-texel cell",
              st > 63.0f && st < 64.0f);
    }

    {
        float xs[4] = {0, 10, 10, 0};
        float ys[4] = {0, 0, 10, 10};
        float us[4] = {0, 1, 1, 0};
        float vs[4] = {0, 0, 1, 1};
        float cols[16];
        int k, rc;
        for (k = 0; k < 16; k++) cols[k] = 1.0f;
        rc = eegs_geom_tex_quads_px(NULL, xs, ys, us, vs, cols, 1,
                                    64, 64, 0, 0);
        check("NULL tex is refused (TinyGL IM fallback)", rc == -1);
    }

    /* PRIM ABE bit 6 follows the blended argument once a plane is bound.
     * A=0 cell corners need ABE on so they do not stamp RGB=0 as a square. */
    {
        static uint32_t plane[64 * 64];
        static gs_tex_t tex;
        float xs[4] = {0, 10, 10, 0};
        float ys[4] = {0, 0, 10, 10};
        float us[4] = {0, 1, 1, 0};
        float vs[4] = {0, 0, 1, 1};
        float cols[16];
        int k;
        const eegs_chain_rec *ch;
        for (k = 0; k < 16; k++) cols[k] = 1.0f;
        memset(&tex, 0, sizeof tex);
        tex.plane = plane;
        tex.px_w = tex.px_h = 64;
        tex.pad_w = tex.pad_h = 64;
        tex.tw_exp = tex.th_exp = 6;
        s_find_tex = &tex;

        s_n = 0;
        eegs_frame_begin(rec_sink, NULL);
        check("blended=0 tex quad records",
              eegs_geom_tex_quads_px(&tex, xs, ys, us, vs, cols, 1,
                                     64, 64, 0, 0) >= 0);
        eegs_frame_end();
        ch = eegs_chain_at(0);
        check("blended=0 chain exists", ch != 0);
        if (ch) {
            check("blended=0 ABE bit 6 is clear",
                  (ch->prim & (1u << 6)) == 0);
            check("blended=0 TME bit 4 is set",
                  (ch->prim & (1u << 4)) != 0);
        }

        s_n = 0;
        eegs_frame_begin(rec_sink, NULL);
        check("blended=1 tex quad records",
              eegs_geom_tex_quads_px(&tex, xs, ys, us, vs, cols, 1,
                                     64, 64, 1, 0) >= 0);
        eegs_frame_end();
        ch = eegs_chain_at(0);
        check("blended=1 chain exists", ch != 0);
        if (ch) {
            check("blended=1 ABE bit 6 is set",
                  (ch->prim & (1u << 6)) != 0);
            check("blended=1 TME bit 4 is set",
                  (ch->prim & (1u << 4)) != 0);
        }
        s_find_tex = 0;
    }

    printf("%d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}

const gs_tex_t *gs_tex_find(const void *plane)
{
    if (s_find_tex && plane == s_find_tex->plane)
        return s_find_tex;
    return NULL;
}
