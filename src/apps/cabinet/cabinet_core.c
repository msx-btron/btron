/*
 * cabinet_core.c — Shared Headless B-System /SYS Real-Object Volume Store
 *
 * Mounts the real bundled B-System ROMFS image ("BROM" v1, built at compile
 * time by scripts/mk_picocalc_fs.py from authentic repository files) plus
 * the live in-memory /SYS/BOOT.LOG hardware diagnostics buffer.
 *
 * Zero hardcoded mock documents, zero libc, zero heap (NASA JPL Rule 3).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "cabinet_core.h"
#include "picocalc_fs_image.h"

#define BROM_FLAG_TAD      0x01u
#define BROM_FLAG_EDITABLE 0x02u

static char      s_rw_buf[CAB_RW_BUF_CAP];
static int       s_rw_used = 0;
static char      s_bootlog_buf[CAB_BOOTLOG_CAP];
static uint32_t  s_bootlog_len = 0;
static int       s_bootlog_idx = -1;
static CAB_ENTRY s_entries[CAB_MAX_FILES];
static int       s_count  = 0;
static int       s_inited = 0;

static void c_strlcpy(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0) return;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int c_strieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int cabinet_core_mount_image(const uint8_t *img, uint32_t img_size)
{
    uint32_t version, file_count, total_size, i;
    if (!img || img_size < 16u) return -1;
    if (img[0] != 'B' || img[1] != 'R' || img[2] != 'O' || img[3] != 'M') return -1;

    version    = rd_le32(img + 4u);
    file_count = rd_le32(img + 8u);
    total_size = rd_le32(img + 12u);
    if (version != 1u || total_size > img_size) return -1;
    if (16u + file_count * 36u > img_size) return -1;

    for (i = 0; i < file_count && s_count < CAB_MAX_FILES - 1; i++) {
        const uint8_t *de = img + 16u + i * 36u;
        uint32_t off   = rd_le32(de + 24u);
        uint32_t sz    = rd_le32(de + 28u);
        uint32_t flags = rd_le32(de + 32u);
        CAB_ENTRY *e;
        if (off + sz > img_size) continue;

        e = &s_entries[s_count++];
        c_strlcpy(e->name, CAB_NAME_MAX, (const char *)de);
        e->is_tad = (flags & BROM_FLAG_TAD) ? 1 : 0;

        if ((flags & BROM_FLAG_EDITABLE) && !s_rw_used) {
            uint32_t cp = sz < (CAB_RW_BUF_CAP - 1u) ? sz : (CAB_RW_BUF_CAP - 1u);
            uint32_t k;
            for (k = 0; k < cp; k++) s_rw_buf[k] = (char)img[off + k];
            s_rw_buf[cp] = '\0';
            e->ro_data = NULL;
            e->rw_data = s_rw_buf;
            e->size    = cp;
            e->cap     = CAB_RW_BUF_CAP;
            s_rw_used  = 1;
        } else {
            e->ro_data = (const char *)(img + off);
            e->rw_data = NULL;
            e->size    = sz;
            e->cap     = sz;
        }
    }
    return 0;
}

static void mount_bootlog(void)
{
    CAB_ENTRY *e;
    if (s_count >= CAB_MAX_FILES) return;
    s_bootlog_idx = s_count++;
    e = &s_entries[s_bootlog_idx];
    c_strlcpy(e->name, CAB_NAME_MAX, "BOOT.LOG");
    e->ro_data = NULL;
    e->rw_data = s_bootlog_buf;
    e->size    = s_bootlog_len;
    e->cap     = CAB_BOOTLOG_CAP;
    e->is_tad  = 0;
    s_bootlog_buf[s_bootlog_len] = '\0';
}

void cabinet_core_init(void)
{
    if (s_inited) return;
    s_inited  = 1;
    s_count   = 0;
    s_rw_used = 0;
    s_bootlog_idx = -1;

    cabinet_core_mount_image(btron_picocalc_fs_img, (uint32_t)sizeof(btron_picocalc_fs_img));
    mount_bootlog();
}

void cabinet_bootlog_append(const char *s)
{
    if (!s) return;
    while (*s) {
        if (s_bootlog_len >= CAB_BOOTLOG_CAP - 1u) {
            /* Keep tail: shift second half down so newest hardware logs stay visible */
            uint32_t drop = CAB_BOOTLOG_CAP / 2u;
            uint32_t cur  = (s_bootlog_len < CAB_BOOTLOG_CAP) ? s_bootlog_len : (CAB_BOOTLOG_CAP - 1u);
            uint32_t rem  = (cur > drop) ? (cur - drop) : 0u;
            uint32_t i;
            for (i = 0; i < rem; i++) s_bootlog_buf[i] = s_bootlog_buf[drop + i];
            s_bootlog_len = rem;
        }
        s_bootlog_buf[s_bootlog_len++] = *s++;
    }
    s_bootlog_buf[s_bootlog_len] = '\0';
    if (s_bootlog_idx >= 0 && s_bootlog_idx < s_count) {
        s_entries[s_bootlog_idx].size = s_bootlog_len;
    }
}

void cabinet_bootlog_clear(void)
{
    s_bootlog_len = 0;
    s_bootlog_buf[0] = '\0';
    if (s_bootlog_idx >= 0 && s_bootlog_idx < s_count) {
        s_entries[s_bootlog_idx].size = 0;
    }
}

int cabinet_core_count(void)
{
    cabinet_core_init();
    return s_count;
}

const CAB_ENTRY *cabinet_core_get(int idx)
{
    cabinet_core_init();
    if (idx < 0 || idx >= s_count) return NULL;
    return &s_entries[idx];
}

int cabinet_core_find(const char *name)
{
    int i;
    cabinet_core_init();
    if (!name) return -1;
    for (i = 0; i < s_count; i++) {
        if (c_strieq(s_entries[i].name, name)) return i;
    }
    return -1;
}

const char *cabinet_core_read(int idx, uint32_t *out_size)
{
    const CAB_ENTRY *e = cabinet_core_get(idx);
    if (!e) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = e->size;
    return e->rw_data ? e->rw_data : e->ro_data;
}

int cabinet_core_write(const char *name, const char *data, uint32_t size)
{
    int idx = cabinet_core_find(name);
    CAB_ENTRY *e;
    uint32_t i;
    if (idx < 0) return -1;
    e = &s_entries[idx];
    if (!e->rw_data) return -2; /* Read-only Flash XIP file */
    if (size >= e->cap) size = e->cap - 1u;
    for (i = 0; i < size; i++) e->rw_data[i] = data[i];
    e->rw_data[size] = '\0';
    e->size = size;
    if (idx == s_bootlog_idx) s_bootlog_len = size;
    return 0;
}

uint32_t cabinet_core_used_bytes(void)
{
    uint32_t sum = 0;
    int i;
    cabinet_core_init();
    for (i = 0; i < s_count; i++) sum += s_entries[i].size;
    return sum;
}

uint32_t cabinet_core_total_bytes(void)
{
    return (uint32_t)sizeof(btron_picocalc_fs_img) + CAB_RW_BUF_CAP + CAB_BOOTLOG_CAP;
}
