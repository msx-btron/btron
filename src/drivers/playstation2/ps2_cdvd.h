/*
 * ps2_cdvd.h — Emotion Engine CDVD sector reader
 *
 * Retail path is CDVDFSV:
 *   sceCdInit       RPC 0x80000592
 *   sceCdDiskReady  RPC 0x8000059A
 *   sceCdGetDiskType SCMD 3  (0 = empty tray / host ELF)
 *   sceCdMmode      SCMD 34
 *   sceCdSearchFile RPC 0x80000597
 *   sceCdRead       RPC 0x80000595 NCMD READ
 * EE MMIO at 0xBF402000 is IOP-private and last-resort only.
 *
 * The 18.6 MiB PAK stays on the disc.  RAM holds one file range.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_CDVD_H
#define PS2_CDVD_H

#include <stdint.h>
#include "ps2_iso9660.h"

/* Probe the drive, wait for spin-up, record the disc type.
 * Returns 1 when a disc is present and NSTATUS is ready. */
int ps2_cdvd_init(void);

uint8_t ps2_cdvd_disk_type(void);
int     ps2_cdvd_ready(void);

/* CDVDFSV SearchFile (sceCdSearchFile).  `path` is ISO style with a
 * leading backslash, e.g. "\\PAK0.PAK;1".  Returns 1 and writes LSN
 * (ISO LBA) and byte size. */
int ps2_cdvd_search(const char *path, uint32_t *out_lsn, uint32_t *out_size);

/* Read `nsectors` 2048-byte sectors at `lba` into EE RAM `dst`. */
int ps2_cdvd_read_sectors(uint32_t lba, uint32_t nsectors, void *dst);

/* Read `len` bytes from a file that begins at `start_lba`, starting
 * `offset` bytes into that file.  Chunks of 16 sectors through IOP RAM. */
int ps2_cdvd_read_bytes(uint32_t start_lba, uint32_t offset,
                        uint32_t len, void *dst);

/* ps2_iso_read_fn adapter (ctx unused). */
int ps2_cdvd_iso_read(void *ctx, uint32_t lba, uint32_t nsectors, void *dst);

#endif /* PS2_CDVD_H */
