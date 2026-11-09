/*
 * ps2_fileio.c — Cleanroom IOP FILEIO (RPC 0x80000001)
 *
 * Argument layouts match fileio-common.h / io_common.h.  16 KiB is the
 * IOP FILEIO transfer size; larger reads are chunked.  Unaligned tails
 * arrive in the remnant struct the server DMAs back on RPC_END.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_fileio.h"
#include "ps2_sif.h"
#include "ps2_console.h"

#include <libstr.h>
#include <stdio.h>

#define FIO_RPC_SID    0x80000001u
#define FIO_F_OPEN     0
#define FIO_F_CLOSE    1
#define FIO_F_READ     2
#define FIO_F_LSEEK    4
#define FIO_PATH_MAX   256
#define FIO_UNCACHED   0x20000000u

struct fio_open_arg {
    int32_t mode;
    char    name[FIO_PATH_MAX];
} __attribute__((aligned(16)));

struct fio_read_arg {
    int32_t fd;
    void   *ptr;
    int32_t size;
    void   *read_data;
} __attribute__((aligned(16)));

struct fio_lseek_arg {
    union { int32_t fd; int32_t result; } p;
    int32_t offset;
    int32_t whence;
} __attribute__((aligned(16)));

struct fio_read_data {
    uint32_t size1;
    uint32_t size2;
    void    *dest1;
    void    *dest2;
    uint8_t  buf1[16];
    uint8_t  buf2[16];
} __attribute__((aligned(64)));

static ps2_sif_rpc_t          s_fio;
static int                    s_ready;
static int32_t                s_recv[16] __attribute__((aligned(64)));
static struct fio_open_arg    s_open     __attribute__((aligned(16)));
static struct fio_read_arg    s_rarg     __attribute__((aligned(16)));
static struct fio_lseek_arg   s_lseek    __attribute__((aligned(16)));
static int32_t                s_closefd  __attribute__((aligned(16)));
static struct fio_read_data   s_remnant  __attribute__((aligned(64)));
/* IOP DMAs into this, then we copy through the uncached alias so the
 * EE dcache cannot keep a stale view of a PACK directory or BSP. */
static uint8_t                s_xfer[PS2_FIO_XFER] __attribute__((aligned(64)));

static int32_t recv_i32(void)
{
    volatile int32_t *p =
        (volatile int32_t *)((uintptr_t)s_recv | FIO_UNCACHED);
    return p[0];
}

static void uart_esc_path(const char *tag, const char *path, int fd)
{
    char esc[80];
    char msg[112];
    int i, j = 0;

    for (i = 0; path[i] && j < 70; i++) {
        if (path[i] == '\\') {
            esc[j++] = '\\';
            if (j < 70) esc[j++] = '\\';
        } else {
            esc[j++] = path[i];
        }
    }
    esc[j] = 0;
    snprintf(msg, sizeof(msg), "%s %s fd=%d\n", tag, esc, fd);
    uart_puts_raw(msg);
}

int ps2_fio_init(void)
{
    int fd;

    if (s_ready) return 1;
    if (!ps2_sif_init()) return 0;
    if (!ps2_sif_bind(FIO_RPC_SID, &s_fio)) return 0;
    s_ready = 1;
    uart_puts_raw("[FILEIO] bound 0x80000001\n");
    fd = ps2_fio_open_cdrom("SYSTEM.CNF");
    if (fd >= 0) {
        uart_puts_raw("[FILEIO] root SYSTEM.CNF ok (SearchFile lives)\n");
        ps2_fio_close(fd);
    } else {
        uart_puts_raw("[FILEIO] root SYSTEM.CNF missed (SearchFile)\n");
    }
    return 1;
}

int ps2_fio_ready(void)
{
    return s_ready;
}

int ps2_fio_open(const char *path, int mode)
{
    int32_t fd;
    size_t n;

    if (!path || !path[0]) return -1;
    if (!s_ready && !ps2_fio_init()) return -1;

    tkl_memset(&s_open, 0, sizeof(s_open));
    s_open.mode = mode;
    n = tkl_strlen(path);
    if (n >= FIO_PATH_MAX) n = FIO_PATH_MAX - 1;
    tkl_memcpy(s_open.name, path, n);
    s_open.name[n] = 0;
    tkl_memset(s_recv, 0, sizeof(s_recv));

    if (!ps2_sif_call(&s_fio, FIO_F_OPEN,
                      &s_open, (int)sizeof(s_open),
                      s_recv, 4, 0, 0))
        return -1;
    fd = recv_i32();
    return (fd >= 0) ? (int)fd : -1;
}

int ps2_fio_close(int fd)
{
    if (fd < 0) return -1;
    if (!s_ready) return -1;
    s_closefd = fd;
    tkl_memset(s_recv, 0, sizeof(s_recv));
    if (!ps2_sif_call(&s_fio, FIO_F_CLOSE,
                      &s_closefd, 4, s_recv, 4, 0, 0))
        return -1;
    return (int)recv_i32();
}

