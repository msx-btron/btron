/*
 * src/quake/sys/fs_btron.c — File System, PAK & BSP Loader for B-System
 *
 * Fast-path strategy:
 *   1. FS_Init indexes pak0.pak (directory only). The archive body stays on
 *      media — Pi maps it at 0x04000000; PS2 never slurps 18.6 MiB.
 *   2. An FNV-1a open-address hash table maps filename → (offset, length).
 *   3. FS_CacheBSP binds ONE .bsp (the current level) and drops it on warp.
 *      e1m1 is 1.36 MiB; all 21 BSPs together are 10.8 MiB.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/fs_btron.h"

#if defined(BTRON_PS2_TARGET)
#include "ps2_cdvd.h"
#include "ps2_iso9660.h"
#include "ps2_fileio.h"
#include "ps2_console.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
size_t ps2_heap_largest_hole(void);
void   ps2_heap_holders(const char *why);
#endif

#if !defined(BTRON_PS2_TARGET)
/* ── Search paths for pak0.pak ─────────────────────────────────── */
static const char *s_search_paths[] = {
    "assets/quake/pak/pak0.pak",
    "assets/quake/id1/pak0.pak",
    "id1/pak0.pak",
    "pak0.pak",
    "/SYS/GAMES/QUAKE/PAK0.PAK",
    "/CHOKANJI/PAK0.PAK",
    NULL
};
#endif

/* ── PAK on-disk header ─────────────────────────────────────────── */
typedef struct {
    char id[4];   /* "PACK" */
    int  dirofs;
    int  dirlen;
} dpackheader_t;

/* ── Whole-PAK image in hunk ────────────────────────────────────── */
static byte        *s_pak_image    = NULL;   /* Entire PAK file in hunk */
static int          s_pak_size     = 0;
static const char  *s_pak_path_str = NULL;

/* ── Hash table ─────────────────────────────────────────────────── */
static fs_ht_slot_t s_ht[FS_HT_SIZE];

/* ── BSP global pre-cache ───────────────────────────────────────── */
fs_bsp_cache_t g_bsp_cache;

/* ── Helpers ────────────────────────────────────────────────────── */

/* FNV-1a 32-bit, case-insensitive (forward slash normalised) */
static uint32_t fnv1a(const char *str) {
    uint32_t h = 0x811c9dc5u;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        /* Normalise backslash → forward slash, uppercase → lowercase */
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = c + 32u;
        h ^= (uint32_t)c;
        h *= 0x01000193u;
    }
    return h ? h : 1u; /* Never store 0 (empty-slot sentinel) */
}

static void ht_insert(uint32_t hash, int offset, int length, const char *name) {
    uint32_t idx = hash & FS_HT_MASK;
    for (uint32_t probe = 0; probe < FS_HT_SIZE; probe++) {
        uint32_t slot = (idx + probe) & FS_HT_MASK;
        if (!s_ht[slot].hash) {
            s_ht[slot].hash   = hash;
            s_ht[slot].offset = offset;
            s_ht[slot].length = length;
            /* Copy name, lowercase-normalised */
            int i = 0;
            while (name[i] && i < 55) {
                char c = name[i];
                if (c == '\\') c = '/';
                if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
                s_ht[slot].name[i] = c;
                i++;
            }
            s_ht[slot].name[i] = '\0';
            return;
        }
    }
    /* Table full — silently drop (should never happen with load < 0.5) */
}

static const fs_ht_slot_t *ht_find(const char *path) {
    uint32_t hash = fnv1a(path);
    uint32_t idx  = hash & FS_HT_MASK;
    for (uint32_t probe = 0; probe < FS_HT_SIZE; probe++) {
        uint32_t slot = (idx + probe) & FS_HT_MASK;
        if (!s_ht[slot].hash) return NULL;           /* Empty — not found */
        if (s_ht[slot].hash == hash) {
            /* Verify full name equality to handle collisions */
            const char *a = s_ht[slot].name;
            const char *b = path;
            int match = 1;
            while (*a || *b) {
                unsigned char ca = (unsigned char)*a++;
                unsigned char cb = (unsigned char)*b++;
                if (cb == '\\') cb = '/';
                if (cb >= 'A' && cb <= 'Z') cb += 32u;
                if (ca != cb) { match = 0; break; }
            }
            if (match) return &s_ht[slot];
        }
    }
    return NULL;
}

int FS_PakIndexed(void)
{
    return ht_find("maps/e1m1.bsp") ? 1 : 0;
}

#if (defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)) || defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
void FS_DropBSP(void)
{
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
}
#endif

