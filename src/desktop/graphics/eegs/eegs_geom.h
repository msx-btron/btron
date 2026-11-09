/*
 * eegs_geom.h — reusable EE/GS geometry for native OpenGL ES apps.
 *
 * Homogeneous clip, viewport map, VU0 (PS2) / scalar MVP transform, and
 * device-space 2D submit.  Quake, XMB and Lil Cu 64 share this instead of
 * walking TinyGL immediate mode when the window is the EEGS layer.
 *
 * eegs_vertex_t.s/t are texels.  Callers pass texels, not 0..1 UVs.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#ifndef BTRON_EEGS_GEOM_H
#define BTRON_EEGS_GEOM_H

#include "backend_eegs.h"

#define EEGS_GEOM_MAX_VERTS 32
#define EEGS_GEOM_CLIP_MAX  40
#define EEGS_GEOM_QUAD_CAP  64
#define EEGS_GEOM_GRID_CAP  1024

typedef struct {
    float x, y, z, w;
    float s, t;
} eegs_clip_vert_t;

/* Column-major 4x4, matching TinyGL / Quake EEGS. */
void eegs_mat_ident(float *m);
void eegs_mat_mul(float *out, const float *a, const float *b);
void eegs_mat_frustum(float *m, float l, float r, float b, float t, float n, float f);
void eegs_mat_rotate(float *m, float deg, float x, float y, float z);
void eegs_mat_translate(float *m, float x, float y, float z);

uint32_t eegs_geom_rgba(float r, float g, float b, float a);

/* Device viewport used by map/submit.  Y grows downward after NDC. */
void eegs_geom_viewport(int width, int height);

/* Six-plane homogeneous clip.  verts[] capacity is EEGS_GEOM_CLIP_MAX. */
int eegs_clip_poly(eegs_clip_vert_t *verts, int n);

/* Perspective divide + viewport.  Returns 0, or -1 if w / GS 12.4 unusable. */
int eegs_geom_map(const eegs_clip_vert_t *pc, uint32_t rgba, eegs_vertex_t *out);

/* Clip then fan-submit clip-space verts.  ST already texels. */
void eegs_geom_submit_poly(const eegs_clip_vert_t *pc, int n, uint32_t rgba,
                           const gs_tex_t *tex, eegs_tri_state_t st);

/* World-space xyz (w=1) through column-major MVP, clip, submit.
 * PS2 uses VU0 transform_batch; other targets use scalar. */
void eegs_geom_world_poly(const float mvp[16], const float *xyz, const float *uv,
                          int nverts, uint32_t rgba, const gs_tex_t *tex,
                          eegs_tri_state_t tri);
void eegs_geom_world_tris(const float mvp[16], const float *xyz, const float *uv,
                          int ntris, uint32_t rgba, const gs_tex_t *tex,
                          eegs_tri_state_t tri);

/* 2D device-space (pixels).  No TinyGL. */
void eegs_geom_fill_rect(int x, int y, int w, int h, uint32_t rgba);
void eegs_geom_fill_quad(const eegs_vertex_t v[4], eegs_tri_state_t st);
void eegs_geom_line_loop(const eegs_vertex_t *v, int n, eegs_tri_state_t st);

/* Pixel-space retained ribbon: xs/ys/bri are row-major rows*cols.
 * brighten=1 is XMB's DST_COLOR/ONE sheet.  Returns verts submitted, or -1. */
int eegs_geom_grid_px(const float *xs, const float *ys, const float *bri,
                      int rows, int cols, int vp_w, int vp_h, int brighten);

/* Pixel-space textured quads.  us/vs are 0..1; converted to texels via tex.
 * cols is interleaved RGBA floats (4 per vertex, 16 per quad).
 * Returns triangles submitted, or -1. */
int eegs_geom_tex_quads_px(const gs_tex_t *tex,
                           const float *xs, const float *ys,
                           const float *us, const float *vs,
                           const float *cols, int nquads,
                           int vp_w, int vp_h, int blended, int brighten);

#endif /* BTRON_EEGS_GEOM_H */
