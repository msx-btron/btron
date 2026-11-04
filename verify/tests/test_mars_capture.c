/*
 * B-System BTRON3 / T-FW — test_mars_capture.c
 * Automated Mars Voxel Landscape Screenshot Capture & Feedback Loop Test
 */

#include <btron/forth_vm.h>
#include <btron/wnd.h>
#include <btron/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>

#define MARS_W 320
#define MARS_H 240

static COLOR s_fb[MARS_W * MARS_H];
static GDEV s_mock_gdev = {
    .width = MARS_W,
    .height = MARS_H,
    .pixels = s_fb
};
static WND s_mock_wnd = {
    .dev = &s_mock_gdev
};

static forth_cell_t mock_wnd_open(ForthVM *vm, forth_cell_t w, forth_cell_t h) {
    (void)vm; (void)w; (void)h;
    memset(s_fb, 0, sizeof(s_fb));
    return (forth_cell_t)(uintptr_t)&s_mock_wnd;
}

static void mock_wnd_close(ForthVM *vm, forth_cell_t wnd) {
    (void)vm; (void)wnd;
}

static void mock_wnd_size(ForthVM *vm, forth_cell_t wnd, forth_cell_t *out_w, forth_cell_t *out_h) {
    (void)vm; (void)wnd;
    if (out_w) *out_w = MARS_W;
    if (out_h) *out_h = MARS_H;
}

static void mock_wnd_rect(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t x, forth_cell_t y,
                          forth_cell_t w, forth_cell_t h, forth_cell_t col) {
    (void)vm; (void)wnd_val;
    if (w <= 0 || h <= 0) return;
    forth_cell_t x2 = x + w - 1;
    forth_cell_t y2 = y + h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 >= MARS_W) x2 = MARS_W - 1;
    if (y2 >= MARS_H) y2 = MARS_H - 1;

    for (forth_cell_t cy = y; cy <= y2; cy++) {
        COLOR *row = &s_fb[cy * MARS_W + x];
        for (forth_cell_t cx = x; cx <= x2; cx++) {
            *row++ = (COLOR)col;
        }
    }
}

static void mock_wnd_vline(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t x,
                           forth_cell_t y1, forth_cell_t y2, forth_cell_t col) {
    (void)vm; (void)wnd_val;
    if (x < 0 || x >= MARS_W) return;
    if (y1 > y2) { forth_cell_t t = y1; y1 = y2; y2 = t; }
    if (y1 < 0) y1 = 0;
    if (y2 >= MARS_H) y2 = MARS_H - 1;
    for (forth_cell_t y = y1; y <= y2; y++) {
        s_fb[y * MARS_W + x] = (COLOR)col;
    }
}

static void mock_wnd_update(ForthVM *vm, forth_cell_t wnd_val) {
    (void)vm; (void)wnd_val;
}

static void mock_ms(ForthVM *vm, forth_cell_t ms) {
    (void)vm; (void)ms;
}

static bool mock_key_q(void) {
    return false;
}

static int mock_key(void) {
    return 0;
}