/* ────────────────────────────────────────────────────────────────── */
/*   Hosted build (POSIX / QEMU)                                     */
/* ────────────────────────────────────────────────────────────────── */

#if (defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)) || defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)

int FS_Init(void) {
    memset(s_ht, 0, sizeof(s_ht));
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));

    /* ── Check for RAM memory-mapped PAK (e.g. QEMU loader / Pi 400 preload at 0x04000000) ── */
#if (defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)) && !defined(BTRON_HOSTED) && !defined(__APPLE__)
    volatile const char *ram_pak = (const char *)0x04000000;
    if (ram_pak[0] == 'P' && ram_pak[1] == 'A' && ram_pak[2] == 'C' && ram_pak[3] == 'K') {
        const dpackheader_t *hdr = (const dpackheader_t *)ram_pak;
        s_pak_image = (byte *)ram_pak;
        s_pak_size = hdr->dirofs + hdr->dirlen;
        s_pak_path_str = "RAM@0x04000000";
        int num_entries = hdr->dirlen / (int)sizeof(packfile_t);
        if (num_entries > FS_HT_SIZE / 2) num_entries = FS_HT_SIZE / 2;
        const packfile_t *dir = (const packfile_t *)(s_pak_image + hdr->dirofs);
        for (int i = 0; i < num_entries; i++) {
            if (dir[i].filelen <= 0 || dir[i].filepos < 0) continue;
            ht_insert(fnv1a(dir[i].name), dir[i].filepos, dir[i].filelen, dir[i].name);
        }
        Con_Printf("FS_Init: PAK memory-mapped from RAM @ 0x04000000 (%d entries)\n", num_entries);
        return 1;
    }
#endif

    for (int pi = 0; s_search_paths[pi]; pi++) {
        FILE *f = fopen(s_search_paths[pi], "rb");
        if (!f) continue;

        /* ── Read PAK header ─────────────────────────────────────── */
        dpackheader_t hdr;
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
            strncmp(hdr.id, "PACK", 4) != 0) {
            fclose(f); continue;
        }

        /* ── Determine total PAK size ────────────────────────────── */
        fseek(f, 0, SEEK_END);
        long pak_sz = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (pak_sz <= 0) { fclose(f); continue; }

        /* Hosted/Pi: one fread of the archive so FindInPak is zero-copy.
         * PS2 does not take this path; it indexes a mapped image and loads
         * one BSP at a time via FS_CacheBSP. */
        s_pak_image = (byte *)Hunk_AllocName((size_t)pak_sz + 1, "pak0_image");
        if (!s_pak_image) { fclose(f); continue; }

        size_t got = fread(s_pak_image, 1, (size_t)pak_sz, f);
        fclose(f);

        if ((long)got < pak_sz) {
            s_pak_image = NULL;
            Con_DPrintf("FS_Init: short read on %s\n", s_search_paths[pi]);
            continue;
        }
        s_pak_image[pak_sz] = 0;
        s_pak_size     = (int)pak_sz;
        s_pak_path_str = s_search_paths[pi];

        /* ── Build hash table from embedded directory ─────────────── */
        int num_entries = hdr.dirlen / (int)sizeof(packfile_t);
        if (num_entries > FS_HT_SIZE / 2) num_entries = FS_HT_SIZE / 2;

        const packfile_t *dir = (const packfile_t *)(s_pak_image + hdr.dirofs);
        for (int i = 0; i < num_entries; i++) {
            if (dir[i].filelen <= 0 || dir[i].filepos < 0) continue;
            if (dir[i].filepos + dir[i].filelen > s_pak_size) continue;
            ht_insert(fnv1a(dir[i].name), dir[i].filepos,
                      dir[i].filelen, dir[i].name);
        }

        Con_Printf("FS_Init: PAK cached %s (%d KB, %d entries)\n",
                   s_search_paths[pi],
                   (int)(pak_sz / 1024), num_entries);
        return 1;
    }

    Con_DPrintf("FS_Init: no pak0.pak found\n");
    return 0;
}

/* ── Zero-copy PAK lookup ───────────────────────────────────────── */
const byte *FS_FindInPak(const char *path, int *out_len) {
    if (!s_pak_image || !path) return NULL;
    const fs_ht_slot_t *slot = ht_find(path);
    if (!slot) return NULL;
    if (out_len) *out_len = slot->length;
    return s_pak_image + slot->offset;
}

