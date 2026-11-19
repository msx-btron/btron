/*
 * B-System BTRON3 Filesystem — blk_part.c
 * MBR & Protective GPT partition scanner and block device partition slicer.
 *
 * Scans MBR (all BTRON types: 0x13, 0x61, 0x62, 0xB3..0xB5, 0xE1..0xE4) and
 * Protective GPT (0xEE -> LBA 1 "EFI PART") for BTRON partitions, accounts for
 * direct raw volume (+0) or 8-sector IPL/VBR offset (+8), and wraps the
 * underlying device into a partition BlkDev with the requested logical block size.
 *
 * NASA/JPL Rule 3: Zero heap allocation on freestanding targets (static pool).
 */

#include <fs/block.h>
#include <fs/vol_api.h>
#include <btron/error.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <libstr.h>
#endif

typedef struct {
    BlkDev *parent;
    UW      start_lba; /* start in parent block units (sectors) */
    UW      sectors_per_block;
} PartCtx;

#if !(defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET))
#define MAX_PART_DESCS 16
static PartCtx s_part_ctx[MAX_PART_DESCS];
static BlkDev  s_part_dev[MAX_PART_DESCS];
static bool    s_part_in_use[MAX_PART_DESCS];
#endif

/* Canonical 16-byte mixed-endian GPT Type GUIDs for BTRON partitions */
static const uint8_t k_gpt_guid_chokanji[16] = {
    0x6F, 0x63, 0x6D, 0x70, 0x70, 0x72, 0x54, 0x42,
    0x52, 0x4E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};
static const uint8_t k_gpt_guid_bfs_v1[16] = {
    0x53, 0x46, 0x68, 0x8B, 0x54, 0x42, 0x00, 0x31,
    0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};
static const uint8_t k_gpt_guid_bfs_v2[16] = {
    0x53, 0x46, 0x68, 0x8B, 0x54, 0x42, 0x00, 0x32,
    0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};
/* Microsoft Basic Data GUID (accepted if superblock magic is valid BTRON) */
static const uint8_t k_gpt_guid_basic_data[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static inline void set_err(ER *out_err, ER code)
{
    if (out_err) *out_err = code;
}

static inline uint32_t rd_le32(const unsigned char *p)
{
    return (uint32_t)p[0] |
          ((uint32_t)p[1] <<  8) |
          ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

static inline uint64_t rd_le64(const unsigned char *p)
{
    return (uint64_t)rd_le32(p) | ((uint64_t)rd_le32(p + 4) << 32);
}

static inline bool is_btron_magic_u16(uint16_t m)
{
    return (m == 0x52FE || m == 0xFE52 ||   /* Chokanji / B-right/V */
            m == 0x42FE || m == 0xFE42 ||   /* B-FS V1 (STD)        */
            m == 0x62FE || m == 0xFE62);    /* B-FS V2 (Modern)     */
}

bool blk_is_btron_mbr_type(uint8_t part_type)
{
    if (part_type == 0x13 || part_type == 0x61 || part_type == 0x62)
        return true;
    if (part_type >= 0xB3 && part_type <= 0xB5)
        return true;
    if (part_type >= 0xE1 && part_type <= 0xE4)
        return true;
    return false;
}

static int part_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    }
    return 0;
}

static bool is_btron_gpt_guid(const unsigned char *guid16)
{
    if (part_memcmp(guid16, k_gpt_guid_chokanji, 16) == 0) return true;
    if (part_memcmp(guid16, k_gpt_guid_bfs_v1, 16) == 0)   return true;
    if (part_memcmp(guid16, k_gpt_guid_bfs_v2, 16) == 0)   return true;
    return false;
}

static bool is_guid_zero(const unsigned char *guid16)
{
    for (int i = 0; i < 16; i++) {
        if (guid16[i] != 0) return false;
    }
    return true;
}

static bool has_protective_mbr(const unsigned char *sec0)
{
    for (int i = 0; i < 4; i++) {
        if (sec0[446 + i * 16 + 4] == 0xEE) return true;
    }
    return false;
}

static int part_read(BlkDev *dev, BLK lba, void *buf, UW count)
{
    PartCtx *ctx = (PartCtx *)dev->ctx;
    if (lba + count > dev->nblocks) return ER_PAR;
    BLK parent_lba = ctx->start_lba + lba * ctx->sectors_per_block;
    UW  parent_count = count * ctx->sectors_per_block;
    return ctx->parent->read(ctx->parent, parent_lba, buf, parent_count);
}