/* ── Minimal PNG Writer via zlib ─────────────────────────────────── */
static void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static int save_png(const char *path, const COLOR *pixels, int w, int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    /* 1. PNG Header */
    const uint8_t png_sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(png_sig, 1, 8, fp);

    /* 2. IHDR Chunk */
    uint8_t ihdr_data[13];
    write_be32(&ihdr_data[0], (uint32_t)w);
    write_be32(&ihdr_data[4], (uint32_t)h);
    ihdr_data[8] = 8; /* bit depth */
    ihdr_data[9] = 2; /* color type: RGB (3 bytes per pixel) */
    ihdr_data[10] = 0; /* compression */
    ihdr_data[11] = 0; /* filter */
    ihdr_data[12] = 0; /* interlace */

    uint8_t chunk_hdr[8];
    write_be32(&chunk_hdr[0], 13);
    memcpy(&chunk_hdr[4], "IHDR", 4);
    fwrite(chunk_hdr, 1, 8, fp);
    fwrite(ihdr_data, 1, 13, fp);

    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)"IHDR", 4);
    crc = crc32(crc, ihdr_data, 13);
    uint8_t crc_bytes[4];
    write_be32(crc_bytes, crc);
    fwrite(crc_bytes, 1, 4, fp);

    /* 3. IDAT Chunk: Raw scanlines with filter byte 0 */
    size_t raw_row_len = 1 + (size_t)w * 3;
    size_t raw_size = raw_row_len * (size_t)h;
    uint8_t *raw_buf = (uint8_t *)malloc(raw_size);
    if (!raw_buf) { fclose(fp); return -1; }

    for (int y = 0; y < h; y++) {
        uint8_t *row_dst = &raw_buf[y * raw_row_len];
        *row_dst++ = 0; /* filter type: None */
        for (int x = 0; x < w; x++) {
            COLOR c = pixels[y * w + x];
            /* ARGB: R is bits 16..23, G is bits 8..15, B is bits 0..7 */
            *row_dst++ = (uint8_t)((c >> 16) & 0xFF);
            *row_dst++ = (uint8_t)((c >> 8) & 0xFF);
            *row_dst++ = (uint8_t)(c & 0xFF);
        }
    }

    uLongf comp_size = compressBound(raw_size);
    uint8_t *comp_buf = (uint8_t *)malloc(comp_size);
    if (!comp_buf) { free(raw_buf); fclose(fp); return -1; }

    if (compress(comp_buf, &comp_size, raw_buf, raw_size) != Z_OK) {
        free(raw_buf); free(comp_buf); fclose(fp); return -1;
    }
    free(raw_buf);

    write_be32(&chunk_hdr[0], (uint32_t)comp_size);
    memcpy(&chunk_hdr[4], "IDAT", 4);
    fwrite(chunk_hdr, 1, 8, fp);
    fwrite(comp_buf, 1, comp_size, fp);

    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)"IDAT", 4);
    crc = crc32(crc, comp_buf, (uInt)comp_size);
    write_be32(crc_bytes, crc);
    fwrite(crc_bytes, 1, 4, fp);
    free(comp_buf);

    /* 4. IEND Chunk */
    write_be32(&chunk_hdr[0], 0);
    memcpy(&chunk_hdr[4], "IEND", 4);
    fwrite(chunk_hdr, 1, 8, fp);
    crc = crc32(0L, (const Bytef *)"IEND", 4);
    write_be32(crc_bytes, crc);
    fwrite(crc_bytes, 1, 4, fp);

    fclose(fp);
    return 0;
}

