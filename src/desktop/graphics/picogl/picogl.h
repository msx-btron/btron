/*
 * picogl.h — PicoGL scene submit and the one-band raster.
 *
 * The seven R_EEGS_* calls on a PicoCalc image forward here. A host test
 * calls these functions directly, so a band can be checked without the panel
 * and without the second core.
 *
 * One bucket set, 16 triangles a band, depth on the triangle. Measured on
 * .build/picocalc-rp2040/btron-picocalc-rp2040.elf before this file was
 * linked: data 3268 + bss 198924 = 202192, assert 215040, so 12848 bytes
 * were free beside SegUI. 16 * 20 * 24 = 7680.
 *
 * The 24 bytes are the three screen vertices, iz (1/w in Q10, larger is
 * closer), the flat RGB565, and one texel. A bound target is drawn at once
 * into the caller's color and iz, so a gear strip longer than 16 triangles
 * does not drop. With no target, a full band still counts as a drop.
 */
#ifndef BTRON_PICOGL_H
#define BTRON_PICOGL_H

#include <stdint.h>

#define PICOGL_BAND_H    16
#define PICOGL_BANDS     20
#define PICOGL_BAND_CAP  16
#define PICOGL_Q10       1024
#define PICOGL_NEAR      8.0f
#define PICOGL_FAR       1024.0f

#define PICOGL_FLAT      1u
#define PICOGL_TEX       2u
#define PICOGL_LIFT      4u
/* World triangles. Integer projection can leave the shared edge pixel
 * in neither face. The span covers one extra pixel on each side. */
#define PICOGL_SEAL      8u

typedef struct {
    int16_t  x0, y0, x1, y1, x2, y2;
    int32_t  iz;
    uint16_t rgb;
    uint8_t  tex;
    uint8_t  flags;
    uint16_t u, v;
} picogl_tri_t;

void picogl_begin_frame(int width, int height);
void picogl_set_camera(const float org[3], const float ang[3], int width, int height);
void picogl_set_mvp(const float m[16]);
void picogl_set_cull(int enable);
void picogl_set_depth(int enable);
void picogl_poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int tex);
void picogl_tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int tex);
void picogl_fill(int x, int y, int w, int h, uint32_t color);
void picogl_string(int x, int y, const char *str, const uint8_t font[96][8]);

/* Color is RGB565. iz is one int32 per pixel, 0 meaning empty, larger
 * meaning closer. stride is in pixels. The pointer stays owned by the caller. */
/* While dst is set, screen triangles are stored and not painted.
 * picogl_begin_frame clears the count. A null dst paints again. */
void picogl_capture(picogl_tri_t *dst, int cap);
int  picogl_captured(void);

/* Paint captured view-space triangles into one panel band, 1:1.
 * be is big-endian RGB565. iz is one band, stride ints wide. clear_rgb
 * is native RGB565. With coverage off, the view rectangle is cleared
 * to that sky and iz is zeroed. With coverage on, the caller has sorted
 * nearer triangles first, iz is left alone, and sky is written only
 * into pixels no triangle filled. */
void picogl_paint_band(const picogl_tri_t *tris, int ntri,
                       uint16_t *be, int stride,
                       int band_y, int band_h,
                       int ox, int oy, int vw, int vh,
                       int32_t *iz, uint16_t clear_rgb);

/* 1 skips a pixel a nearer triangle already wrote. The caller sorts
 * that list by descending iz first and keeps the earlier triangle when
 * iz ties. 0 is the default. Gears and picogl_raster_band leave it off. */
void picogl_set_cover(int on);

/* q and r are n*3 ints, one remainder per edge. y is one int16 per
 * triangle: the next source row that remainder is ready for, or < 0
 * when the slot is cold. Null pointers divide every band, which is
 * what gears and the host raster test do. The caller clears y after
 * the triangle list changes. Bands must be painted downward. */
void picogl_set_edge_resume(int *q, int *r, int16_t *y, int n);

void picogl_bind_target(uint16_t *rgb, int32_t *iz, int w, int h, int stride);
void picogl_clear_target(int color, int depth, uint16_t rgb);