/* ── FS_LoadFile: try PAK cache first, then direct file ─────────── */
byte *FS_LoadFile(const char *path, int *out_len) {
    if (!path) return NULL;

    /* 1. PAK hash-table lookup — O(1), zero-copy reference */
    const byte *pak_ptr = FS_FindInPak(path, out_len);
    if (pak_ptr) {
        /*
         * Callers may write a NUL terminator one byte past.
         * Make a hunk copy only when the caller requests a mutable buffer.
         * For read-only callers (TEX, BSP parser) we return directly.
         * We tag the pointer by returning it as-is; FS_FreeFile is a no-op.
         */
        return (byte *)pak_ptr;   /* Direct pointer into hunk image */
    }

    /* 2. Direct filesystem fallback (loose files, modded content) */
    FILE *f = fopen(path, "rb");
    if (!f && path[0] != '/') {
        char syspath[128];
        snprintf(syspath, sizeof(syspath), "/SYS/%s", path);
        f = fopen(syspath, "rb");
    }
    if (!f) {
        const char *base = strrchr(path, '/');
        if (base && *(base + 1)) {
            char syspath[128];
            snprintf(syspath, sizeof(syspath), "/SYS/%s", base + 1);
            f = fopen(syspath, "rb");
        }
    }
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    int size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) { fclose(f); return NULL; }

    byte *buf = (byte *)Hunk_AllocName((size_t)size + 1, path);
    if (!buf) { fclose(f); return NULL; }

    fread(buf, 1, (size_t)size, f);
    buf[size] = 0;
    fclose(f);

    if (out_len) *out_len = size;
    return buf;
}

/* ── BSP pre-cache: load once, expose lump pointers ─────────────── */
int FS_CacheBSP(const char *bsp_path) {
    if (!bsp_path || !bsp_path[0]) return 0;
    if (g_bsp_cache.is_ready && strcmp(g_bsp_cache.path, bsp_path) == 0) return 1; /* Already cached */

    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
    strncpy(g_bsp_cache.path, bsp_path, sizeof(g_bsp_cache.path) - 1);

    int bsp_len = 0;
    const byte *data = FS_FindInPak(bsp_path, &bsp_len);

    /* Fallback: loose file */
    if (!data) {
        data = FS_LoadFile(bsp_path, &bsp_len);
    }

    if (!data || bsp_len < (int)sizeof(dheader_t)) {
        Con_DPrintf("FS_CacheBSP: %s not found\n", bsp_path);
        return 0;
    }

    const dheader_t *hdr = (const dheader_t *)data;
    if (hdr->version != BSPVERSION) {
        Con_DPrintf("FS_CacheBSP: %s bad version %d\n", bsp_path, hdr->version);
        return 0;
    }

    g_bsp_cache.data   = data;
    g_bsp_cache.length = bsp_len;

    for (int l = 0; l < HEADER_LUMPS; l++) {
        int ofs = hdr->lumps[l].fileofs;
        int len = hdr->lumps[l].filelen;
        if (ofs < 0 || len < 0 || ofs + len > bsp_len) {
            g_bsp_cache.lumps[l]     = NULL;
            g_bsp_cache.lump_lens[l] = 0;
        } else {
            g_bsp_cache.lumps[l]     = data + ofs;
            g_bsp_cache.lump_lens[l] = len;
        }
    }

    g_bsp_cache.is_ready = 1;
    Con_Printf("FS_CacheBSP: cached %s (%d KB)\n",
               bsp_path, bsp_len / 1024);
    return 1;
}

/* ── Freestanding: PS2 streams PAK from CDVD; UEFI has no PAK. ── */
#else

#if defined(BTRON_PS2_TARGET)
extern unsigned char ps2_pak0_image[] __attribute__((weak));
extern unsigned char ps2_pak0_image_end[] __attribute__((weak));

static uint32_t s_pak_lba;          /* ISO LBA of QUAKE/PAK0.PAK (CDVD fallback) */
static int      s_pak_fd = -1;      /* FILEIO fd of cdrom0:QUAKE/PAK0.PAK */
static byte    *s_level_image;      /* one heap BSP; replaced on warp */
/* Drone tracks are 23 KiB.  After e1m1 is cached the first-fit heap may
 * refuse a 23 KiB malloc even with hundreds of KiB free in smaller holes.
 * One static store, one demo at a time. */
#define FS_DEMO_STORE 32768
static byte     s_demo_store[FS_DEMO_STORE];

void FS_DropBSP(void)
{
    if (s_level_image) {
        free(s_level_image);
        s_level_image = NULL;
    }
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
}

static int s_ht_hashed;

