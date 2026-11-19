/*
 * B-System BTRON3 Filesystem — vol_api.h
 * Public Volume API: mount/format/sync + block backend constructors.
 *
 * Consumers:
 *   - src/cores/core_posix.c   (boot mount)
 *   - src/apps/clu_fs.c        (CLU commands)
 *   - src/tools/mkbtronfs.c    (host tool)
 *   - tests/test_fs.c          (tests)
 *
 * The opaque Volume struct is defined in src/fs/vol.c.
 * All CLU operations use g_sys_vol (the /SYS mount).
 */

#ifndef _BTRON_FS_VOL_API_H_
#define _BTRON_FS_VOL_API_H_

#include <fs/block.h>
#include <fs/fs_types.h>
#include <btron/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of opaque Volume handle is in fs_types.h */

/* ── Global /SYS volume ────────────────────────────────────────── */
/*
 * g_sys_vol is set at boot by core_posix / core_boot after mounting
 * btron_sys.vol (or a freshly formatted RAM disk).
 * All CLU builtins in clu_fs.c operate on g_sys_vol.
 */
extern Volume *g_sys_vol;
extern Volume *g_anders_vol;
extern Volume *g_chokanji_vol;

/*
 * g_cwd_path — current working path (UTF-8), updated by clu_cd.
 * Format: "/SYS" or "/SYS/SomeDirectory".  Used for the gterm prompt.
 */
extern char g_cwd_path[128];

/* ── Volume lifecycle ──────────────────────────────────────────── */

/*
 * vol_format — write an empty BTRON volume to `dev`.
 *   nfmax  : maximum number of files (FIDs); determines FID table size.
 *   nlb    : total logical blocks (device must be at least nlb blocks).
 *   name   : UTF-8 volume name (also used as root file name, FID 0).
 * Returns 0 on success, negative on error.
 * After vol_format the device holds a valid, clean volume.
 */
int vol_format(BlkDev *dev, UW nfmax, UW nlb, const char *name);

/*
 * vol_mount — open a previously formatted volume for use.
 *   dev    : block device holding the volume image.
 * Validates the magic number, loads FID table + bitmaps into RAM,
 * sets dirty=1 in the header (marks it as mounted), and returns an
 * opaque Volume handle.  Returns NULL if the magic is wrong.
 */
Volume *vol_mount(BlkDev *dev);

/*
 * vol_umount — flush and close a volume.
 * Calls vol_sync(), sets dirty=0, frees in-RAM structures.
 * After this call `v` is invalid.
 */
void vol_umount(Volume *v);

/*
 * Dynamic mounted volume registry: query any mounted volume dynamically by index or name.
 */
int     vol_mounted_count(void);
Volume *vol_get_mounted(int index);
Volume *vol_find_by_name(const char *name);
Volume *vol_find_by_prefix(const char *path, const char **out_suffix);

/*
 * vol_sync — flush all dirty in-RAM state to the block device.
 * Writes back: FID table blocks, bitmap blocks, header block.
 * Safe to call multiple times.
 */
void vol_sync(Volume *v);

/*
 * vol_set_batch_mode — defer metadata flushes during bulk replication.
 * When disabled (enable == 0), performs a single batched vol_sync(v).
 */
void vol_set_batch_mode(Volume *v, int enable);

/*
 * blk_mount_disk_partitions — Mount all BTRON partitions on a 512-byte sector
 * MBR or Protective GPT disk device. If preferred_slot >= 0, mounts that slot
 * first as g_sys_vol. Assigns "SYS" -> g_sys_vol, "ANDERS" -> g_anders_vol,
 * "CHOKANJI" -> g_chokanji_vol, and registers all volumes in vol.c's registry.
 * Returns mounted volume count (>= 1) on success, or negative ER code on failure.
 */
ER blk_mount_disk_partitions(BlkDev *disk_512, int preferred_slot);

/* ── Volume statistics ─────────────────────────────────────────── */

/*
 * vol_free_blocks — return current free-block count.
 */
UW vol_free_blocks(const Volume *v);

/*
 * vol_total_blocks — return total block count (nlb).
 */
UW vol_total_blocks(const Volume *v);

/*
 * vol_nfmax — return max FID count.
 */
UW vol_nfmax(const Volume *v);

/*
 * vol_name — return pointer to the UTF-8 volume name (NUL-terminated).
 */
const char *vol_name(const Volume *v);

/*
 * vol_block_size — return logical block size in bytes (e.g. 1024 or 8192).
 */
UW vol_block_size(const Volume *v);

/*
 * vol_fs_type — return filesystem type identifier (e.g. FS_TYPE_STD, FS_TYPE_BRIGHTV).
 */
UH vol_fs_type(const Volume *v);

