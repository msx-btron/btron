/*
 * ps2_iso9660.c — ISO9660 path finder
 *
 * Specification-derived (ECMA-119).  No vendor code.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_iso9660.h"
#include <string.h>

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static unsigned char up(unsigned char c)
{
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 32u);
    return c;
}

/* ISO name `iso[0..nlen)` equals `want`, ignoring `;version`. */
static int names_eq(const char *iso, int nlen, const char *want)
{
    int i = 0;
    int j = 0;
    if (!iso || !want || nlen < 0) return 0;
    while (i < nlen && iso[i] != ';') {
        unsigned char a = up((unsigned char)iso[i]);
        unsigned char b = up((unsigned char)want[j]);
        if (b == 0) return 0;
        if (a != b) return 0;
        i++;
        j++;
    }
    return want[j] == 0;
}

static int find_in_dir(ps2_iso_read_fn read, void *ctx,
                       uint32_t dir_lba, uint32_t dir_size,
                       const char *component,
                       uint32_t *out_lba, uint32_t *out_size, int *out_dir)
{
    uint8_t sec[PS2_ISO_SECTOR];
    uint32_t remaining = dir_size;
    uint32_t lba = dir_lba;

    if (!read || !component || !component[0]) return 0;

    while (remaining) {
        uint32_t use = remaining < PS2_ISO_SECTOR ? remaining : PS2_ISO_SECTOR;
        uint32_t off = 0;

        if (!read(ctx, lba, 1, sec)) return 0;

        while (off < use) {
            uint8_t rec_len;
            uint8_t nlen;
            uint8_t flags;
            const char *nm;

            rec_len = sec[off];
            if (rec_len == 0) break;
            if (off + rec_len > use) break;

            nlen  = sec[off + 32];
            flags = sec[off + 25];
            nm    = (const char *)(sec + off + 33);

            if (!(nlen == 1 && (nm[0] == 0 || nm[0] == 1))
                && names_eq(nm, (int)nlen, component)) {
                *out_lba  = rd_le32(sec + off + 2);
                *out_size = rd_le32(sec + off + 10);
                *out_dir  = (flags & 0x02) ? 1 : 0;
                return 1;
            }
            off += rec_len;
        }
        lba++;
        remaining -= use;
    }
    return 0;
}

int ps2_iso_find(ps2_iso_read_fn read, void *ctx, const char *path,
                 uint32_t *out_lba, uint32_t *out_size)
{
    uint8_t pvd[PS2_ISO_SECTOR];
    const uint8_t *root;
    uint32_t lba;
    uint32_t size;
    const char *p;

    if (!read || !path || !out_lba || !out_size) return 0;
    if (!read(ctx, PS2_ISO_PVD_LBA, 1, pvd)) return 0;
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return 0;

    root = pvd + 156;
    lba  = rd_le32(root + 2);
    size = rd_le32(root + 10);

    p = path;
    while (*p == '/' || *p == '\\') p++;
    if (!*p) return 0;

    while (*p) {
        char comp[64];
        int n = 0;
        uint32_t flba = 0, fsize = 0;
        int isdir = 0;

        while (p[n] && p[n] != '/' && p[n] != '\\' && n < 63) {
            comp[n] = p[n];
            n++;
        }
        comp[n] = 0;
        p += n;
        while (*p == '/' || *p == '\\') p++;
        if (!comp[0]) continue;

        if (!find_in_dir(read, ctx, lba, size, comp, &flba, &fsize, &isdir))
            return 0;

        if (*p) {
            if (!isdir) return 0;
            lba  = flba;
            size = fsize;
        } else {
            *out_lba  = flba;
            *out_size = fsize;
            return 1;
        }
    }
    return 0;
}

int ps2_iso_read_range(ps2_iso_read_fn read, void *ctx,
                       uint32_t file_lba, uint32_t file_off,
                       uint32_t len, void *dst)
{
    uint8_t sec[PS2_ISO_SECTOR];
    uint8_t *out = (uint8_t *)dst;
    uint32_t remaining = len;
    uint32_t lba = file_lba + (file_off / PS2_ISO_SECTOR);
    uint32_t skip = file_off % PS2_ISO_SECTOR;

    if (!read || !dst) return 0;
    if (len == 0) return 1;

    while (remaining) {
        uint32_t n;
        if (!read(ctx, lba, 1, sec)) return 0;
        n = PS2_ISO_SECTOR - skip;
        if (n > remaining) n = remaining;
        memcpy(out, sec + skip, n);
        out += n;
        remaining -= n;
        skip = 0;
        lba++;
    }
    return 1;
}
