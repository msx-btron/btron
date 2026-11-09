/*
 * r_eegs.c — EEGS-native Quake raster (VU0 transform + GS submit).
 *
 * Draw-time does not call TinyGL glBegin/glVertex/glFrustum.  Texture
 * planes still come from the load-time glTexImage2D registry.
 */
#include "../include/r_eegs.h"
#include "../include/texture.h"
#include "gl_dispatch.h"

#include <string.h>
#include <math.h>

#if defined(BTRON_PS2_TARGET)
#include "../../../desktop/graphics/eegs/backend_eegs.h"
#include "../../../desktop/graphics/eegs/eegs_geom.h"
#include "../../../drivers/playstation2/ps2_vu1_geom.h"
#include "../../../drivers/playstation2/ps2_gs_tex.h"
#endif

int R_EEGS_Live(void)
{
#if defined(BTRON_PS2_TARGET)
    return gl_active_backend() == GL_BACKEND_EEGS;
#else
    return 0;
#endif
}

#if !defined(BTRON_PS2_TARGET)

void R_EEGS_BeginFrame(int width, int height) { (void)width; (void)height; }
void R_EEGS_SetCamera(const float org[3], const float ang[3], int width, int height)
{
    (void)org; (void)ang; (void)width; (void)height;
}
void R_EEGS_Poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int gl_tex_idx)
{
    (void)xyz; (void)st; (void)nverts; (void)rgba; (void)gl_tex_idx;
}
void R_EEGS_Fill(int x, int y, int w, int h, uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void R_EEGS_String(int x, int y, const char *str, const uint8_t font[96][8])
{
    (void)x; (void)y; (void)str; (void)font;
}
void R_EEGS_Tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int gl_tex_idx)
{
    (void)xyz; (void)st; (void)ntris; (void)rgba; (void)gl_tex_idx;
}

#else /* BTRON_PS2_TARGET */

static float s_mvp[16] __attribute__((aligned(16)));
static int   s_vp_w, s_vp_h;
static int   s_inited;
static int   s_logged;

static uint32_t rgb_to_rgba(uint32_t color)
{
    unsigned r = (color >> 16) & 0xFFu;
    unsigned g = (color >> 8) & 0xFFu;
    unsigned b = color & 0xFFu;
    return r | (g << 8) | (b << 16) | (0xFFu << 24);
}

static const gs_tex_t *tex_of(int gl_idx)
{
    const void *plane;
    const gs_tex_t *tex;
    if (gl_idx < 0 || gl_idx >= g_num_gl_textures) return NULL;
    if (!g_gl_textures[gl_idx].is_uploaded) return NULL;
    plane = g_gl_textures[gl_idx].gs_plane;
    if (!plane) return NULL;
    tex = gs_tex_find(plane);
    if (tex) return tex;
    /* Layer steal evicted the GS slots; the TinyGL pixmap is still here. */
    tinygl_texture_commit(g_gl_textures[gl_idx].tex_id);
    return gs_tex_find(plane);
}

static eegs_tri_state_t world_st(const gs_tex_t *tex)
{
    eegs_tri_state_t st;
    memset(&st, 0, sizeof st);
    st.textured = tex ? 1u : 0u;
    return st;
}

void R_EEGS_BeginFrame(int width, int height)
{
    if (!s_inited) {
        ps2_vu1_geom_init();
        s_inited = 1;
        if (!s_logged) {
            extern void uart_puts_raw(const char *s);
            uart_puts_raw("[QUAKE] EEGS native VU0+GS (no TinyGL immediate mode)\n");
            s_logged = 1;
        }
    }
    s_vp_w = width;
    s_vp_h = height;
    eegs_geom_viewport(width, height);
    if (width > 0 && height > 0)
        eegs_clear(0, 0, width, height, eegs_geom_rgba(0.08f, 0.06f, 0.05f, 1.0f));
}

void R_EEGS_SetCamera(const float org[3], const float ang[3], int width, int height)
{
    float aspect, proj[16];
    (void)width; (void)height;
    if (s_vp_h <= 0) return;
    aspect = (float)s_vp_w / (float)s_vp_h;
    eegs_mat_frustum(proj, -0.1f * aspect, 0.1f * aspect, -0.1f, 0.1f, 0.1f, 1000.0f);
    memcpy(s_mvp, proj, sizeof s_mvp);
    /* Match R_RenderView's TinyGL camera: Quake X-forward Y-left Z-up. */
    eegs_mat_rotate(s_mvp, -90.0f, 1.0f, 0.0f, 0.0f);
    eegs_mat_rotate(s_mvp,  90.0f, 0.0f, 0.0f, 1.0f);
    eegs_mat_rotate(s_mvp, -ang[2], 1.0f, 0.0f, 0.0f);
    eegs_mat_rotate(s_mvp, -ang[0], 0.0f, 1.0f, 0.0f);
    eegs_mat_rotate(s_mvp, -ang[1], 0.0f, 0.0f, 1.0f);
    eegs_mat_translate(s_mvp, -org[0], -org[1], -org[2]);
    ps2_vu1_geom_set_mvp(s_mvp);
}

void R_EEGS_Poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int gl_tex_idx)
{
    const gs_tex_t *tex = tex_of(gl_tex_idx);
    eegs_geom_world_poly(s_mvp, xyz, st, nverts, rgba, tex, world_st(tex));
}

void R_EEGS_Tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int gl_tex_idx)
{
    const gs_tex_t *tex = tex_of(gl_tex_idx);
    eegs_geom_world_tris(s_mvp, xyz, st, ntris, rgba, tex, world_st(tex));
}

void R_EEGS_Fill(int x, int y, int w, int h, uint32_t color)
{
    eegs_geom_fill_rect(x, y, w, h, rgb_to_rgba(color));
}

void R_EEGS_String(int x, int y, const char *str, const uint8_t font[96][8])
{
    uint32_t rgba = eegs_geom_rgba(0.85f, 0.70f, 0.15f, 1.0f);
    int cur_x;
    if (!str || !font) return;
    eegs_texture(NULL);
    cur_x = x;
    while (*str) {
        int num = (unsigned char)*str;
        int glyph, r;
        if (num < 32 || num > 127) num = '?';
        glyph = num - 32;
        for (r = 0; r < 8; r++) {
            uint8_t row = font[glyph][r];
            int c = 0;
            while (c < 8) {
                int run;
                if ((row & (0x80 >> c)) == 0) { c++; continue; }
                run = c;
                while (c < 8 && (row & (0x80 >> c))) c++;
                eegs_geom_fill_rect(cur_x + run, y + r, c - run, 1, rgba);
            }
        }
        cur_x += 8;
        str++;
    }
}

#endif /* BTRON_PS2_TARGET */
