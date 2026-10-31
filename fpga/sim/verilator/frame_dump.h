/*
 * frame_dump.h — Pure C99 Video Frame Dumper (PPM / RAW RGB).
 *
 * Captures emulated/simulated VRAM frames for automated regression testing.
 */

#ifndef _FRAME_DUMP_H_
#define _FRAME_DUMP_H_

#include <stdint.h>
#include <stdbool.h>

#define MSX_VDP_WIDTH   256u
#define MSX_VDP_HEIGHT  212u

bool frame_dump_ppm(const char *path, const uint16_t *rgb565, uint32_t w, uint32_t h);
bool frame_dump_rgb24(const char *path, const uint8_t *rgb24, uint32_t w, uint32_t h);

#endif /* _FRAME_DUMP_H_ */

