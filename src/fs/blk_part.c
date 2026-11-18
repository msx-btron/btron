/*
 * B-System BTRON3 Filesystem — blk_part.c
 * MBR partition scanner & block device partition slicer for B-System.
 *
 * Scans MBR for partition type 0x13 (Personal Media BTRON / B-right/V),
 * accounts for either direct raw volume or 8-sector IPL/bootloader area,
 * and wraps the underlying device into a partition BlkDev with the
 * appropriate filesystem block size.
 */

#include <fs/block.h>
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

static int part_read(BlkDev *dev, BLK lba, void *buf, UW count)
{
    PartCtx *ctx = (PartCtx *)dev->ctx;
    if (lba + count > dev->nblocks) return -1;
    BLK parent_lba = ctx->start_lba + lba * ctx->sectors_per_block;
    UW  parent_count = count * ctx->sectors_per_block;
    return ctx->parent->read(ctx->parent, parent_lba, buf, parent_count);
}

static int part_write(BlkDev *dev, BLK lba, const void *buf, UW count)
{
    PartCtx *ctx = (PartCtx *)dev->ctx;
    if (!ctx->parent->write) return -1;
    if (lba + count > dev->nblocks) return -1;
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

BlkDev *blk_mbr_find_btron_partition_idx(BlkDev *dev, int target_idx, UW fs_block_size)
{
    if (!dev || dev->block_size != 512 || target_idx < 0 || target_idx >= 4) return NULL;

    unsigned char sec0[512];
    if (dev->read(dev, 0, sec0, 1) != 0) return NULL;

    /* Verify MBR signature 0x55 0xAA */
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) return NULL;

    int current_btron_found = 0;

    /* Scan 4 partition entries at offset 446 (0x1BE) */
    for (int i = 0; i < 4; i++) {
        const unsigned char *ent = sec0 + 446 + i * 16;
        uint8_t  part_type = ent[4];
        uint32_t start_lba = (uint32_t)ent[8] |
                            ((uint32_t)ent[9]  <<  8) |
                            ((uint32_t)ent[10] << 16) |
                            ((uint32_t)ent[11] << 24);
        uint32_t total_sec = (uint32_t)ent[12] |
                            ((uint32_t)ent[13] <<  8) |
                            ((uint32_t)ent[14] << 16) |
                            ((uint32_t)ent[15] << 24);

        if (part_type == 0x13 && total_sec > 0) { /* 0x13 = BTRON partition */
            if (current_btron_found == target_idx) {
                uint32_t fs_start_lba = start_lba;
                uint32_t fs_sectors   = total_sec;

                unsigned char probe_sec[512];
                if (dev->read(dev, start_lba, probe_sec, 1) == 0) {
                    uint16_t m = (uint16_t)probe_sec[0] | ((uint16_t)probe_sec[1] << 8);
                    /* 0x52FE (LE) or 0x42FE / 0xFE52 / 0xFE42 (BE) */
                    if (m != 0x52FE && m != 0x42FE && m != 0xFE52 && m != 0xFE42) {
                        /* Check at start_lba + 8 for B-right/V */
                        if (total_sec > 8) {
                            fs_start_lba = start_lba + 8;
                            fs_sectors   = total_sec - 8;
                        }
                    }
                }

                if (fs_block_size == 0) fs_block_size = 1024;
                uint32_t spb = fs_block_size / 512;
                uint32_t nblocks = fs_sectors / spb;

                return blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
            }
            current_btron_found++;
        }
    }

    return NULL;
}

BlkDev *blk_mbr_find_btron_partition(BlkDev *dev, UW fs_block_size)
{
    return blk_mbr_find_btron_partition_idx(dev, 0, fs_block_size);
}

BlkDev *blk_mbr_find_partition_by_slot(BlkDev *dev, int slot_idx, UW fs_block_size)
{
    if (!dev || dev->block_size != 512 || slot_idx < 0 || slot_idx >= 4) return NULL;

    unsigned char sec0[512];
    if (dev->read(dev, 0, sec0, 1) != 0) return NULL;

    /* Verify MBR signature 0x55 0xAA */
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) return NULL;

    const unsigned char *ent = sec0 + 446 + slot_idx * 16;
    uint32_t start_lba = (uint32_t)ent[8] |
                        ((uint32_t)ent[9]  <<  8) |
                        ((uint32_t)ent[10] << 16) |
                        ((uint32_t)ent[11] << 24);
    uint32_t total_sec = (uint32_t)ent[12] |
                        ((uint32_t)ent[13] <<  8) |
                        ((uint32_t)ent[14] << 16) |
                        ((uint32_t)ent[15] << 24);

    if (total_sec == 0) return NULL;

    uint32_t fs_start_lba = start_lba;
    uint32_t fs_sectors   = total_sec;

    unsigned char probe_sec[512];
    if (dev->read(dev, start_lba, probe_sec, 1) == 0) {
        uint16_t m = (uint16_t)probe_sec[0] | ((uint16_t)probe_sec[1] << 8);
        if (m != 0x52FE && m != 0x42FE && m != 0xFE52 && m != 0xFE42) {
            if (total_sec > 8) {
                fs_start_lba = start_lba + 8;
                fs_sectors   = total_sec - 8;
            }
        }
    }

    if (fs_block_size == 0) fs_block_size = 1024;
    uint32_t spb = fs_block_size / 512;
    uint32_t nblocks = fs_sectors / spb;

    return blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
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