int ps2_fio_read(int fd, void *buf, int n)
{
    int32_t got;
    volatile uint8_t *uc;
    struct fio_read_data *u;
    intptr_t off;

    if (fd < 0 || !buf || n <= 0) return -1;
    if (!s_ready) return -1;
    if (n > PS2_FIO_XFER) n = PS2_FIO_XFER;

    tkl_memset(&s_rarg, 0, sizeof(s_rarg));
    tkl_memset(&s_remnant, 0, sizeof(s_remnant));
    s_rarg.fd        = fd;
    s_rarg.ptr       = s_xfer;
    s_rarg.size      = n;
    s_rarg.read_data = &s_remnant;
    tkl_memset(s_recv, 0, sizeof(s_recv));
    ps2_sif_flush_dcache();

    if (!ps2_sif_call(&s_fio, FIO_F_READ,
                      &s_rarg, (int)sizeof(s_rarg),
                      s_recv, 4, 0, 0))
        return -1;
    got = recv_i32();
    if (got > n) got = n;
    if (got > 0) {
        /* DMA body: uncached alias, never write back s_xfer after IOP. */
        uc = (volatile uint8_t *)((uintptr_t)s_xfer | FIO_UNCACHED);
        tkl_memcpy(buf, (const void *)uc, (size_t)got);
        /* Unaligned head/tail: IOP parked them in the remnant struct. */
        u = (struct fio_read_data *)((uintptr_t)&s_remnant | FIO_UNCACHED);
        if (u->size1 && u->dest1) {
            off = (uint8_t *)u->dest1 - s_xfer;
            if (off >= 0 && off + (intptr_t)u->size1 <= got)
                tkl_memcpy((uint8_t *)buf + off, u->buf1, u->size1);
        }
        if (u->size2 && u->dest2) {
            off = (uint8_t *)u->dest2 - s_xfer;
            if (off >= 0 && off + (intptr_t)u->size2 <= got)
                tkl_memcpy((uint8_t *)buf + off, u->buf2, u->size2);
        }
    }
    return (int)got;
}

int ps2_fio_lseek(int fd, int off, int whence)
{
    int32_t pos;

    if (fd < 0) return -1;
    if (!s_ready) return -1;
    tkl_memset(&s_lseek, 0, sizeof(s_lseek));
    s_lseek.p.fd   = fd;
    s_lseek.offset = off;
    s_lseek.whence = whence;
    tkl_memset(s_recv, 0, sizeof(s_recv));
    if (!ps2_sif_call(&s_fio, FIO_F_LSEEK,
                      &s_lseek, (int)sizeof(s_lseek),
                      s_recv, 4, 0, 0))
        return -1;
    pos = recv_i32();
    return (int)pos;
}

int ps2_fio_read_at(int fd, int off, void *buf, int n)
{
    uint8_t *p = (uint8_t *)buf;
    int left = n;
    int pos;

    if (fd < 0 || !buf) return -1;
    if (n == 0) return 0;
    pos = ps2_fio_lseek(fd, off, PS2_FIO_SEEK_SET);
    if (pos < 0) return -1;

    while (left) {
        int chunk = left > PS2_FIO_XFER ? PS2_FIO_XFER : left;
        int got = ps2_fio_read(fd, p, chunk);
        if (got <= 0)
            return (n - left) > 0 ? (n - left) : got;
        p    += got;
        left -= got;
        if (got < chunk) break;
    }
    return n - left;
}

static void fio_upper(char *s)
{
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - ('a' - 'A'));
        s++;
    }
}

static void fio_to_backslash(char *s)
{
    while (*s) {
        if (*s == '/') *s = '\\';
        s++;
    }
}

int ps2_fio_open_cdrom(const char *rel)
{
    /* CdSearchFileInner requires a leading '\' after cdrom0:.  Slash
     * paths fail immediately.  Two tries: retail C and SYSTEM.CNF. */
    static const char *fmt[] = {
        "cdrom0:\\%s;1",
        "cdrom0:\\\\%s;1",
    };
    char path[FIO_PATH_MAX];
    char back[192];
    size_t n;
    unsigned i;
    int fd;

    if (!rel || !rel[0]) return -1;
    if (!s_ready && !ps2_fio_init()) return -1;

    n = tkl_strlen(rel);
    if (n >= sizeof(back) - 1) return -1;
    tkl_memcpy(back, rel, n + 1);
    fio_upper(back);
    fio_to_backslash(back);

    for (i = 0; i < sizeof(fmt) / sizeof(fmt[0]); i++) {
        snprintf(path, sizeof(path), fmt[i], back);
        fd = ps2_fio_open(path, PS2_FIO_RDONLY);
        if (fd >= 0) {
            uart_esc_path("[FILEIO] open", path, fd);
            return fd;
        }
        uart_esc_path("[FILEIO] miss", path, fd);
    }
    return -1;
}
