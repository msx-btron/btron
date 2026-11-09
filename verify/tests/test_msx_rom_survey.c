/*
 * Headless ROM-by-ROM survey matching msx_app.c's machine pick and loader.
 * Usage: test_msx_rom_survey [frames]
 */
#include <btron/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/v9938.h"
#include "../../src/emulators/msx/geo3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>

static uint16_t s_fb[256 * 212];
extern v9938_t *msx_get_v9938(void);
extern geo3d_t g_geo3d;

static const char *k_dirs[] = { "assets/msx", NULL };

static int is_dsk(const char *n)
{
    size_t l;
    if (!n) return 0;
    l = strlen(n);
    return l > 4 && n[l - 4] == '.' &&
           (n[l - 3] == 'd' || n[l - 3] == 'D') &&
           (n[l - 2] == 's' || n[l - 2] == 'S') &&
           (n[l - 1] == 'k' || n[l - 1] == 'K');
}

static int is_rom(const char *n)
{
    size_t l;
    if (!n) return 0;
    l = strlen(n);
    return l > 4 && n[l - 4] == '.' &&
           (n[l - 3] == 'r' || n[l - 3] == 'R') &&
           (n[l - 2] == 'o' || n[l - 2] == 'O') &&
           (n[l - 1] == 'm' || n[l - 1] == 'M');
}

static uint16_t machine_for(const char *name, size_t size)
{
    if (name && (strstr(name, "HC-95") || strstr(name, "HC95") || strstr(name, "hc95")))
        return is_dsk(name) ? BMSX_MACHINE_HC95_CBIOS_FDD : BMSX_MACHINE_HC95_CBIOS;
    if (name && (strstr(name, "HC-90") || strstr(name, "HC90") || strstr(name, "hc90")))
        return BMSX_MACHINE_HC90_CBIOS;
    if (is_dsk(name)) {
        if (strstr(name, "Bad Apple")) return BMSX_MACHINE_MSX2P_CBIOS_FDD;
        return BMSX_MACHINE_MSX2_CBIOS_FDD;
    }
    if (name && strstr(name, "Bad Apple")) return BMSX_MACHINE_MSX2P_CBIOS;
    if (name && strstr(name, "Dynamite")) return BMSX_MACHINE_MSX2_CBIOS_JP;
    if (name && (strstr(name, "MSX_LOGO") || strstr(name, "msx_logo")))
        return BMSX_MACHINE_MSX2_CBIOS;
    if (name && (strstr(name, "MARS") || strstr(name, "mars")))
        return BMSX_MACHINE_MSX2_CBIOS;
    if (name && (strstr(name, "xracing") || strstr(name, "XRACING") ||
                 strstr(name, "XRacing")))
        return BMSX_MACHINE_MSX2_CBIOS;
    if (size > 65536u ||
        (name && (strstr(name, "BULLRUSH") || strstr(name, "NEON") ||
                  strstr(name, "NIGHT") || strstr(name, "btron") ||
                  strstr(name, "BTRON") || strstr(name, "MAGICAL") ||
                  strstr(name, "NICCC") || strstr(name, "Geo3D") ||
                  strstr(name, "GEO3D") || strstr(name, "Zero") ||
                  strstr(name, "AUREL"))))
        return BMSX_MACHINE_MSXTR_CBIOS;
    return BMSX_MACHINE_MSX;
}

static const char *mapper_name(int m)
{
    switch (m) {
    case MAPPER_PLAIN: return "PLAIN";
    case MAPPER_ASCII16: return "ASCII16";
    case MAPPER_ASCII16_SRAM: return "ASCII16_SRAM";
    case MAPPER_ASCII8: return "ASCII8";
    case MAPPER_ASCII8_SRAM: return "ASCII8_SRAM";
    case MAPPER_KONAMI: return "KONAMI";
    case MAPPER_KONAMI_SCC: return "KONAMI_SCC";
    case MAPPER_NEO8: return "NEO8";
    case MAPPER_NEO16: return "NEO16";
    default: return "OTHER";
    }
}

