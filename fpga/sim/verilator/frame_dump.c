/*
 * frame_dump.c — Pure C99 Frame Exporter Implementation.
 */

#include "frame_dump.h"
#include <stdio.h>
#include <stdlib.h>

bool frame_dump_ppm(const char *path, const uint16_t *rgb565, uint32_t w, uint32_t h)
{
    FILE *f;
    uint32_t i;
    if (!path || !rgb565 || !w || !h) return false;
    f = fopen(path, "wb");
    if (!f) return false;

    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (i = 0; i < w * h; i++) {
        uint16_t p = rgb565[i];
        uint8_t r = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
        uint8_t g = (uint8_t)(((p >> 5)  & 0x3F) * 255 / 63);
        uint8_t b = (uint8_t)(( p        & 0x1F) * 255 / 31);
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }
    fclose(f);
    return true;
}

bool frame_dump_rgb24(const char *path, const uint8_t *rgb24, uint32_t w, uint32_t h)
{
    FILE *f;
    if (!path || !rgb24 || !w || !h) return false;
    f = fopen(path, "wb");
    if (!f) return false;

    fprintf(f, "P6\n%u %u\n255\n", w, h);
    fwrite(rgb24, 1, w * h * 3u, f);
    fclose(f);
    return true;
}

