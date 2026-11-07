/*
 * src/demo/mars.h — MARS 1993 Voxel Landscape Application & CLU Command
 *
 * Cleanroom C99 implementation of Tim J. Clarke's 1993 Martian voxel terrain
 * engine (Pouët 4662 / MARS.COM / Wojciech Muła). Adheres to NASA/JPL rules.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_DEMO_MARS_H
#define BTRON_DEMO_MARS_H

#include <btron/btron.h>
#include <btron/wnd.h>
#include "../../apps/clu/clu.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opens the native BTRON MARS 1993 Voxel Landscape window.
 * Returns pointer to created WND or NULL on failure.
 */
WND* open_mars_window(void);
WND* open_mars_window_rect(int x, int y, int w, int h);

/*
 * Render a complete Tim Clarke 1993 Mars frame directly to an ARGB pixel buffer.
 */
void mars_render_to_buffer(COLOR *dst, int dw, int dh, uint16_t cam_x, uint16_t cam_y, int32_t jump_h);

/*
 * CLU Shell Command Entrypoint: 'mars'
 */
void clu_cmd_mars(const char *args, ShellOutputFn out, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_DEMO_MARS_H */

