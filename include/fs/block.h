/*
 * B-System BTRON3 Filesystem — block.h
 * Portable block device abstraction (BlkDev) and backend declarations.
 *
 * Per FS.md §11.  All FS code touches the volume only through BlkDev
 * read/write by logical block address (LBA).  The block_size is always
 * BTRON_BLOCK_SIZE (1024) for BTRON volumes.
 *
 * Error codes: 0 = success, negative = error (compatible with ER).
 */

#ifndef _BTRON_FS_BLOCK_H_
#define _BTRON_FS_BLOCK_H_

#include <fs/fs_types.h>
#include <btron/error.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BlkDev — portable block device handle.
 * All on-disk access goes through this interface.
 *
 *   read (ctx, lba, buf, count)  — read `count` blocks starting at `lba`
 *   write(ctx, lba, buf, count)  — write `count` blocks starting at `lba`
 *                                  NULL write means read-only (ROM memdisk)
 *   block_size  — bytes per block (1024 for BTRON logical blocks)
 *   nblocks     — total number of logical blocks on this device
 *   ctx         — opaque per-backend state pointer
 */
typedef struct BlkDev {
    int  (*read )(struct BlkDev *dev, BLK lba, void       *buf, UW count);
    int  (*write)(struct BlkDev *dev, BLK lba, const void *buf, UW count);
    UW   block_size;   /* always 1024 for BTRON                    */
    UW   nblocks;      /* total blocks on device                   */
    void *ctx;         /* backend private data                     */
} BlkDev;

/* ── Backend constructors ──────────────────────────────────────── */

/*
 * blk_mem_create — memdisk backend.
 *   buf      : raw memory holding the volume image (caller owns lifetime)
 *   nbytes   : byte size of the buffer (must be a multiple of 1024)
 *   readonly : non-zero → write callback is NULL (ROM image)
 * Returns a heap-allocated BlkDev, or NULL on error.
 * The caller frees with blk_destroy().
 */
BlkDev *blk_mem_create(void *buf, UW nbytes, int readonly);

/*
 * blk_file_create — POSIX file backend.
 *   path       : file path for the raw volume image
 *   create_new : if non-zero, create (truncate) the file to nblocks * 1024 bytes
 *   nblocks    : used only when create_new != 0; ignored on open of existing file
 * Returns a heap-allocated BlkDev, or NULL on error.
 * The caller frees with blk_destroy().
 */
BlkDev *blk_file_create(const char *path, int create_new, UW nblocks);

/*
 * blk_destroy — release a BlkDev and its backend resources.
 * Does NOT free the memory buffer for blk_mem; caller is responsible.
 */
void blk_destroy(BlkDev *dev);

/* Release a partition descriptor made by blk_partition_create / blk_mbr_find_* */
void blk_partition_release(BlkDev *dev);

/*
 * blk_file_close — close the FILE* and free a file-backed BlkDev.
 * Use instead of blk_destroy() for BlkDevs created by blk_file_create().
 */
void blk_file_close(BlkDev *dev);

/*
 * blk_qcow2_create — QEMU QCOW2 v3 image block backend (read-write).
 *   path      : file path to the .qcow2 image
 *   read_only : 1 for read-only, 0 for read-write
 * Returns a heap-allocated BlkDev with 512-byte sectors, or NULL on error.
 */
BlkDev *blk_qcow2_create(const char *path, int read_only);
void    blk_qcow2_close(BlkDev *dev);
int     blk_qcow2_create_file(const char *path, uint64_t disk_size);

/*
 * blk_partition_create — Partition / slice wrapper over a base BlkDev.
 *   parent     : underlying block device (e.g. disk or qcow2 image)
 *   start_lba  : starting sector/block on the parent device (in parent block units)
 *   nblocks    : length of partition in new block units
 *   block_size : block size for this partition (e.g. 1024 or 8192)
 * Returns a heap-allocated BlkDev, or NULL on error.
 */
BlkDev *blk_partition_create(BlkDev *parent, UW start_lba, UW nblocks, UW block_size);

/*
 * blk_is_btron_mbr_type — Returns true for all recognized BTRON MBR partition types:
 *   0x13 (Chokanji / B-right/V), 0x61 (B-FS V1), 0x62 (B-FS V2),
 *   0xB3..0xB5 (Signed B-FS), 0xE1..0xE4 (Encrypted B-FS).
 */
bool blk_is_btron_mbr_type(uint8_t part_type);

/*
 * blk_mbr_find_btron_partition — Scan MBR (or Protective GPT) on dev for BTRON partitions.
 * Accounts for direct raw volume (+0) or 8-sector VBR/IPL offset (+8),
 * using the specified filesystem logical block size (e.g. 8192 or 1024).
 * The _ex variants populate *out_err with a normative BTRON ER code (E_OK,
 * ER_PART_NOMBR, ER_PART_BADGPT, ER_PART_NOTFOUND, ER_PART_POOLFULL, ER_IO, E_PAR).
 */
BlkDev *blk_mbr_find_btron_partition(BlkDev *dev, UW fs_block_size);
BlkDev *blk_mbr_find_btron_partition_idx(BlkDev *dev, int part_idx, UW fs_block_size);
BlkDev *blk_mbr_find_btron_partition_ex(BlkDev *dev, int part_idx, UW fs_block_size, ER *out_err);
BlkDev *blk_gpt_find_btron_partition_idx(BlkDev *dev, int part_idx, UW fs_block_size);
BlkDev *blk_gpt_find_btron_partition_ex(BlkDev *dev, int part_idx, UW fs_block_size, ER *out_err);
BlkDev *blk_mbr_find_partition_by_slot(BlkDev *dev, int slot_idx, UW fs_block_size);
BlkDev *blk_mbr_find_partition_by_slot_ex(BlkDev *dev, int slot_idx, UW fs_block_size, ER *out_err);

/*
 * blk_probe_partition_label — Inspect a 512-byte sector partition start_lba for a
 * BTRON superblock magic (+0 or +8 sectors) and copy its VolumeHeader vol_name
 * into out_label (if non-NULL). Returns E_OK if valid BTRON magic found, else ER_FS_BADMAGIC / ER_IO.
 */
ER blk_probe_partition_label(BlkDev *dev, uint32_t start_lba, uint32_t total_sec,
                             char *out_label, int out_label_sz, uint32_t *out_fs_start_lba);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_BLOCK_H_ */