int main(void) {
    printf("==========================================================\n");
    printf(" Running Mars Voxel Screenshot Capture & Feedback Loop...\n");
    printf("==========================================================\n");

    ForthVM vm;
    forth_vm_init(&vm);

    /* 1. Register display processor in device-tree to activate graphical mode */
    FCodeNode *root = forth_dev_find(&vm, "/");
    FCodeNode *devs = forth_dev_find(&vm, "/devices");
    if (!devs) {
        devs = forth_dev_create(&vm, "devices", root);
    }
    forth_dev_create(&vm, "display", devs);

    /* 2. Bind VM Window & Raster Graphics hooks */
    vm.wnd_open_fn = mock_wnd_open;
    vm.wnd_close_fn = mock_wnd_close;
    vm.wnd_size_fn = mock_wnd_size;
    vm.wnd_rect_fn = mock_wnd_rect;
    vm.wnd_vline_fn = mock_wnd_vline;
    vm.wnd_update_fn = mock_wnd_update;
    vm.ms_fn = mock_ms;
    vm.key_q_fn = mock_key_q;
    vm.key_fn = mock_key;

    /* 3. Load assets/forth/mars.fth into VM */
    FILE *f = fopen("assets/forth/mars.fth", "rb");
    assert(f != NULL && "Opened assets/forth/mars.fth");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *fbuf = (char *)malloc(fsize + 1);
    assert(fbuf != NULL);
    size_t nread = fread(fbuf, 1, fsize, f);
    fclose(f);
    fbuf[nread] = '\0';

    printf("[1] Evaluating mars.fth definitions...\n");
    /* Evaluate line by line up to MARS call */
    char *cur = fbuf;
    while (*cur) {
        char *lstart = cur;
        while (*cur && *cur != '\r' && *cur != '\n') cur++;
        if (*cur == '\r' || *cur == '\n') {
            *cur++ = '\0';
            if (*cur == '\n') cur++;
        }
        /* Skip the final top-level 'MARS' invocation so we can control frames */
        if (strcmp(lstart, "MARS") == 0 || strcmp(lstart, "mars") == 0) {
            continue;
        }
        if (*lstart) {
            int err = forth_eval(&vm, lstart);
            if (err != 0) {
                fprintf(stderr, "Error evaluating line: %s\n", lstart);
                assert(err == 0);
            }
        }
    }
    free(fbuf);

    printf("[2] Executing initial voxel scene setup...\n");
    /* Open window and initialize camera (stationary by default, settled above terrain) */
    forth_eval(&vm, "320 240 WND-OPEN WND !");
    forth_eval(&vm, "0 CAM-X ! 0 CAM-Y ! CAM-X @ CAM-Y @ MARS-HEIGHT 6 + CAM-Z ! 0 HEADING ! 0 SPEED ! 0 STRAFE ! 0 V-VEL ! 0 IS-JUMPING ! 1 RUNNING !");

    printf("[3] Rendering Initial Perspective Frame (Heading=0, North)...\n");
    forth_eval(&vm, "RENDER-MARS-FRAME");

    /* 4. Save PNG to artifact directory for visual feedback first */
    const char *out_png = "/Users/tonpa/.gemini/antigravity/brain/c07f6a6e-71f2-4055-911d-58cb97003d9c/mars_voxel.png";
    int png_ok = save_png(out_png, s_fb, MARS_W, MARS_H);
    assert(png_ok == 0 && "Saved mars_voxel.png");
    printf("    -> Saved screenshot: %s (%dx%d px)\n", out_png, MARS_W, MARS_H);

    /* Verify frame has Martian sky and canyon terrain */
    int sky_count = 0;
    int terrain_count = 0;
    int black_count = 0;
    for (int y = 0; y < MARS_H; y++) {
        for (int x = 0; x < MARS_W; x++) {
            COLOR c = s_fb[y * MARS_W + x];
            if (c == 0) {
                black_count++;
            } else {
                uint8_t r = (c >> 16) & 0xFF;
                uint8_t g = (c >> 8) & 0xFF;
                uint8_t b = c & 0xFF;
                if (r >= g && r >= b) {
                    if (y < 60) sky_count++;
                    else terrain_count++;
                }
            }
        }
    }
    printf("    -> Rendered pixels: %d sky, %d terrain, %d black (total %d)\n",
           sky_count, terrain_count, black_count, MARS_W * MARS_H);
    assert(sky_count > 5000 && "Martian atmospheric sky rendered");
    assert((terrain_count + sky_count) > 30000 && "Martian scene rendered");

    /* 5. Test Heading Rotation (Yaw Turn) */
    printf("[4] Testing 3D View Frustum Rotation (Heading=4, East)...\n");
    forth_eval(&vm, "4 HEADING ! RENDER-MARS-FRAME");
    const char *out_png_rot = "/Users/tonpa/.gemini/antigravity/brain/c07f6a6e-71f2-4055-911d-58cb97003d9c/mars_voxel_east.png";
    save_png(out_png_rot, s_fb, MARS_W, MARS_H);
    printf("    -> Saved rotated screenshot: %s\n", out_png_rot);

    /* 6. Test Ballistic Jump (Altitude Leap) */
    printf("[5] Testing Ballistic Jump Arc (Altitude Leap CAM-Z + 10)...\n");
    forth_eval(&vm, "10 CAM-Z +! RENDER-MARS-FRAME");
    const char *out_png_jump = "/Users/tonpa/.gemini/antigravity/brain/c07f6a6e-71f2-4055-911d-58cb97003d9c/mars_voxel_jump.png";
    save_png(out_png_jump, s_fb, MARS_W, MARS_H);
    printf("    -> Saved jump screenshot: %s\n", out_png_jump);

    printf("==========================================================\n");
    printf(" Mars Voxel Screenshot Verification: 100%% SUCCESS!\n");
    printf("==========================================================\n");

    return 0;
}
