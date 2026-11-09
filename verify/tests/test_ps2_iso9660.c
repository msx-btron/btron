/*
 * test_ps2_iso9660.c — ISO9660 finder vs a synthetic PVD and btron-ps2.iso
 *
 * The production reader is compiled in; a FILE * supplies sectors.  The
 * synthetic image pins PVD-offset-156-is-a-directory-record (treating
 * that word as an LBA was the first parser bug).  When btron-ps2.iso is
 * present the same finder must locate QUAKE/PAK0.PAK and DRONE1M1.DEM
 * and see maps/e1m1.bsp in the PAK directory.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/drivers/playstation2/ps2_iso9660.c"

static int s_checks, s_failures;

static void pass(const char *what)
{
    s_checks++;
    printf("  [OK]   %s\n", what);
}

static void fail(const char *what)
{
    s_checks++;
    s_failures++;
    printf("  [FAIL] %s\n", what);
}

static void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* 32 sectors of a tiny ISO: PVD at 16, root at 20, QUAKE at 21. */
#define SYN_SECS 32
static uint8_t s_syn[SYN_SECS * PS2_ISO_SECTOR];

static int syn_read(void *ctx, uint32_t lba, uint32_t n, void *dst)
{
    (void)ctx;
    if (lba + n > SYN_SECS) return 0;
    memcpy(dst, s_syn + lba * PS2_ISO_SECTOR, n * PS2_ISO_SECTOR);
    return 1;
}

static uint32_t emit_dirent(uint8_t *sec, uint32_t off, uint32_t lba,
                            uint32_t size, int isdir, const char *name)
{
    size_t nlen = strlen(name);
    uint8_t rec_len = (uint8_t)(33 + nlen);
    if (rec_len & 1) rec_len++;
    memset(sec + off, 0, rec_len);
    sec[off] = rec_len;
    wr_le32(sec + off + 2, lba);
    wr_le32(sec + off + 10, size);
    sec[off + 25] = isdir ? 0x02 : 0x00;
    sec[off + 32] = (uint8_t)nlen;
    memcpy(sec + off + 33, name, nlen);
    return off + rec_len;
}

static void build_synthetic(void)
{
    uint8_t *pvd  = s_syn + 16 * PS2_ISO_SECTOR;
    uint8_t *root = s_syn + 20 * PS2_ISO_SECTOR;
    uint8_t *qdir = s_syn + 21 * PS2_ISO_SECTOR;
    uint8_t *pak  = s_syn + 22 * PS2_ISO_SECTOR;
    uint8_t *dem  = s_syn + 23 * PS2_ISO_SECTOR;
    uint32_t off;

    memset(s_syn, 0, sizeof s_syn);
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    pvd[156] = 34;
    wr_le32(pvd + 156 + 2, 20);
    wr_le32(pvd + 156 + 10, 2048);
    pvd[156 + 25] = 0x02;
    pvd[156 + 32] = 1;
    pvd[156 + 33] = 0;

    off = 0;
    (void)emit_dirent(root, off, 21, 2048, 1, "QUAKE");

    off = 0;
    /* `;1` suffix must still match PAK0.PAK */
    off = emit_dirent(qdir, off, 22, 64, 0, "PAK0.PAK;1");
    (void)emit_dirent(qdir, off, 23, 32, 0, "DRONE1M1.DEM");

    memcpy(pak, "PACK", 4);
    memcpy(dem, "DEMO", 4);
}

static int file_read(void *ctx, uint32_t lba, uint32_t n, void *dst)
{
    FILE *f = (FILE *)ctx;
    if (fseek(f, (long)lba * (long)PS2_ISO_SECTOR, SEEK_SET) != 0) return 0;
    return fread(dst, PS2_ISO_SECTOR, n, f) == n;
}