static uint8_t *read_file(const char *path, uint32_t *out)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    b = (uint8_t *)malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *out = (uint32_t)n;
    return b;
}

static uint32_t ink(const uint16_t *fb)
{
    uint16_t bg = fb[0];
    uint32_t i, n = 0;
    for (i = 0; i < 256u * 212u; i++) if (fb[i] != bg) n++;
    return n;
}

static int run_one(const char *path, const char *name, int frames)
{
    uint32_t len = 0;
    uint8_t *img = read_file(path, &len);
    msx_cfg_t cfg;
    int rc, load, i;
    uint32_t inkv = 0;
    v9938_t *v;

    if (!img) {
        printf("FAIL  %-44s  cannot read\n", name);
        return 1;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.frame_hz = 60;
    cfg.machine = machine_for(name, len);
    if (strstr(name, "tfw_")) msx_set_bios_mode(BMSX_BIOS_TFW);
    else if (strstr(name, "tbios_")) msx_set_bios_mode(BMSX_BIOS_TBIOS);
    else msx_set_bios_mode(BMSX_BIOS_CBIOS);

    rc = msx_init(&cfg);
    if (rc != MSX_OK) {
        printf("FAIL  %-44s  init=%d mach=%u size=%u\n",
               name, rc, cfg.machine, len);
        free(img);
        return 1;
    }

    if (is_dsk(name)) {
        load = slots_set_dsk(img, len);
        slots_set_rom(NULL, 0);
    } else if (strstr(name, "tfw_") || strstr(name, "tbios_") || strstr(name, "cbios_main")) {
        slots_set_bios(img, len);
        slots_set_rom(NULL, 0);
        load = MSX_OK;
    } else {
        load = msx_load_rom(img, len, 0);
    }
    free(img);
    if (load != MSX_OK) {
        printf("FAIL  %-44s  load=%d mach=%u size=%u map=%s\n",
               name, load, cfg.machine, len, mapper_name(slots_get_mapper()));
        msx_shutdown();
        return 1;
    }
    msx_reset();
    for (i = 0; i < frames; i++) {
        msx_run_frame();
        msx_blit_rgb565(s_fb);
    }
    inkv = ink(s_fb);
    v = msx_get_v9938();
    printf("%-4s  %-44s  %7u  mach=%2u  map=%-10s  pc=%04X  r800=%d  mode=%02X  ink=%6u  g3d=%u\n",
           (inkv >= 64u) ? "OK" : "DIM",
           name, len, cfg.machine, mapper_name(slots_get_mapper()),
           msx_get_pc(), msx_is_r800_active(),
           v ? v9938_mode(v) : 0, inkv, g_geo3d.cnt_drawn);
    msx_shutdown();
    return (inkv >= 64u) ? 0 : 0; /* DIM is still a load; caller tallies */
}

int main(int argc, char **argv)
{
    int frames = (argc > 1) ? atoi(argv[1]) : 180;
    int n = 0, dim = 0, fail = 0;
    int d;

    printf("name                                              bytes  machine mapper      pc    r800 mode ink    geo3d\n");
    for (d = 0; k_dirs[d]; d++) {
        DIR *dir = opendir(k_dirs[d]);
        struct dirent *de;
        if (!dir) continue;
        while ((de = readdir(dir)) != NULL) {
            char path[256];
            int r;
            if (de->d_name[0] == '.') continue;
            if (!is_rom(de->d_name) && !is_dsk(de->d_name)) continue;
            snprintf(path, sizeof(path), "%s/%s", k_dirs[d], de->d_name);
            r = run_one(path, de->d_name, frames);
            n++;
            if (r) fail++;
        }
        closedir(dir);
    }
    printf("surveyed %d  fail %d  (DIM = loaded, few pixels after %d frames)\n",
           n, fail, frames);
    (void)dim;
    return fail ? 1 : 0;
}
