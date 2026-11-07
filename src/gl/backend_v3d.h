/*
 * src/gl/backend_v3d.h — VideoCore VI (V3D 4.2) Hardware Rasterizer Backend Interface
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_BACKEND_V3D_H
#define BTRON_GL_BACKEND_V3D_H

#include "gl_dispatch.h"

extern gl_ops_t g_v3d_ops;

void v3d_backend_init(int width, int height, void *pixel_buf);
void v3d_backend_resize(int width, int height, void *pixel_buf);
void v3d_backend_make_current(int width, int height, void *pixel_buf);

#endif /* BTRON_GL_BACKEND_V3D_H */

