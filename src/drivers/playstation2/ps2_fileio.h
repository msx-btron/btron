/*
 * ps2_fileio.h — IOP FILEIO client over SIF RPC 0x80000001
 *
 * Open/read/lseek/close of cdrom0: paths.  BOOT2 left FILEIO resident;
 * this is how the EE reads QUAKE/PAK0.PAK without touching CDVD MMIO.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_FILEIO_H
#define PS2_FILEIO_H

#include <stdint.h>

#define PS2_FIO_RDONLY   0x0001
#define PS2_FIO_SEEK_SET 0
#define PS2_FIO_SEEK_CUR 1
#define PS2_FIO_SEEK_END 2
#define PS2_FIO_XFER     (16 * 1024)

int  ps2_fio_init(void);
int  ps2_fio_ready(void);

/* Returns fd >= 0, or -1.  Path is sent as-is (cdrom0:\QUAKE\PAK0.PAK;1). */
int  ps2_fio_open(const char *path, int mode);
int  ps2_fio_close(int fd);
int  ps2_fio_read(int fd, void *buf, int n);
int  ps2_fio_lseek(int fd, int off, int whence);

/* SEEK_SET to `off`, then read `n` bytes in 16 KiB FILEIO chunks. */
int  ps2_fio_read_at(int fd, int off, void *buf, int n);

/* First successful open among canonical cdrom0: spellings of `rel`
 * (e.g. "QUAKE/PAK0.PAK"). */
int  ps2_fio_open_cdrom(const char *rel);

#endif /* PS2_FILEIO_H */