static int part_write(BlkDev *dev, BLK lba, const void *buf, UW count)
{
    PartCtx *ctx = (PartCtx *)dev->ctx;
    if (!ctx->parent->write) return ER_RONLY;
    if (lba + count > dev->nblocks) return ER_PAR;
    BLK parent_lba = ctx->start_lba + lba * ctx->sectors_per_block;
    UW  parent_count = count * ctx->sectors_per_block;
    return ctx->parent->write(ctx->parent, parent_lba, buf, parent_count);
}

BlkDev *blk_partition_create(BlkDev *parent, UW start_lba, UW nblocks, UW block_size)
{
    if (!parent || block_size < parent->block_size || (block_size % parent->block_size) != 0)
        return NULL;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
    PartCtx *ctx = (PartCtx *)malloc(sizeof(PartCtx));
    BlkDev  *dev = (BlkDev  *)malloc(sizeof(BlkDev));
    if (!ctx || !dev) {
        free(ctx); free(dev);
        return NULL;
    }
#else
    int slot = -1;
    for (int i = 0; i < MAX_PART_DESCS; i++) {
        if (!s_part_in_use[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;
    s_part_in_use[slot] = true;
    PartCtx *ctx = &s_part_ctx[slot];
    BlkDev  *dev = &s_part_dev[slot];
#endif

    ctx->parent            = parent;
    ctx->start_lba         = start_lba;
    ctx->sectors_per_block = block_size / parent->block_size;

    dev->read       = part_read;
    dev->write      = parent->write ? part_write : NULL;
    dev->block_size = block_size;
    dev->nblocks    = nblocks;
    dev->ctx        = ctx;

    return dev;
}

static void resolve_fs_offset(BlkDev *dev, uint32_t start_lba, uint32_t total_sec,
                              uint32_t *out_fs_start_lba, uint32_t *out_fs_sectors,
                              bool require_magic_for_shift)
{
    uint32_t fs_start_lba = start_lba;
    uint32_t fs_sectors   = total_sec;
    unsigned char probe_sec[512];

    if (dev->read(dev, start_lba, probe_sec, 1) == 0) {
        uint16_t m = (uint16_t)probe_sec[0] | ((uint16_t)probe_sec[1] << 8);
        if (!is_btron_magic_u16(m) && total_sec > 8) {
            if (!require_magic_for_shift) {
                fs_start_lba = start_lba + 8;
                fs_sectors   = total_sec - 8;
            } else if (dev->read(dev, start_lba + 8, probe_sec, 1) == 0) {
                uint16_t m2 = (uint16_t)probe_sec[0] | ((uint16_t)probe_sec[1] << 8);
                if (is_btron_magic_u16(m2)) {
                    fs_start_lba = start_lba + 8;
                    fs_sectors   = total_sec - 8;
                }
            }
        }
    }

    *out_fs_start_lba = fs_start_lba;
    *out_fs_sectors   = fs_sectors;
}

ER blk_probe_partition_label(BlkDev *dev, uint32_t start_lba, uint32_t total_sec,
                             char *out_label, int out_label_sz, uint32_t *out_fs_start_lba)
{
    if (!dev || dev->block_size != 512 || total_sec == 0) return E_PAR;

    unsigned char probe_sec[512];
    uint32_t offsets[2] = { 0, 8 };
    int n_try = (total_sec > 8) ? 2 : 1;

    for (int i = 0; i < n_try; i++) {
        uint32_t lba = start_lba + offsets[i];
        if (dev->read(dev, lba, probe_sec, 1) != 0) return ER_IO;
        uint16_t m = (uint16_t)probe_sec[0] | ((uint16_t)probe_sec[1] << 8);
        if (is_btron_magic_u16(m)) {
            if (out_fs_start_lba) *out_fs_start_lba = lba;
            if (out_label && out_label_sz > 0) {
                /* VolumeHeader.vol_name is at offset 28 (0x1C, 40 bytes) in B-FS V1/V2 */
                int max_copy = out_label_sz - 1;
                if (max_copy > 39) max_copy = 39;
                int k = 0;
                for (; k < max_copy; k++) {
                    unsigned char c = probe_sec[28 + k];
                    if (c == 0) break;
                    out_label[k] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
                }
                out_label[k] = '\0';
            }
            return E_OK;
        }
    }

    return ER_FS_BADMAGIC;
}

BlkDev *blk_gpt_find_btron_partition_ex(BlkDev *dev, int target_idx, UW fs_block_size, ER *out_err)
{
    if (!dev || dev->block_size != 512 || target_idx < 0 || target_idx >= 128) {
        set_err(out_err, E_PAR);
        return NULL;
    }

    unsigned char sec[512];
    if (dev->read(dev, 1, sec, 1) != 0) {
        set_err(out_err, ER_IO);
        return NULL;
    }

    /* Verify "EFI PART" signature at LBA 1 */
    if (part_memcmp(sec, "EFI PART", 8) != 0) {
        set_err(out_err, ER_PART_BADGPT);
        return NULL;
    }

    uint64_t part_lba   = rd_le64(sec + 72);
    uint32_t num_parts  = rd_le32(sec + 80);
    uint32_t entry_size = rd_le32(sec + 84);

    if (entry_size < 128 || (entry_size % 128) != 0 || num_parts == 0) {
        set_err(out_err, ER_PART_BADGPT);
        return NULL;
    }
    if (num_parts > 128) num_parts = 128;

    uint32_t entries_per_sec = 512 / entry_size;
    if (entries_per_sec == 0) {
        set_err(out_err, ER_PART_BADGPT);
        return NULL;
    }

    int current_btron_found = 0;
    uint32_t total_sectors = (num_parts + entries_per_sec - 1) / entries_per_sec;

    for (uint32_t s = 0; s < total_sectors; s++) {
        if (dev->read(dev, (BLK)(part_lba + s), sec, 1) != 0) {
            set_err(out_err, ER_IO);
            return NULL;
        }
        for (uint32_t e = 0; e < entries_per_sec; e++) {
            uint32_t idx = s * entries_per_sec + e;
            if (idx >= num_parts) break;

            const unsigned char *ent = sec + e * entry_size;
            if (is_guid_zero(ent)) continue;

            uint64_t first_lba = rd_le64(ent + 32);
            uint64_t last_lba  = rd_le64(ent + 40);
            if (first_lba == 0 || last_lba < first_lba) continue;

            bool match = is_btron_gpt_guid(ent);
            if (!match && part_memcmp(ent, k_gpt_guid_basic_data, 16) == 0) {
                uint32_t total_sec = (uint32_t)(last_lba - first_lba + 1);
                if (blk_probe_partition_label(dev, (uint32_t)first_lba, total_sec, NULL, 0, NULL) == E_OK) {
                    match = true;
                }
            }

            if (match) {
                if (current_btron_found == target_idx) {
                    uint32_t start_lba = (uint32_t)first_lba;
                    uint32_t total_sec = (uint32_t)(last_lba - first_lba + 1);
                    uint32_t fs_start_lba = start_lba;
                    uint32_t fs_sectors   = total_sec;
                    resolve_fs_offset(dev, start_lba, total_sec, &fs_start_lba, &fs_sectors, false);

                    if (fs_block_size == 0) fs_block_size = 1024;
                    uint32_t spb = fs_block_size / 512;
                    uint32_t nblocks = fs_sectors / spb;
                    BlkDev *part = blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
                    set_err(out_err, part ? E_OK : ER_PART_POOLFULL);
                    return part;
                }
                current_btron_found++;
            }
        }
    }

    set_err(out_err, ER_PART_NOTFOUND);
    return NULL;
}

BlkDev *blk_gpt_find_btron_partition_idx(BlkDev *dev, int target_idx, UW fs_block_size)
{
    return blk_gpt_find_btron_partition_ex(dev, target_idx, fs_block_size, NULL);
}

BlkDev *blk_mbr_find_btron_partition_ex(BlkDev *dev, int target_idx, UW fs_block_size, ER *out_err)
{
    if (!dev || dev->block_size != 512 || target_idx < 0 || target_idx >= 128) {
        set_err(out_err, E_PAR);
        return NULL;
    }

    unsigned char sec0[512];
    if (dev->read(dev, 0, sec0, 1) != 0) {
        set_err(out_err, ER_IO);
        return NULL;
    }

    /* Verify MBR signature 0x55 0xAA */
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) {
        set_err(out_err, ER_PART_NOMBR);
        return NULL;
    }

    /* If Protective MBR (0xEE) is present, delegate automatically to GPT scanner */
    if (has_protective_mbr(sec0)) {
        return blk_gpt_find_btron_partition_ex(dev, target_idx, fs_block_size, out_err);
    }

    if (target_idx >= 4) {
        set_err(out_err, ER_PART_NOTFOUND);
        return NULL;
    }

    int current_btron_found = 0;

    /* Scan 4 primary MBR partition entries at offset 446 (0x1BE) */
    for (int i = 0; i < 4; i++) {
        const unsigned char *ent = sec0 + 446 + i * 16;
        uint8_t  part_type = ent[4];
        uint32_t start_lba = rd_le32(ent + 8);
        uint32_t total_sec = rd_le32(ent + 12);

        if (blk_is_btron_mbr_type(part_type) && total_sec > 0) {
            if (current_btron_found == target_idx) {
                uint32_t fs_start_lba = start_lba;
                uint32_t fs_sectors   = total_sec;
                resolve_fs_offset(dev, start_lba, total_sec, &fs_start_lba, &fs_sectors, false);

                if (fs_block_size == 0) fs_block_size = 1024;
                uint32_t spb = fs_block_size / 512;
                uint32_t nblocks = fs_sectors / spb;

                BlkDev *part = blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
                set_err(out_err, part ? E_OK : ER_PART_POOLFULL);
                return part;
            }
            current_btron_found++;
        }
    }

    set_err(out_err, ER_PART_NOTFOUND);
    return NULL;
}

BlkDev *blk_mbr_find_btron_partition_idx(BlkDev *dev, int target_idx, UW fs_block_size)
{
    return blk_mbr_find_btron_partition_ex(dev, target_idx, fs_block_size, NULL);
}

BlkDev *blk_mbr_find_btron_partition(BlkDev *dev, UW fs_block_size)
{
    return blk_mbr_find_btron_partition_ex(dev, 0, fs_block_size, NULL);
}

BlkDev *blk_mbr_find_partition_by_slot_ex(BlkDev *dev, int slot_idx, UW fs_block_size, ER *out_err)
{
    if (!dev || dev->block_size != 512 || slot_idx < 0 || slot_idx >= 128) {
        set_err(out_err, E_PAR);
        return NULL;
    }

    unsigned char sec0[512];
    if (dev->read(dev, 0, sec0, 1) != 0) {
        set_err(out_err, ER_IO);
        return NULL;
    }

    /* Verify MBR signature 0x55 0xAA */
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) {
        set_err(out_err, ER_PART_NOMBR);
        return NULL;
    }

    /* Support GPT slot indexing when Protective MBR is present */
    if (has_protective_mbr(sec0)) {
        unsigned char gpt_sec[512];
        if (dev->read(dev, 1, gpt_sec, 1) != 0) {
            set_err(out_err, ER_IO);
            return NULL;
        }
        if (part_memcmp(gpt_sec, "EFI PART", 8) != 0) {
            set_err(out_err, ER_PART_BADGPT);
            return NULL;
        }
        uint64_t part_lba   = rd_le64(gpt_sec + 72);
        uint32_t num_parts  = rd_le32(gpt_sec + 80);
        uint32_t entry_size = rd_le32(gpt_sec + 84);
        if (entry_size < 128 || (uint32_t)slot_idx >= num_parts) {
            set_err(out_err, ER_PART_NOTFOUND);
            return NULL;
        }
        uint32_t eps = 512 / entry_size;
        if (eps == 0 || dev->read(dev, (BLK)(part_lba + (uint32_t)slot_idx / eps), gpt_sec, 1) != 0) {
            set_err(out_err, ER_IO);
            return NULL;
        }
        const unsigned char *ent = gpt_sec + ((uint32_t)slot_idx % eps) * entry_size;
        if (is_guid_zero(ent)) {
            set_err(out_err, ER_PART_NOTFOUND);
            return NULL;
        }
        uint64_t first_lba = rd_le64(ent + 32);
        uint64_t last_lba  = rd_le64(ent + 40);
        if (first_lba == 0 || last_lba < first_lba) {
            set_err(out_err, ER_PART_NOTFOUND);
            return NULL;
        }
        uint32_t start_lba = (uint32_t)first_lba;
        uint32_t total_sec = (uint32_t)(last_lba - first_lba + 1);
        uint32_t fs_start_lba = start_lba;
        uint32_t fs_sectors   = total_sec;
        resolve_fs_offset(dev, start_lba, total_sec, &fs_start_lba, &fs_sectors, true);
        if (fs_block_size == 0) fs_block_size = 1024;
        uint32_t nblocks = fs_sectors / (fs_block_size / 512);
        BlkDev *part = blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
        set_err(out_err, part ? E_OK : ER_PART_POOLFULL);
        return part;
    }

    if (slot_idx >= 4) {
        set_err(out_err, ER_PART_NOTFOUND);
        return NULL;
    }

    const unsigned char *ent = sec0 + 446 + slot_idx * 16;
    uint32_t start_lba = rd_le32(ent + 8);
    uint32_t total_sec = rd_le32(ent + 12);

    if (total_sec == 0) {
        set_err(out_err, ER_PART_NOTFOUND);
        return NULL;
    }

    uint32_t fs_start_lba = start_lba;
    uint32_t fs_sectors   = total_sec;
    resolve_fs_offset(dev, start_lba, total_sec, &fs_start_lba, &fs_sectors, true);

    if (fs_block_size == 0) fs_block_size = 1024;
    uint32_t spb = fs_block_size / 512;
    uint32_t nblocks = fs_sectors / spb;

    BlkDev *part = blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
    set_err(out_err, part ? E_OK : ER_PART_POOLFULL);
    return part;
}

BlkDev *blk_mbr_find_partition_by_slot(BlkDev *dev, int slot_idx, UW fs_block_size)
{
    return blk_mbr_find_partition_by_slot_ex(dev, slot_idx, fs_block_size, NULL);
}

/* Release a descriptor from blk_partition_create (does not touch the parent device).
 * blk_destroy() must NOT be used on partitions in the freestanding kernel: the
 * descriptors live in a static pool, not on the heap. */
void blk_partition_release(BlkDev *dev)
{
    if (!dev) return;
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
    free(dev->ctx);
    free(dev);
#else
    for (int i = 0; i < MAX_PART_DESCS; i++) {
        if (dev == &s_part_dev[i]) { s_part_in_use[i] = false; return; }
    }
#endif
}

static int part_strcasecmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (char)(*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (char)(*s2 + 32) : *s2;
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

ER blk_mount_disk_partitions(BlkDev *disk_512, int preferred_slot)
{
    if (!disk_512 || disk_512->block_size != 512) return E_PAR;

    int mounted_count = 0;
    ER first_err = E_OK;

    if (preferred_slot >= 0) {
        ER er = E_OK;
        BlkDev *part = blk_mbr_find_partition_by_slot_ex(disk_512, preferred_slot, BTRON_BLOCK_SIZE, &er);
        if (!part) return er;
        Volume *v = vol_mount(part);
        if (!v) {
            blk_partition_release(part);
            return ER_FS_BADMAGIC;
        }
        g_sys_vol = v;
        mounted_count++;
    }

    for (int idx = 0; idx < 16; idx++) {
        ER er = E_OK;
        BlkDev *part = blk_mbr_find_btron_partition_idx(disk_512, idx, BTRON_BLOCK_SIZE);
        if (!part) {
            if (idx == 0 && mounted_count == 0) {
                (void)blk_mbr_find_btron_partition_ex(disk_512, 0, BTRON_BLOCK_SIZE, &er);
                first_err = er;
            }
            break;
        }

        Volume *v = vol_mount(part);
        if (!v) {
            blk_partition_release(part);
            continue;
        }

        const char *vn = vol_name(v);
        if (preferred_slot >= 0 && g_sys_vol && part_strcasecmp(vn, vol_name(g_sys_vol)) == 0 && idx == 0) {
            vol_umount(v);
            blk_partition_release(part);
            continue;
        }

        if (!g_sys_vol && (part_strcasecmp(vn, "SYS") == 0 || idx == 0)) {
            g_sys_vol = v;
        } else if (!g_anders_vol && part_strcasecmp(vn, "ANDERS") == 0) {
            g_anders_vol = v;
        } else if (!g_chokanji_vol && (part_strcasecmp(vn, "CHOKANJI") == 0 || part_strcasecmp(vn, "B-right") == 0)) {
            g_chokanji_vol = v;
        } else if (!g_anders_vol && g_sys_vol && idx == 1) {
            g_anders_vol = v;
        }

        mounted_count++;
    }

    if (mounted_count == 0) {
        return (first_err < 0) ? first_err : ER_PART_NOTFOUND;
    }
    return (ER)mounted_count;
}