static int fs_index_dir(const packfile_t *dir, int num_entries, int pak_sz)
{
    int i;
    int n = 0;
    if (num_entries > FS_HT_SIZE / 2) num_entries = FS_HT_SIZE / 2;
    for (i = 0; i < num_entries; i++) {
        if (dir[i].filelen <= 0 || dir[i].filepos < 0) continue;
        if (dir[i].filepos + dir[i].filelen > pak_sz) continue;
        ht_insert(fnv1a(dir[i].name), dir[i].filepos, dir[i].filelen, dir[i].name);
        n++;
    }
    s_ht_hashed = n;
    return n;
}

static int fs_map_pak_image(byte *img, int pak_sz, const char *path)
{
    const dpackheader_t *hdr;
    const packfile_t *dir;
    int num_entries;

    if (!img || pak_sz < (int)sizeof(dpackheader_t)) return 0;
    if (img[0] != 'P' || img[1] != 'A' || img[2] != 'C' || img[3] != 'K') return 0;
    hdr = (const dpackheader_t *)img;
    if (hdr->dirofs < 0 || hdr->dirlen < 0) return 0;
    if (hdr->dirofs + hdr->dirlen > pak_sz) return 0;
    s_pak_image = img;
    s_pak_size = pak_sz;
    s_pak_path_str = path;
    s_pak_lba = 0;
    s_pak_fd = -1;
    num_entries = hdr->dirlen / (int)sizeof(packfile_t);
    dir = (const packfile_t *)(s_pak_image + hdr->dirofs);
    fs_index_dir(dir, num_entries, pak_sz);
    return 1;
}

static void fs_upcase(char *s)
{
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - ('a' - 'A'));
        if (*s == '\\') *s = '/';
        s++;
    }
}

static int fs_name_is_demo(const char *path)
{
    const char *p = path ? path : "";
    if (strstr(p, ".dem") || strstr(p, ".DEM")) return 1;
    if (strstr(p, "drone") || strstr(p, "DRONE")) return 1;
    if (strstr(p, "demo") || strstr(p, "DEMO")) return 1;
    return 0;
}

static byte *fs_alloc_loose(int size, const char *path)
{
    byte *p;
    if (size <= 0) return NULL;
    p = (byte *)malloc((size_t)size + 1);
    if (p) return p;
    if (fs_name_is_demo(path) && size + 1 <= (int)sizeof s_demo_store) {
        uart_puts_raw("[FS] demo static store (malloc missed)\n");
        return s_demo_store;
    }
    return NULL;
}

static byte *fs_load_stdio(const char *path, int *out_len)
{
    FILE *f;
    int size;
    byte *buf;
    char syspath[128];
    const char *base;

    if (!path || !path[0]) return NULL;
    f = fopen(path, "rb");
    if (!f) {
        snprintf(syspath, sizeof(syspath), "/SYS/%s", path);
        f = fopen(syspath, "rb");
    }
    if (!f) {
        base = strrchr(path, '/');
        base = base ? base + 1 : path;
        snprintf(syspath, sizeof(syspath), "/SYS/%s", base);
        f = fopen(syspath, "rb");
        if (!f && !strstr(base, ".dem") && !strstr(base, ".DEM")) {
            snprintf(syspath, sizeof(syspath), "/SYS/%s.dem", base);
            f = fopen(syspath, "rb");
        }
    }
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = (int)ftell(f);
    if (size <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = fs_alloc_loose(size, path);
    if (!buf) { fclose(f); return NULL; }
    if ((int)fread(buf, 1, (size_t)size, f) != size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[size] = 0;
    if (out_len) *out_len = size;
    return buf;
}

static int fs_pak_reopen(void)
{
    int fd;
    if (s_pak_fd >= 0) return 1;
    if (!s_pak_path_str || !s_pak_path_str[0]) return 0;
    fd = ps2_fio_open_cdrom(s_pak_path_str);
    if (fd < 0) {
        uart_puts_raw("[FS] FILEIO PAK reopen missed\n");
        return 0;
    }
    s_pak_fd = fd;
    uart_puts_raw("[FS] FILEIO PAK fd restored\n");
    return 1;
}

static byte *fs_load_fio_quake(const char *path, int *out_len)
{
    const char *base;
    char name[64], rel[80];
    int fd, size, n, k, parked;
    byte *buf;
    static const char *prefix[] = { "QUAKE/", "" };

    /* Loose ISO files are the drone tracks, not PAK members.  A .bsp
     * lives inside PAK0.PAK; opening QUAKE/E1M1.BSP on the disc is a miss. */
    if (!fs_name_is_demo(path)) return NULL;

    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!base[0]) return NULL;
    n = snprintf(name, sizeof(name), "%s", base);
    if (n < 0 || n >= (int)sizeof(name)) return NULL;
    fs_upcase(name);
    if (!strstr(name, ".DEM")) {
        n = snprintf(name, sizeof(name), "%s.DEM", name);
        if (n < 0 || n >= (int)sizeof(name)) return NULL;
    }

    /* QUAKE\DRONE1M1.DEM first (ISO dir), then the root copy.  A missing
     * backslash used to open cdrom0:QUAKEDRONE1M1.DEM;1.  IOP FILEIO
     * often holds one cdrom0 fd: if PAK0.PAK is still open, park it,
     * read the demo, restore the PAK fd. */
    fd = -1;
    parked = 0;
    for (k = 0; k < 2; k++) {
        n = snprintf(rel, sizeof(rel), "%s%s", prefix[k], name);
        if (n < 0 || n >= (int)sizeof(rel)) continue;
        fd = ps2_fio_open_cdrom(rel);
        if (fd >= 0) break;
    }
    if (fd < 0 && s_pak_fd >= 0) {
        uart_puts_raw("[FS] FILEIO demo: parking PAK fd\n");
        ps2_fio_close(s_pak_fd);
        s_pak_fd = -1;
        parked = 1;
        for (k = 0; k < 2; k++) {
            n = snprintf(rel, sizeof(rel), "%s%s", prefix[k], name);
            if (n < 0 || n >= (int)sizeof(rel)) continue;
            fd = ps2_fio_open_cdrom(rel);
            if (fd >= 0) break;
        }
    }
    if (fd < 0) {
        if (parked) (void)fs_pak_reopen();
        return NULL;
    }

    size = ps2_fio_lseek(fd, 0, PS2_FIO_SEEK_END);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        ps2_fio_close(fd);
        if (parked) (void)fs_pak_reopen();
        return NULL;
    }
    buf = fs_alloc_loose(size, path);
    if (!buf) {
        ps2_fio_close(fd);
        if (parked) (void)fs_pak_reopen();
        return NULL;
    }
    n = ps2_fio_read_at(fd, 0, buf, size);
    ps2_fio_close(fd);
    if (parked) (void)fs_pak_reopen();
    if (n != size) {
        if (buf != s_demo_store) free(buf);
        return NULL;
    }
    buf[size] = 0;
    if (out_len) *out_len = size;
    return buf;
}

