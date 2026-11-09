/*
 * src/gl/egl_surface.h — EGL-lite Surface Bridge for B-System
 *
 * Connects a B-System WND / GDEV surface to the active GL backend.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_EGL_SURFACE_H
#define BTRON_EGL_SURFACE_H

#include <btron/wnd.h>
#include <btron/dp.h>
#include "gl_dispatch.h"

typedef struct {
    WND    *wnd;
    int     width;
    int     height;
    COLOR  *pixels;
    void   *gl_ctx;
    /* The name this surface draws under.  SPEC (doc/txt/PS2.txt §19.2):
     * one GS layer at a time, owned by the focused GL window that meets
     * EEGS size water.  layer_home is 1 while this surface holds that
     * layer.  An unfocused GL window is TinyGL (expected framedrop).
     * Size hysteresis (EGL_LIVE_*) still applies to the focused window. */
    GL_BACKEND backend;
    unsigned char layer_home;

    /* Debug FPS readout state, owned and advanced only by egl_swap_buffers().
     * One surface is one window, so each OpenGL window reports its own frame
     * rate into its own title bar.  `fps_win_us` is the clock tick the current
     * counting window opened on, `fps_frames` frames since then, `fps_shown` the
     * last whole-number fps computed, and `fps_base` the app's own caption
     * captured before the readout decorates it.
     *
     * `fps_open` says that the window has been opened and `fps_base` captured.
     * It is a flag and not `fps_win_us != 0`, because 0 is a legal clock tick --
     * dp_core.c's weak default is `return 0` on every target without an
     * installed clock -- and a sentinel that collides with a real value re-captures
     * the base from the already-decorated caption on every frame, which is how the
     * readout ended up appended to itself instead of replacing itself.
     *
     * `fps_dead` says the clock cannot time a frame, so the readout gives up and
     * leaves the app's own caption alone.  A number computed from a clock that
     * never advances is not a measurement, and a window title is the wrong place
     * to print one. */
    uint32_t fps_frames;
    uint32_t fps_win_us;
    uint32_t fps_shown;
    uint32_t fps_open;
    uint32_t fps_dead;
    char     fps_base[64];
    char     fps_last[64];
} EGL_SURFACE;

/* Client-pixel hysteresis for the live TinyGL↔EEGS switch.  Tile XMB
 * (266×190 window, ~258×154 client) sits below the low water.  Default
 * 520×360 XMB sits above the high water.  The band between keeps the
 * current backend so a drag across 280×180 does not flicker. */
#define EGL_LIVE_TINYGL_W  280
#define EGL_LIVE_TINYGL_H  180
#define EGL_LIVE_EEGS_W    320
#define EGL_LIVE_EEGS_H    220

/* Bind or create GL surface for window */
EGL_SURFACE* egl_create_window_surface(WND *wnd);
void         egl_make_current(EGL_SURFACE *surf);
void         egl_surface_resize(EGL_SURFACE *surf, int width, int height);
void         egl_swap_buffers(EGL_SURFACE *surf);
void         egl_destroy_surface(EGL_SURFACE *surf);
/* Apply focus+size TinyGL↔EEGS.  The focused GL window of EEGS size
 * takes the GS layer; the previous holder is demoted to TinyGL.
 * Cheap if the backend is already the one focus and client size want. */
void         egl_surface_apply_live_backend(EGL_SURFACE *surf);

#endif /* BTRON_EGL_SURFACE_H */