static void test_synthetic(void)
{
    uint32_t lba = 0, size = 0;
    uint8_t buf[8];

    build_synthetic();

    if (!ps2_iso_find(syn_read, 0, "QUAKE/PAK0.PAK", &lba, &size))
        fail("synthetic PAK0.PAK");
    else if (lba != 22 || size != 64)
        fail("synthetic PAK0.PAK lba/size");
    else
        pass("synthetic QUAKE/PAK0.PAK (strips ;1)");

    if (!ps2_iso_find(syn_read, 0, "quake/drone1m1.dem", &lba, &size))
        fail("synthetic drone1m1 case-fold");
    else if (lba != 23 || size != 32)
        fail("synthetic drone1m1 lba/size");
    else
        pass("synthetic QUAKE/DRONE1M1.DEM case-fold");

    if (ps2_iso_find(syn_read, 0, "QUAKE/NOPE.PAK", &lba, &size))
        fail("missing file must miss");
    else
        pass("missing file misses");

    /* Treating PVD+156 as an LBA would seek past the image. */
    if (!ps2_iso_read_range(syn_read, 0, 22, 0, 4, buf) || memcmp(buf, "PACK", 4) != 0)
        fail("range read PACK magic");
    else
        pass("range read PACK magic");
}

static void test_real_iso(void)
{
    FILE *f;
    uint32_t lba = 0, size = 0;
    uint8_t hdr[12];
    int32_t dirofs, dirlen;
    uint8_t *dir;
    int n, i, found_e1m1 = 0, found_progs = 0;

    f = fopen("btron-ps2.iso", "rb");
    if (!f) {
        printf("  [SKIP] btron-ps2.iso not in cwd — synthetic path only\n");
        return;
    }

    if (!ps2_iso_find(file_read, f, "QUAKE/PAK0.PAK", &lba, &size)) {
        fail("iso QUAKE/PAK0.PAK");
        fclose(f);
        return;
    }
    if (size < 12) {
        fail("iso PAK0.PAK too small");
        fclose(f);
        return;
    }
    printf("  [OK]   iso QUAKE/PAK0.PAK LBA=%u size=%u\n",
           (unsigned)lba, (unsigned)size);
    s_checks++;

    if (!ps2_iso_read_range(file_read, f, lba, 0, 12, hdr)
        || memcmp(hdr, "PACK", 4) != 0) {
        fail("iso PAK magic");
        fclose(f);
        return;
    }
    pass("iso PAK PACK magic");

    memcpy(&dirofs, hdr + 4, 4);
    memcpy(&dirlen, hdr + 8, 4);
    if (dirofs < 0 || dirlen <= 0 || dirofs + dirlen > (int32_t)size) {
        fail("iso PAK directory bounds");
        fclose(f);
        return;
    }

    dir = (uint8_t *)malloc((size_t)dirlen);
    if (!dir) {
        fail("iso PAK directory malloc");
        fclose(f);
        return;
    }
    if (!ps2_iso_read_range(file_read, f, lba, (uint32_t)dirofs,
                            (uint32_t)dirlen, dir)) {
        fail("iso PAK directory read");
        free(dir);
        fclose(f);
        return;
    }

    n = dirlen / 64;
    for (i = 0; i < n; i++) {
        const char *name = (const char *)(dir + i * 64);
        if (strcmp(name, "maps/e1m1.bsp") == 0) found_e1m1 = 1;
        if (strcmp(name, "progs.dat") == 0) found_progs = 1;
    }
    free(dir);

    if (!found_e1m1) fail("iso PAK maps/e1m1.bsp");
    else pass("iso PAK contains maps/e1m1.bsp");
    if (!found_progs) fail("iso PAK progs.dat");
    else pass("iso PAK contains progs.dat");

    if (!ps2_iso_find(file_read, f, "QUAKE/DRONE1M1.DEM", &lba, &size))
        fail("iso QUAKE/DRONE1M1.DEM");
    else if (size < 16)
        fail("iso DRONE1M1.DEM too small");
    else {
        printf("  [OK]   iso QUAKE/DRONE1M1.DEM LBA=%u size=%u\n",
               (unsigned)lba, (unsigned)size);
        s_checks++;
    }

    fclose(f);
}

int main(void)
{
    printf("test_ps2_iso9660: ISO9660 finder (CDVD streaming gate)\n");
    test_synthetic();
    test_real_iso();
    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