static byte *fs_load_iso_quake(const char *path, int *out_len)
{
    const char *base;
    char iso_path[80];
    uint32_t lba = 0, size = 0;
    byte *buf;
    int n;

    buf = fs_load_fio_quake(path, out_len);
    if (buf) return buf;

    if (!ps2_cdvd_ready() && !ps2_cdvd_init()) return NULL;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!base[0]) return NULL;

    n = snprintf(iso_path, sizeof(iso_path), "QUAKE/%s", base);
    if (n < 0 || n >= (int)sizeof(iso_path)) return NULL;
    fs_upcase(iso_path + 6);
    if (!strstr(iso_path, ".DEM") && !strstr(iso_path, ".dem")) {
        n = snprintf(iso_path, sizeof(iso_path), "QUAKE/%s.DEM", base);
        if (n < 0 || n >= (int)sizeof(iso_path)) return NULL;
        fs_upcase(iso_path + 6);
    }

    if (!ps2_iso_find(ps2_cdvd_iso_read, 0, iso_path, &lba, &size)) {
        /* Root copy of the same 8.3 name (Makefile stamps both). */
        const char *leaf = strrchr(iso_path, '/');
        leaf = leaf ? leaf + 1 : iso_path;
        if (!leaf[0] || !ps2_iso_find(ps2_cdvd_iso_read, 0, leaf, &lba, &size))
            return NULL;
    }
    if (size == 0 || size > 4u * 1024u * 1024u) return NULL;
    buf = fs_alloc_loose((int)size, path);
    if (!buf) return NULL;
    if (!ps2_cdvd_read_bytes(lba, 0, size, buf)) {
        if (buf != s_demo_store) free(buf);
        return NULL;
    }
    buf[size] = 0;
    if (out_len) *out_len = (int)size;
    return buf;
}

