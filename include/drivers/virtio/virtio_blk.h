/*
 * virtio_blk.h — VirtIO-Block PCI Device Driver for B-System (BTRON 3.20)
 * Designed for QEMU q35 & PC Bare-metal Workstations (virtio-blk-pci).
 * NASA/JPL Rule 3 compliant (100% static ring memory, non-allocating).
 */

#ifndef BTRON_DRIVERS_VIRTIO_BLK_H
#define BTRON_DRIVERS_VIRTIO_BLK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <fs/block.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VirtIO PCI Identifiers for Block Device */
#define VIRTIO_PCI_VENDOR_ID            0x1AF4
#define VIRTIO_PCI_DEVICE_BLOCK         0x1042  /* Modern VirtIO 1.0 Block (0x1040 + 2) */
#define VIRTIO_PCI_DEVICE_LEGACY_BLOCK  0x1001  /* Legacy Transitional Block */
#define VIRTIO_SUBSYS_BLOCK             0x0002  /* Block Device Subsystem ID */

/* VirtIO Block Request Types */
#define VIRTIO_BLK_T_IN                 0   /* Read */
#define VIRTIO_BLK_T_OUT                1   /* Write */
#define VIRTIO_BLK_T_FLUSH              4   /* Flush cache */
#define VIRTIO_BLK_T_GET_ID             8   /* Device ID string */

/* VirtIO Block Status Codes */
#define VIRTIO_BLK_S_OK                 0
#define VIRTIO_BLK_S_IOERR              1
#define VIRTIO_BLK_S_UNSUPP             2

/* VirtIO Block Request Header (16 bytes) */
struct virtio_blk_req {
    uint32_t type;      /* VIRTIO_BLK_T_* */
    uint32_t reserved;  /* Priority / reserved */
    uint64_t sector;    /* 512-byte sector offset */
} __attribute__((packed));

/* VirtIO Block Configuration Structure in BAR MMIO */
struct virtio_blk_config {
    uint64_t capacity;  /* Total capacity in 512-byte sectors */
    uint32_t size_max;
    uint32_t seg_max;
    struct {
        uint16_t cylinders;
        uint8_t  heads;
        uint8_t  sectors;
    } geometry;
    uint32_t blk_size;
    uint8_t  physical_block_exp;
    uint8_t  alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    uint8_t  wce;
    uint8_t  unused;
    uint16_t num_queues;
} __attribute__((packed));

#define VIRTIO_BLK_MAX_DEVICES          4
#define VIRTIO_BLK_QUEUE_SIZE           32

/* Per-Device State */
typedef struct {
    bool        is_detected;
    bool        is_initialized;
    uint8_t     pci_bus;
    uint8_t     pci_slot;
    uint8_t     pci_func;
    uint16_t    device_id;
    uint64_t    capacity_sectors; /* in 512-byte units */

    volatile void *common_cfg;
    volatile void *device_cfg;
    volatile uint16_t *notify_addr;

    uintptr_t   notify_bar_base;
    uint32_t    notify_offset;
    uint32_t    notify_off_mult;

    uint16_t    last_used_idx;
} virtio_blk_pci_t;

/* Driver API */
int         virtio_blk_pci_probe_all(void);
int         virtio_blk_pci_init(int dev_idx);
int         virtio_blk_read_sectors(int dev_idx, uint64_t sector, void *buf, uint32_t count);
int         virtio_blk_write_sectors(int dev_idx, uint64_t sector, const void *buf, uint32_t count);
uint64_t    virtio_blk_get_capacity(int dev_idx);
int         virtio_blk_get_device_count(void);
const virtio_blk_pci_t *virtio_blk_get_device(int dev_idx);

/* BlkDev Wrapper for BTRON Filesystem Layer */
BlkDev*     blk_virtio_create(int dev_idx);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_DRIVERS_VIRTIO_BLK_H */
