/*
 * cabinet_core.h — Shared Headless B-System /SYS Real-Object Volume Store
 *
 * Zero-WND, zero-heap, NASA JPL Rule 3 compliant volume backing shared by:
 *   - Cabinet (lists files & byte sizes)
 *   - Editor  (loads & saves NOTE.TAD / user files)
 *   - Browser (reads .TAD / .TXT documents for tad_parser)
 *   - Terminal (`ls`, `cat`, `df` commands in shell_core)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_CABINET_CORE_H_
#define _BTRON_CABINET_CORE_H_

#include <stdint.h>
#include <stddef.h>

#define CAB_MAX_FILES      8
#define CAB_NAME_MAX      24
#define CAB_RW_BUF_CAP  3072
#define CAB_BOOTLOG_CAP 2048

typedef struct {
    char        name[CAB_NAME_MAX];
    const char *ro_data;     /* Flash XIP read-only content (0 B SRAM) */
    char       *rw_data;     /* Mutable SRAM buffer (or NULL if RO)    */
    uint32_t    size;
    uint32_t    cap;
    int         is_tad;
} CAB_ENTRY;

void             cabinet_core_init(void);
int              cabinet_core_mount_image(const uint8_t *img, uint32_t img_size);
int              cabinet_core_count(void);
const CAB_ENTRY *cabinet_core_get(int idx);
int              cabinet_core_find(const char *name);
const char      *cabinet_core_read(int idx, uint32_t *out_size);
int              cabinet_core_write(const char *name, const char *data, uint32_t size);
void             cabinet_bootlog_append(const char *s);
void             cabinet_bootlog_clear(void);
uint32_t         cabinet_core_used_bytes(void);
uint32_t         cabinet_core_total_bytes(void);

#endif /* _BTRON_CABINET_CORE_H_ */
