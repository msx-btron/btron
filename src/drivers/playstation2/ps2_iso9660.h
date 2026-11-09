/*
 * ps2_iso9660.h — ISO9660 path finder (host-testable)
 *
 * Primary Volume Descriptor at sector 16, 2048-byte sectors.  The root
 * directory record lives at PVD offset 156 (it is not an LBA).  Names
 * match with or without a `;1` version suffix.
 *
 * The reader callback is the only I/O: the host test feeds a FILE *;
 * the Emotion Engine feeds CDVD ReadCd.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_ISO9660_H
#define PS2_ISO9660_H

#include <stdint.h>

#define PS2_ISO_SECTOR  2048u
#define PS2_ISO_PVD_LBA 16u

/* Read `nsectors` 2048-byte sectors starting at `lba` into `dst`.
 * `dst` holds nsectors * 2048 bytes.  Returns 1 on a full read. */
typedef int (*ps2_iso_read_fn)(void *ctx, uint32_t lba, uint32_t nsectors,
                               void *dst);

/* Locate `path` (e.g. "QUAKE/PAK0.PAK") from the PVD.  Returns 1 and
 * writes the file's starting LBA and byte size. */
int ps2_iso_find(ps2_iso_read_fn read, void *ctx, const char *path,
                 uint32_t *out_lba, uint32_t *out_size);

/* Copy `len` bytes from a file that begins at `file_lba`, starting
 * `file_off` bytes into that file.  One-sector reads; CDVD uses a
 * wider chunked path for the same arithmetic. */
int ps2_iso_read_range(ps2_iso_read_fn read, void *ctx,
                       uint32_t file_lba, uint32_t file_off,
                       uint32_t len, void *dst);

#endif /* PS2_ISO9660_H */