/*
 * vol_mount_seq — return this mount's sequence number, unique for the process
 * even when a freed Volume's address is reused. Caches of derived state keyed by
 * the pointer use it to recognise a stale snapshot.
 */
UW vol_mount_seq(const Volume *v);

/*
 * vol_is_le — return 1 if volume layout is little-endian, 0 if big-endian.
 */
int vol_is_le(const Volume *v);

/*
 * vol_description — returns descriptive name of the filesystem format.
 */
const char *vol_description(const Volume *v);

/* TRON Code <-> UTF-8 transcoding */
void btr_tcode_to_utf8(const UH *tc, int max_tcs, char *utf8, int max_bytes);
void btr_utf8_to_tcode(const char *utf8, UH *tc, int max_tcs);

/* ── Internal helpers (used by file.c; not for application code) ── */
BLK  vol_alloc_block(Volume *v);
BLK  vol_alloc_blocks_contig(Volume *v, UW count);
int  vol_alloc_block_pair(Volume *v, BLK *hdr, BLK *data);
void vol_free_block(Volume *v, BLK blk);
FID  vol_fid_alloc(Volume *v);
void vol_fid_free(Volume *v, FID fid);
BLK  vol_fid_get_blk(const Volume *v, FID fid);
void vol_fid_set(Volume *v, FID fid, BLK blk, UB refcount);
UB   vol_fid_refcount(const Volume *v, FID fid);
void vol_fid_ref_inc(Volume *v, FID fid);
void vol_fid_ref_dec(Volume *v, FID fid);
UW   vol_name_hash(const char *name);
void vol_hash_set(Volume *v, FID fid, UW hash);
UW   vol_hash_get(const Volume *v, FID fid);
int  vol_read_blk(Volume *v, BLK lba, void *buf);
int  vol_write_blk(Volume *v, BLK lba, const void *buf);
ER   vol_read_blks(Volume *v, BLK lba, void *buf, UW count);
ER   vol_write_blks(Volume *v, BLK lba, const void *buf, UW count);
void vol_mark_dirty(Volume *v);

/* ── Production Cross-Volume Real Body Copy & VOBJ Link API ─────── */
/*
 * fs_copy_file_vol — Copy a Real Body (FID) from src_vol to dst_vol at production
 * speed using contiguous extent allocation and multi-block batch I/O (16-32 KiB).
 * Preserves FileHeader metadata (flags, app_type, timestamps, nrec, RecordIndex
 * types). If dst_name is NULL, preserves the source file's name.
 * Rolls back destination FID and blocks atomically on any error.
 * Returns E_OK or a normative BTRON ER code (ER_NODSK, ER_FS_FRAG, ER_FS_NOFID, ER_IO, ...).
 */
ER fs_copy_file_vol(Volume *src_vol, FID src_fid, Volume *dst_vol,
                    const char *dst_name, FID *out_dst_fid, UW *out_bytes_copied);

/*
 * fs_add_vobj_link_vol — Append a Virtual Object (VOBJ) link record to parent_fid
 * on dst_vol pointing to child_fid with name child_name.
 */
ER fs_add_vobj_link_vol(Volume *dst_vol, FID parent_fid, FID child_fid, const char *child_name);

/*
 * cpy_fil — Path-level file copy wrapper (supports cross-volume paths like
 * "/SYS/Apps/Editor" -> "/ANDERS/Editor").
 */
ER cpy_fil(const char *src_path, const char *dst_path);

/* ── File-level helpers exposed for host tools ─────────────────────── */
/*
 * fil_set_rec_type — set the RecordIndex.type for a given open fd + record.
 * Used by mkbtronfs to mark RT_TADDATA after ins_rec().
 */
void fil_set_rec_type(ID fd, W rec_idx, UH type);

/* The timestamp a freshly written file header carries, in seconds since 2000-01-01.  One
 * reading, because two writers (vol_format's root header and file.c's create/close path) used to
 * call time(NULL) themselves, which made them agree by luck.
 *
 * btron_fs_set_stamp() pins that reading, and it exists for the host tool: mkbtronfs writes a
 * volume that is then `.incbin`'d into the PS2 image, so a build that stamps "now" produces a
 * different ELF for identical sources -- and the row that checks the image carries the same bytes
 * as the file on disk (scripts/test_ps2.sh, "they are the bytes mkbtronfs wrote") goes red one
 * generation late, comparing yesterday's stamp against today's.  Pinning the newest input's own
 * mtime makes the volume a function of its contents, which is what a shipped payload has to be.
 * Nothing on a target calls it, so the runtime keeps the clock. */
UW   btron_fs_now_stamp(void);
void btron_fs_set_stamp(UW secs_since_2000);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_VOL_API_H_ */