static int fs_index_pak_fd(int fd, int pak_sz, const char *path)
{
    dpackheader_t hdr;
    packfile_t *dir;
    int num_entries;
    char msg[96];

    if (pak_sz < (int)sizeof(hdr)) return 0;
    if (ps2_fio_read_at(fd, 0, &hdr, (int)sizeof(hdr)) != (int)sizeof(hdr))
        return 0;
    if (memcmp(hdr.id, "PACK", 4) != 0) {
        uart_puts_raw("[FS] PAK0.PAK is not a PACK archive\n");
        return 0;
    }
    if (hdr.dirofs < 0 || hdr.dirlen <= 0) return 0;
    if (hdr.dirofs + hdr.dirlen > pak_sz) return 0;

    dir = (packfile_t *)malloc((size_t)hdr.dirlen);
    if (!dir) return 0;
    if (ps2_fio_read_at(fd, hdr.dirofs, dir, hdr.dirlen) != hdr.dirlen) {
        free(dir);
        return 0;
    }

    s_pak_image    = NULL;
    s_pak_size     = pak_sz;
    s_pak_lba      = 0;
    s_pak_fd       = fd;
    s_pak_path_str = path;
    num_entries    = hdr.dirlen / (int)sizeof(packfile_t);
    fs_index_dir(dir, num_entries, pak_sz);

    {
        char first[56];
        int i, e1;
        first[0] = 0;
        for (i = 0; i < 55 && dir[0].name[i]; i++)
            first[i] = dir[0].name[i];
        first[i] = 0;
        e1 = ht_find("maps/e1m1.bsp") ? 1 : 0;
        snprintf(msg, sizeof(msg),
                 "[FS] PAK fileio fd=%d size=%d dir=%d hashed=%d e1m1=%d first='%s'\n",
                 fd, pak_sz, num_entries, s_ht_hashed, e1, first);
        uart_puts_raw(msg);
        Con_Printf("%s", msg);
    }
    free(dir);
    if (!ht_find("maps/e1m1.bsp")) {
        Con_Printf("[FS] PAK directory did not contain maps/e1m1.bsp\n");
        s_pak_fd = -1;
        return 0;
    }
    return 1;
}

static int fs_init_fileio_pak(void)
{
    static const char *names[] = { "PAK0.PAK", "QUAKE/PAK0.PAK" };
    int fd, sz;
    unsigned i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        fd = ps2_fio_open_cdrom(names[i]);
        if (fd < 0) continue;
        sz = ps2_fio_lseek(fd, 0, PS2_FIO_SEEK_END);
        if (sz <= 0) {
            ps2_fio_close(fd);
            continue;
        }
        if (fs_index_pak_fd(fd, sz, names[i]))
            return 1;
        ps2_fio_close(fd);
    }
    return 0;
}

static int fs_init_cdvd_pak(void)
{
    uint32_t lba = 0, size = 0;
    dpackheader_t hdr;
    packfile_t *dir;
    int num_entries;
    char msg[96];
    int via_search = 0;

    if (!ps2_cdvd_init()) return 0;
    if (ps2_cdvd_search("\\PAK0.PAK;1", &lba, &size)
        || ps2_cdvd_search("\\QUAKE\\PAK0.PAK;1", &lba, &size)) {
        via_search = 1;
        Con_Printf("[FS] SearchFile PAK LBA=%u size=%u\n",
                   (unsigned)lba, (unsigned)size);
    } else if (!ps2_iso_find(ps2_cdvd_iso_read, 0, "PAK0.PAK", &lba, &size)
               && !ps2_iso_find(ps2_cdvd_iso_read, 0, "QUAKE/PAK0.PAK",
                                &lba, &size)) {
        Con_Printf("[FS] ISO has no PAK0.PAK\n");
        return 0;
    }
    if (size < sizeof(hdr)) return 0;
    if (!ps2_cdvd_read_bytes(lba, 0, sizeof(hdr), &hdr)) return 0;
    if (memcmp(hdr.id, "PACK", 4) != 0) {
        Con_Printf("[FS] PAK0.PAK is not a PACK archive\n");
        return 0;
    }
    if (hdr.dirofs < 0 || hdr.dirlen <= 0) return 0;
    if (hdr.dirofs + hdr.dirlen > (int)size) return 0;

    dir = (packfile_t *)malloc((size_t)hdr.dirlen);
    if (!dir) return 0;
    if (!ps2_cdvd_read_bytes(lba, (uint32_t)hdr.dirofs,
                             (uint32_t)hdr.dirlen, dir)) {
        free(dir);
        return 0;
    }

    s_pak_image    = NULL;          /* body stays on the disc */
    s_pak_size     = (int)size;
    s_pak_lba      = lba;
    s_pak_fd       = -1;
    s_pak_path_str = "cdvd:QUAKE/PAK0.PAK";
    num_entries    = hdr.dirlen / (int)sizeof(packfile_t);
    fs_index_dir(dir, num_entries, (int)size);
    free(dir);

    snprintf(msg, sizeof(msg),
             "[FS] PAK disc LBA=%u size=%u entries=%d hashed=%d search=%d\n",
             (unsigned)lba, (unsigned)size, num_entries, s_ht_hashed,
             via_search);
    uart_puts_raw(msg);
    Con_Printf("%s", msg);
    if (!ht_find("maps/e1m1.bsp")) {
        Con_Printf("[FS] CDVD PAK directory did not contain maps/e1m1.bsp\n");
        s_pak_lba = 0;
        s_pak_size = 0;
        s_ht_hashed = 0;
        memset(s_ht, 0, sizeof(s_ht));
        return 0;
    }
    return 1;
}
#endif /* BTRON_PS2_TARGET */

