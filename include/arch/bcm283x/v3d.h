/*
 * include/arch/bcm283x/v3d.h — VideoCore VI (V3D 4.2) Driver Interface
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _ARCH_BCM283X_V3D_H_
#define _ARCH_BCM283X_V3D_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ident0;
    uint32_t ident1;
    uint32_t ident2;
    uint32_t ident3;
    uint32_t clock_hz;
    uint32_t qpu_count;
    bool     initialized;
    bool     hardware_present;
} v3d_info_t;

/* Initialize VideoCore VI V3D 4.2 engine (Mailbox power/clock + register probe) */
int v3d_init(void);

/* Query whether hardware V3D acceleration is active */
bool v3d_is_available(void);

/* Get V3D hardware telemetry and state */
const v3d_info_t *v3d_get_info(void);

/* Clear hardware frame with ARGB color */
int v3d_clear(uint32_t color, uint32_t width, uint32_t height);

/* Emit a test triangle using V3D 4.2 CLE (Tile Binning + Render) */
int v3d_test_triangle(uint32_t width, uint32_t height);

typedef struct {
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float pad;
} v3d_vertex_t;

/* Submit vertex batches to V3D 4.2 Control List Executor */
int v3d_submit_primitives(int shader_type, const void *vertex_data, uint32_t vertex_count, uint32_t stride);

/* Emit graphical primitive batch (Triangles, Strips, Fans, Lines) with QPU shader */
int v3d_emit_primitive_batch(int prim_type, int shader_type, const v3d_vertex_t *vertices, uint32_t vertex_count);

/* Set hardware tile clip / viewport */
int v3d_set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/* Set dynamic render target buffer (VC4 framebuffer or window surface) */
int v3d_set_render_target(void *pixel_buf);

/* Upload texture to V3D VRAM */
int v3d_upload_texture(uint32_t tex_id, const void *pixels, uint32_t width, uint32_t height);

/* Flush CLE Thread 1 (Binning) and trigger CLE Thread 0 (Rendering) */
int v3d_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_BCM283X_V3D_H_ */