/* One-deep save so the XMB wave can bind a band strip and put the
 * caller's target back. screen_tri is already in screen pixels. rgb is
 * stored as given (the strip wants the panel byte order). */
void picogl_target_push(void);
void picogl_target_pop(void);
void picogl_screen_tri(int x0, int y0, int x1, int y1, int x2, int y2,
                       int32_t iz, uint16_t rgb);
/* One world triangle through the camera from picogl_set_camera. The matrix
 * is taken once per camera in Q12. Clip and the perspective divide are
 * integer. rgba is the same R|G<<8|B<<16 packing as picogl_tris. */
void picogl_world_tri(int x0, int y0, int z0, int x1, int y1, int z1,
                      int x2, int y2, int z2, uint32_t rgba);
/* Same triangle, drawn as dst * (1 + src). src is a small RGB565 lift.
 * The gradient stays visible. Overlapping quads stack. */
void picogl_screen_lift(int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t lift);

/* Raster one band from the bins into rgb[band rows * stride]. iz_row is
 * width ints and is cleared at each scanline. Host tests call this on the
 * same core. No FIFO. */
void picogl_raster_band(int band, uint16_t *rgb, int stride,
                        int32_t *iz_row, int width);

/* Join the span. Core 0 posts triangles on the Nexus ring and does not wait
 * inside snd. picogl_flush_target is the reap: it returns when core 1 has
 * painted every posted triangle, or immediately when core 1 is down and the
 * triangles were painted inline. picogl_flush_target_now is the consumer
 * drain and runs on core 1. */
void picogl_flush_target(void);
void picogl_flush_target_now(void);

/* Nexus SPSC span ring. posted/finished are the producer and consumer
 * cursors. queued is how far core 1 is behind. refused counts S_FULL. */
void     picogl_span_drain(void);
uint32_t picogl_span_posted(void);
uint32_t picogl_span_finished(void);
int      picogl_span_queued(void);
unsigned picogl_span_refused(void);

int  picogl_drop(void);
int  picogl_band_n(int band);
const picogl_tri_t *picogl_band_tri(int band, int i);
int  picogl_fill_n(void);
int  picogl_glyph_n(void);
int  picogl_tri_n(void);
int  picogl_span_n(void);

/* Zero tri/span/drop/fill/glyph counters. Does not unbind or change the viewport. */
void picogl_reset_stats(void);

/* UART `picogl <client> <ms> tri= span= drop= bands=`. Host builds no-op it. */
void picogl_log_frame(const char *client, uint32_t ms, int bands);

/* GLES door. Names stay off gl* so a host link can still include TinyGL. */
void picogl_gl_begin(int mode);
void picogl_gl_end(void);
void picogl_gl_vertex3f(float x, float y, float z);
void picogl_gl_normal3f(float x, float y, float z);
void picogl_gl_color3f(float r, float g, float b);
void picogl_gl_matrix_mode(int mode);
void picogl_gl_load_identity(void);
void picogl_gl_push_matrix(void);
void picogl_gl_pop_matrix(void);
void picogl_gl_rotate_f(float angle, float x, float y, float z);
void picogl_gl_translate_f(float x, float y, float z);
void picogl_gl_frustum(double l, double r, double b, double t, double n, double f);
void picogl_gl_viewport(int x, int y, int w, int h);
void picogl_gl_clear(unsigned mask);
void picogl_gl_clear_color(float r, float g, float b, float a);
void picogl_gl_enable(int cap);
void picogl_gl_disable(int cap);
void picogl_gl_shade_model(int mode);
void picogl_gl_light_fv(int light, int pname, const float *params);
void picogl_gl_material_fv(int face, int pname, const float *params);

/* Core 0 wakes core 1. The host file is a pthread with the same states.
 * state is 0 off, 1 ready, 2 painting, 3 give-up. reap waits for the ring
 * at a yield. sev wakes the consumer. */
void     pico_core1_launch(void);
int      pico_core1_run_flush(void);
void     pico_core1_reap(void);
void     pico_core1_sev(void);
int      pico_core1_state(void);
unsigned pico_core1_queued(void);
unsigned pico_core1_refused(void);

#endif /* BTRON_PICOGL_H */