#if !defined(BTRON_PS2_TARGET)
void FS_DropBSP(void)
{
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
}
#endif

int FS_Init(void) {
    memset(s_ht, 0, sizeof(s_ht));
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
#if defined(BTRON_PS2_TARGET)
    if (s_pak_fd >= 0) {
        ps2_fio_close(s_pak_fd);
        s_pak_fd = -1;
    }
    /* Retail disc model: sceCdInit + DiskReady first so CDVDMAN
     * refreshes the ISO cache, then FILEIO, then SearchFile / NCMD.
     * Weak incbin is only for images built without an ISO. */
    Con_Printf("[FS] PS2 disc PAK index starting\n");
    if (!ps2_cdvd_init() && ps2_cdvd_disk_type() >= 0x10)
        Con_Printf("[FS] CDVDFSV handshake missed; FILEIO still tried\n");
    if (ps2_cdvd_disk_type() < 0x10) {
        uart_puts_raw(
            "[FS] GATE1 no disc. Boot btron-ps2.iso, not host: ELF.\n");
        Con_Printf("[FS] GATE1 no disc — PAK stays on the ISO\n");
    } else if (fs_init_fileio_pak()) {
        Con_Printf("[FS] FILEIO PAK indexed\n");
        return 1;
    } else {
        memset(s_ht, 0, sizeof(s_ht));
        s_ht_hashed = 0;
        Con_Printf("[FS] FILEIO PAK missed, trying CDVDFSV sectors\n");
        if (fs_init_cdvd_pak()) {
            Con_Printf("[FS] CDVDFSV PAK indexed\n");
            return 1;
        }
    }
    if (ps2_pak0_image && ps2_pak0_image_end > ps2_pak0_image) {
        int n = (int)(ps2_pak0_image_end - ps2_pak0_image);
        if (fs_map_pak_image(ps2_pak0_image, n, "ps2_pak0_image"))
            return 1;
    }
    Con_Printf("[FS] no PAK (FILEIO cdrom0:QUAKE/PAK0.PAK)\n");
#endif
    return 0;
}

const byte *FS_FindInPak(const char *p, int *l) {
    const fs_ht_slot_t *slot;
    if (!s_pak_image || !p) { if (l) *l = 0; return NULL; }
    slot = ht_find(p);
    if (!slot) { if (l) *l = 0; return NULL; }
    if (l) *l = slot->length;
    return s_pak_image + slot->offset;
}

byte *FS_LoadFile(const char *p, int *l) {
#if defined(BTRON_PS2_TARGET)
    const fs_ht_slot_t *slot;
    byte *buf;

    if (!p) { if (l) *l = 0; return NULL; }

    slot = ht_find(p);
    if (!slot && s_pak_fd >= 0) {
        char miss[96];
        snprintf(miss, sizeof(miss), "[FS] not in PAK: %s (hashed=%d)\n",
                 p, s_ht_hashed);
        uart_puts_raw(miss);
    }
    if (slot) {
        size_t need;
        size_t hole;
        char msg[120];
        int nread;

        if (s_pak_image) {
            if (l) *l = slot->length;
            return s_pak_image + slot->offset;
        }
        need = (size_t)slot->length + 1u;
        hole = ps2_heap_largest_hole();
        if (hole < need) {
            snprintf(msg, sizeof(msg),
                     "[FS] %s needs %d kB, hole %d kB\n",
                     p, (int)(need / 1024u), (int)(hole / 1024u));
            uart_puts_raw(msg);
            if (l) *l = 0;
            return NULL;
        }
        buf = (byte *)malloc(need);
        if (!buf) {
            snprintf(msg, sizeof(msg),
                     "[FS] %s malloc %d kB failed (hole %d kB)\n",
                     p, (int)(need / 1024u),
                     (int)(ps2_heap_largest_hole() / 1024u));
            uart_puts_raw(msg);
            ps2_heap_holders("bsp");
            if (l) *l = 0;
            return NULL;
        }
        if (s_pak_fd >= 0) {
            nread = ps2_fio_read_at(s_pak_fd, slot->offset, buf, slot->length);
            if (nread != slot->length) {
                uart_puts_raw("[FS] PAK FILEIO read missed, reopening\n");
                ps2_fio_close(s_pak_fd);
                s_pak_fd = -1;
                nread = -1;
                if (fs_pak_reopen())
                    nread = ps2_fio_read_at(s_pak_fd, slot->offset, buf,
                                            slot->length);
            }
            if (nread != slot->length) {
                uart_puts_raw("[FS] PAK FILEIO read failed\n");
                free(buf);
                if (l) *l = 0;
                return NULL;
            }
        } else if (!ps2_cdvd_read_bytes(s_pak_lba, (uint32_t)slot->offset,
                                        (uint32_t)slot->length, buf)) {
            free(buf);
            if (l) *l = 0;
            return NULL;
        }
        buf[slot->length] = 0;
        if (l) *l = slot->length;
        return buf;
    }

    /* Drone tracks are not in PAK0.PAK.  Try disc FILEIO/CDVD first so a
     * peak-heap fopen Imalloc(8 KiB) miss does not hide QUAKE/DRONE1M1.DEM.
     * /SYS (embedded volume) is the stdio fallback. */
    if (fs_name_is_demo(p)) {
        buf = fs_load_iso_quake(p, l);
        if (buf) return buf;
        buf = fs_load_stdio(p, l);
        if (buf) return buf;
    } else {
        buf = fs_load_stdio(p, l);
        if (buf) return buf;
        buf = fs_load_iso_quake(p, l);
        if (buf) return buf;
    }
    if (l) *l = 0;
    return NULL;
#else
    (void)p; if (l) *l = 0; return NULL;
#endif
}

