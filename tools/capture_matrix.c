/*
 * tools/capture_matrix.c — Automated Headless Matrix Screenshot Generator
 *
 * Renders MATRIX_MODE_CLASSIC, MATRIX_MODE_TRINITY, and MATRIX_MODE_PRIDE
 * through the real B-System Q16.16 libschrift + Matrix BitBlit pipeline and
 * writes standard RGB PNG images to img/screens/ for apps/Matrix.html.
 *
 * Self-contained stored-block PNG encoder (zero external dependencies).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/media/matrix/matrix_internal.h"

#define CAP_W 640
#define CAP_H 400

static uint32_t    g_fb[CAP_W * CAP_H];
static MatrixState g_st;

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        int b;
        crc ^= (uint32_t)buf[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static int write_chunk(FILE *f, const char tag[4], const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    uint8_t crc_be[4];
    uint32_t crc = 0xFFFFFFFFu;

    put_be32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8) { return -1; }
    if (len > 0 && fwrite(data, 1, len, f) != len) { return -1; }
    crc = crc32_update(crc, (const uint8_t *)tag, 4);
    if (len > 0) { crc = crc32_update(crc, data, len); }
    put_be32(crc_be, crc ^ 0xFFFFFFFFu);
    if (fwrite(crc_be, 1, 4, f) != 4) { return -1; }
    return 0;
}

static int write_png_argb32(const char *path, const uint32_t *pixels, int w, int h)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    uint8_t ihdr[13];
    size_t row_bytes = 1u + ((size_t)w * 3u);
    size_t raw_len   = row_bytes * (size_t)h;
    uint8_t *raw;
    uint8_t *zbuf;
    size_t zpos = 0u;
    uint32_t s1 = 1u;
    uint32_t s2 = 0u;
    int y;
    size_t off;
    FILE *f;

    raw  = (uint8_t *)malloc(raw_len);
    zbuf = (uint8_t *)malloc(raw_len + ((raw_len / 65535u) + 2u) * 5u + 16u);
    if (!raw || !zbuf) {
        free(raw); free(zbuf);
        return -1;
    }

    for (y = 0; y < h; y++) {
        uint8_t *dst = &raw[(size_t)y * row_bytes];
        const uint32_t *src = &pixels[y * w];
        int x;
        dst[0] = 0u; /* PNG filter type 0 (None) */
        for (x = 0; x < w; x++) {
            uint32_t c = src[x];
            dst[1 + (x * 3) + 0] = (uint8_t)((c >> 16) & 0xFFu);
            dst[1 + (x * 3) + 1] = (uint8_t)((c >> 8)  & 0xFFu);
            dst[1 + (x * 3) + 2] = (uint8_t)(c & 0xFFu);
        }
    }

    /* Adler-32 over raw scanlines */
    for (off = 0u; off < raw_len; off++) {
        s1 = (s1 + raw[off]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }

    /* Zlib header (no compression, stored DEFLATE blocks) */
    zbuf[zpos++] = 0x78u;
    zbuf[zpos++] = 0x01u;
    off = 0u;
    while (off < raw_len) {
        size_t rem = raw_len - off;
        uint16_t blk = (rem > 65535u) ? 65535u : (uint16_t)rem;
        uint16_t nblk = (uint16_t)(~blk);
        uint8_t is_last = ((off + blk) >= raw_len) ? 1u : 0u;
        zbuf[zpos++] = is_last;
        zbuf[zpos++] = (uint8_t)(blk & 0xFFu);
        zbuf[zpos++] = (uint8_t)(blk >> 8);
        zbuf[zpos++] = (uint8_t)(nblk & 0xFFu);
        zbuf[zpos++] = (uint8_t)(nblk >> 8);
        memcpy(&zbuf[zpos], &raw[off], blk);
        zpos += blk;
        off += blk;
    }
    put_be32(&zbuf[zpos], (s2 << 16) | s1);
    zpos += 4u;

    f = fopen(path, "wb");
    if (!f) {
        free(raw); free(zbuf);
        return -1;
    }
    put_be32(ihdr + 0, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8u; ihdr[9] = 2u; ihdr[10] = 0u; ihdr[11] = 0u; ihdr[12] = 0u;

    (void)fwrite(sig, 1, 8, f);
    (void)write_chunk(f, "IHDR", ihdr, 13u);
    (void)write_chunk(f, "IDAT", zbuf, (uint32_t)zpos);
    (void)write_chunk(f, "IEND", NULL, 0u);
    fclose(f);

    free(raw);
    free(zbuf);
    return 0;
}

static int capture_mode(MatrixMode mode, const char *out_path, int warmup_frames)
{
    int i;
    matrix_sim_init(&g_st, mode, CAP_W, CAP_H);
    for (i = 0; i < warmup_frames; i++) {
        matrix_sim_step(&g_st, 20u);
    }
    g_st.full_redraw = 1u;
    matrix_render_argb32(&g_st, g_fb, CAP_W, CAP_H);
    if (write_png_argb32(out_path, g_fb, CAP_W, CAP_H) < 0) {
        fprintf(stderr, "ERROR: failed to write %s\n", out_path);
        return -1;
    }
    printf("Captured %s (%dx%d, %s)\n", out_path, CAP_W, CAP_H, matrix_mode_name((int)mode));
    return 0;
}

int main(void)
{
    if (capture_mode(MATRIX_MODE_CLASSIC, "img/screens/Matrix_Classic.png", 95) < 0) return 1;
    if (capture_mode(MATRIX_MODE_TRINITY, "img/screens/Matrix_Trinity.png", 95) < 0) return 1;
    if (capture_mode(MATRIX_MODE_PRIDE,   "img/screens/Matrix_Pride.png",   95) < 0) return 1;
    return 0;
}