/* Bind one BSP. A warp replaces it; the other 20 maps stay on media. */
int FS_CacheBSP(const char *bsp_path)
{
    int bsp_len = 0;
    byte *data;
    const dheader_t *hdr;
    int i;
#if defined(BTRON_PS2_TARGET)
    int from_heap = 0;
#endif

    if (!bsp_path || !bsp_path[0]) return 0;
    if (g_bsp_cache.is_ready && strcmp(g_bsp_cache.path, bsp_path) == 0)
        return 1;

#if defined(BTRON_PS2_TARGET)
    if (s_level_image) {
        free(s_level_image);
        s_level_image = NULL;
    }
#endif
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
    strncpy(g_bsp_cache.path, bsp_path, sizeof(g_bsp_cache.path) - 1);

#if defined(BTRON_PS2_TARGET)
    if (s_pak_image) {
        data = (byte *)FS_FindInPak(bsp_path, &bsp_len);
    } else {
        data = FS_LoadFile(bsp_path, &bsp_len);
        from_heap = 1;
    }
#else
    data = (byte *)FS_FindInPak(bsp_path, &bsp_len);
#endif
    if (!data || bsp_len < (int)sizeof(dheader_t)) {
#if defined(BTRON_PS2_TARGET)
        Con_Printf("FS_CacheBSP: %s load failed (hashed=%d lba=%u fd=%d)\n",
                   bsp_path, s_ht_hashed, (unsigned)s_pak_lba, s_pak_fd);
#else
        Con_DPrintf("FS_CacheBSP: %s not found\n", bsp_path);
#endif
        return 0;
    }

    hdr = (const dheader_t *)data;
    if (hdr->version != BSPVERSION) {
        Con_DPrintf("FS_CacheBSP: %s bad version %d\n", bsp_path, hdr->version);
#if defined(BTRON_PS2_TARGET)
        if (from_heap) free(data);
#endif
        return 0;
    }

#if defined(BTRON_PS2_TARGET)
    if (from_heap) s_level_image = data;
#endif
    g_bsp_cache.data   = data;
    g_bsp_cache.length = bsp_len;
    for (i = 0; i < HEADER_LUMPS; i++) {
        int ofs = hdr->lumps[i].fileofs;
        int len = hdr->lumps[i].filelen;
        if (ofs < 0 || len < 0 || ofs + len > bsp_len) {
            g_bsp_cache.lumps[i]     = NULL;
            g_bsp_cache.lump_lens[i] = 0;
        } else {
            g_bsp_cache.lumps[i]     = data + ofs;
            g_bsp_cache.lump_lens[i] = len;
        }
    }
    g_bsp_cache.is_ready = 1;
    Con_Printf("FS_CacheBSP: level %s (%d KB)\n", bsp_path, bsp_len / 1024);
    return 1;
}

#endif  /* !hosted / Pi */

void FS_FreeFile(void *buffer) {
    /* Hunk memory — no-op (NASA JPL Rule 3 arena) */
    (void)buffer;
}
