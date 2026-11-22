/*
 * virtio_blk.c — VirtIO-Block PCI Device Driver for B-System (BTRON 3.20)
 * Cleanroom implementation for QEMU q35 & PC Bare-metal Workstations.
 * Fully supports modern virtio-blk-pci with synchronous VirtQueue 0 engine.
 * NASA/JPL Rule 3: 100% static non-allocating memory model.
 */

#include <drivers/virtio/virtio_blk.h>
#include <drivers/virtio/virtio_gpu.h>
#include <rt/libstr.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

extern void uart_puts_raw(const char *str);

/* ── Tiny formatters for serial logging ──────────────────────────────── */

static void vblk_uart_hex32(uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) { buf[i] = h[v & 0xF]; v >>= 4; }
    buf[10] = '\0';
    uart_puts_raw(buf);
}

static void vblk_uart_dec(uint32_t v) {
    char buf[12]; int i = 10; buf[11] = '\0';
    if (v == 0) { uart_puts_raw("0"); return; }
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + v % 10); v /= 10; }
    uart_puts_raw(buf + i + 1);
}

static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/* ── x86 PCI Configuration Port I/O ──────────────────────────────────── */

#if (defined(__x86_64__) || defined(__i386__)) && (defined(BTRON_UEFI_TARGET) || (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0))

static inline void outl_pci(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl_pci(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outl_pci(0x0CF8, addr);
    return inl_pci(0x0CFC);
}

static void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outl_pci(0x0CF8, addr);
    outl_pci(0x0CFC, val);
}

static uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, slot, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

static uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, slot, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

static uintptr_t pci_get_bar_base(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_idx) {
    uint8_t offset = (uint8_t)(0x10 + bar_idx * 4);
    uint32_t bar_low = pci_read_config32(bus, slot, func, offset);
    if (bar_low & 1) {
        return (uintptr_t)(bar_low & ~0x03U);
    }
    if ((bar_low & 0x06) == 0x04) {
        uint32_t bar_hi = pci_read_config32(bus, slot, func, offset + 4);
        return (uintptr_t)((bar_low & ~0x0FU) | ((uint64_t)bar_hi << 32));
    }
    return (uintptr_t)(bar_low & ~0x0FU);
}

#else

/* Mock PCI for hosted verification */
static uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    (void)bus; (void)slot; (void)func; (void)offset; return 0xFFFFFFFF;
}
static void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    (void)bus; (void)slot; (void)func; (void)offset; (void)val;
}
static uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    (void)bus; (void)slot; (void)func; (void)offset; return 0xFF;
}
static uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    (void)bus; (void)slot; (void)func; (void)offset; return 0xFFFF;
}
static uintptr_t pci_get_bar_base(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_idx) {
    (void)bus; (void)slot; (void)func; (void)bar_idx; return 0;
}

#endif

/* ── Global State (Static, Rule 3) ───────────────────────────────────── */

static virtio_blk_pci_t s_blk_devices[VIRTIO_BLK_MAX_DEVICES];
static int              s_blk_dev_count = 0;

/* Statically allocated split ring structures per device */
static struct vring_desc s_desc_rings[VIRTIO_BLK_MAX_DEVICES][VIRTIO_BLK_QUEUE_SIZE] __attribute__((aligned(16)));
static struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_BLK_QUEUE_SIZE];
} s_avail_rings[VIRTIO_BLK_MAX_DEVICES] __attribute__((aligned(2)));

static struct {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[VIRTIO_BLK_QUEUE_SIZE];
} s_used_rings[VIRTIO_BLK_MAX_DEVICES] __attribute__((aligned(4)));

/* Command & Status Buffers (Static, 16-byte aligned) */
static struct virtio_blk_req s_req_bufs[VIRTIO_BLK_MAX_DEVICES] __attribute__((aligned(16)));
static uint8_t               s_status_bufs[VIRTIO_BLK_MAX_DEVICES] __attribute__((aligned(16)));

/* Static BlkDev handles */
static BlkDev s_blk_dev_handles[VIRTIO_BLK_MAX_DEVICES];

/* Mock storage for hosted testing when PCI device not physically present */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
static uint8_t *s_mock_storage[VIRTIO_BLK_MAX_DEVICES] = {NULL};
static uint64_t s_mock_storage_size[VIRTIO_BLK_MAX_DEVICES] = {0};

void virtio_blk_set_mock_storage(int dev_idx, void *buf, uint64_t size_bytes) {
    if (dev_idx >= 0 && dev_idx < VIRTIO_BLK_MAX_DEVICES) {
        s_mock_storage[dev_idx] = (uint8_t *)buf;
        s_mock_storage_size[dev_idx] = size_bytes;
        s_blk_devices[dev_idx].is_detected = true;
        s_blk_devices[dev_idx].is_initialized = true;
        s_blk_devices[dev_idx].capacity_sectors = size_bytes / 512;
        if (s_blk_dev_count <= dev_idx) s_blk_dev_count = dev_idx + 1;
    }
}
#endif

/* ── PCI Bus Scanning & Discovery ────────────────────────────────────── */

int virtio_blk_pci_probe_all(void) {
    s_blk_dev_count = 0;

    for (uint32_t bus = 0; bus < 4; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_read_config16(bus, slot, func, 0x00);
                if (vendor != VIRTIO_PCI_VENDOR_ID) continue;

                uint16_t device = pci_read_config16(bus, slot, func, 0x02);

                bool is_blk = (device == VIRTIO_PCI_DEVICE_BLOCK) ||
                              (device == VIRTIO_PCI_DEVICE_LEGACY_BLOCK);

                if (!is_blk) continue;
                if (s_blk_dev_count >= VIRTIO_BLK_MAX_DEVICES) break;

                int idx = s_blk_dev_count++;
                virtio_blk_pci_t *d = &s_blk_devices[idx];
                d->pci_bus   = (uint8_t)bus;
                d->pci_slot  = (uint8_t)slot;
                d->pci_func  = (uint8_t)func;
                d->device_id = device;
                d->is_detected = true;

                /* Enable Bus Master, Memory Space, and SERR */
                uint32_t cmd = pci_read_config32(bus, slot, func, 0x04);
                cmd |= (1U << 1) | (1U << 2) | (1U << 8);
                pci_write_config32(bus, slot, func, 0x04, cmd);

                uart_puts_raw("[VIRTIO-BLK] Detected VirtIO-Block at PCI ");
                vblk_uart_dec(bus);  uart_puts_raw(":");
                vblk_uart_dec(slot); uart_puts_raw(".");
                vblk_uart_dec(func);
                uart_puts_raw(" (DevID="); vblk_uart_hex32(device); uart_puts_raw(")\r\n");

                /* Walk PCI capabilities for Modern VirtIO structures */
                uint8_t cap_ptr = pci_read_config8(bus, slot, func, 0x34) & ~0x03U;
                int max_caps = 48;

                while (cap_ptr >= 0x40 && max_caps-- > 0) {
                    uint8_t cap_id = pci_read_config8(bus, slot, func, cap_ptr);
                    if (cap_id == 0x09) { /* PCI_CAP_ID_VNDR */
                        uint8_t  cfg_type = pci_read_config8(bus, slot, func, cap_ptr + 3);
                        uint8_t  bar      = pci_read_config8(bus, slot, func, cap_ptr + 4);
                        uint32_t offset   = pci_read_config32(bus, slot, func, cap_ptr + 8);
                        uintptr_t bar_base = pci_get_bar_base(bus, slot, func, bar);

                        if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                            d->common_cfg = (volatile void *)(bar_base + offset);
                        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                            d->notify_bar_base = bar_base;
                            d->notify_offset   = offset;
                            d->notify_off_mult = pci_read_config32(bus, slot, func, cap_ptr + 16);
                        } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                            d->device_cfg = (volatile void *)(bar_base + offset);
                        }
                    }
                    cap_ptr = pci_read_config8(bus, slot, func, cap_ptr + 1) & ~0x03U;
                }

                /* Initialize controller */
                virtio_blk_pci_init(idx);
            }
        }
    }

    return s_blk_dev_count;
}

int virtio_blk_pci_init(int dev_idx) {
    if (dev_idx < 0 || dev_idx >= s_blk_dev_count) return -1;
    virtio_blk_pci_t *d = &s_blk_devices[dev_idx];

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
    if (s_mock_storage[dev_idx]) {
        d->is_initialized = true;
        return 0;
    }
#endif

    volatile struct virtio_pci_common_cfg *cfg = (volatile struct virtio_pci_common_cfg *)d->common_cfg;
    if (!cfg) {
        uart_puts_raw("[VIRTIO-BLK] Missing common configuration MMIO!\r\n");
        return -2;
    }

    /* 1. Device Reset */
    cfg->device_status = 0;
    for (volatile int i = 0; i < 1000; i++) cpu_relax();

    /* 2. Acknowledge & Driver Status */
    cfg->device_status = VIRTIO_STAT_ACKNOWLEDGE | VIRTIO_STAT_DRIVER;

    /* 3. Negotiate Features (Accept VirtIO 1.0 VERSION_1) */
    cfg->driver_feature_select = 1;
    cfg->driver_feature = (uint32_t)(VIRTIO_F_VERSION_1 >> 32);
    cfg->device_status |= VIRTIO_STAT_FEATURES_OK;

    if (!(cfg->device_status & VIRTIO_STAT_FEATURES_OK)) {
        uart_puts_raw("[VIRTIO-BLK] Feature negotiation failed!\r\n");
        return -3;
    }

    /* 4. Configure VirtQueue 0 (requestq) */
    cfg->queue_select = 0;
    uint16_t qsize = cfg->queue_size;
    if (qsize == 0 || qsize > VIRTIO_BLK_QUEUE_SIZE) {
        cfg->queue_size = VIRTIO_BLK_QUEUE_SIZE;
        qsize = VIRTIO_BLK_QUEUE_SIZE;
    }

    cfg->queue_desc   = (uint64_t)(uintptr_t)&s_desc_rings[dev_idx][0];
    cfg->queue_driver = (uint64_t)(uintptr_t)&s_avail_rings[dev_idx];
    cfg->queue_device = (uint64_t)(uintptr_t)&s_used_rings[dev_idx];

    /* Calculate Doorbell Notify Address */
    uint16_t notify_off = cfg->queue_notify_off;
    d->notify_addr = (volatile uint16_t *)(d->notify_bar_base + d->notify_offset + (uint32_t)notify_off * d->notify_off_mult);

    cfg->queue_enable = 1;

    /* 5. Read Device Capacity from Device Configuration MMIO */
    if (d->device_cfg) {
        volatile struct virtio_blk_config *bcfg = (volatile struct virtio_blk_config *)d->device_cfg;
        d->capacity_sectors = bcfg->capacity;
    } else {
        d->capacity_sectors = 0;
    }

    /* 6. Mark DRIVER_OK */
    cfg->device_status |= VIRTIO_STAT_DRIVER_OK;
    d->last_used_idx = s_used_rings[dev_idx].idx;
    d->is_initialized = true;

    uart_puts_raw("[VIRTIO-BLK] Unit ");
    vblk_uart_dec(dev_idx);
    uart_puts_raw(" online. Capacity: ");
    vblk_uart_dec((uint32_t)(d->capacity_sectors * 512 / (1024 * 1024)));
    uart_puts_raw(" MiB (");
    vblk_uart_dec((uint32_t)d->capacity_sectors);
    uart_puts_raw(" sectors)\r\n");

    return 0;
}

/* ── Synchronous Sector Read / Write Engine ──────────────────────────── */

int virtio_blk_read_sectors(int dev_idx, uint64_t sector, void *buf, uint32_t count) {
    if (dev_idx < 0 || dev_idx >= s_blk_dev_count || !buf || count == 0) return -1;
    virtio_blk_pci_t *d = &s_blk_devices[dev_idx];
    if (!d->is_initialized) return -2;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
    if (s_mock_storage[dev_idx]) {
        uint64_t offset = sector * 512;
        uint64_t bytes = (uint64_t)count * 512;
        if (offset + bytes > s_mock_storage_size[dev_idx]) return -3;
        memcpy(buf, s_mock_storage[dev_idx] + offset, bytes);
        return 0;
    }
#endif

    if (!d->notify_addr) return -3;

    struct virtio_blk_req *req = &s_req_bufs[dev_idx];
    req->type = VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;

    volatile uint8_t *status = &s_status_bufs[dev_idx];
    *status = 0xFF;

    struct vring_desc *desc = s_desc_rings[dev_idx];

    /* Desc 0: Request Header (Host Read-Only) */
    desc[0].addr  = (uint64_t)(uintptr_t)req;
    desc[0].len   = (uint32_t)sizeof(*req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next  = 1;

    /* Desc 1: Buffer (Host Write-Only) */
    desc[1].addr  = (uint64_t)(uintptr_t)buf;
    desc[1].len   = count * 512;
    desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    desc[1].next  = 2;

    /* Desc 2: Status (Host Write-Only) */
    desc[2].addr  = (uint64_t)(uintptr_t)status;
    desc[2].len   = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next  = 0;

    /* Publish head descriptor to Avail Ring */
    uint16_t avail_idx = s_avail_rings[dev_idx].idx;
    s_avail_rings[dev_idx].ring[avail_idx % VIRTIO_BLK_QUEUE_SIZE] = 0;
    __asm__ volatile("" ::: "memory");
    s_avail_rings[dev_idx].idx = avail_idx + 1;
    __asm__ volatile("" ::: "memory");

    /* Ring Doorbell */
    *d->notify_addr = 0;

    /* Spin wait with pause */
    uint16_t expected_used = d->last_used_idx + 1;
    for (uint32_t spin = 0; spin < 5000000; spin++) {
        __asm__ volatile("" ::: "memory");
        if (s_used_rings[dev_idx].idx == expected_used) {
            d->last_used_idx = s_used_rings[dev_idx].idx;
            return (*status == VIRTIO_BLK_S_OK) ? 0 : -4;
        }
        cpu_relax();
    }

    uart_puts_raw("[VIRTIO-BLK] Read timeout on sector ");
    vblk_uart_dec((uint32_t)sector);
    uart_puts_raw("\r\n");
    return -5;
}

int virtio_blk_write_sectors(int dev_idx, uint64_t sector, const void *buf, uint32_t count) {
    if (dev_idx < 0 || dev_idx >= s_blk_dev_count || !buf || count == 0) return -1;
    virtio_blk_pci_t *d = &s_blk_devices[dev_idx];
    if (!d->is_initialized) return -2;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
    if (s_mock_storage[dev_idx]) {
        uint64_t offset = sector * 512;
        uint64_t bytes = (uint64_t)count * 512;
        if (offset + bytes > s_mock_storage_size[dev_idx]) return -3;
        memcpy(s_mock_storage[dev_idx] + offset, buf, bytes);
        return 0;
    }
#endif

    if (!d->notify_addr) return -3;

    struct virtio_blk_req *req = &s_req_bufs[dev_idx];
    req->type = VIRTIO_BLK_T_OUT;
    req->reserved = 0;
    req->sector = sector;

    volatile uint8_t *status = &s_status_bufs[dev_idx];
    *status = 0xFF;

    struct vring_desc *desc = s_desc_rings[dev_idx];

    /* Desc 0: Request Header (Host Read-Only) */
    desc[0].addr  = (uint64_t)(uintptr_t)req;
    desc[0].len   = (uint32_t)sizeof(*req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next  = 1;

    /* Desc 1: Buffer (Host Read-Only) */
    desc[1].addr  = (uint64_t)(uintptr_t)buf;
    desc[1].len   = count * 512;
    desc[1].flags = VRING_DESC_F_NEXT;
    desc[1].next  = 2;

    /* Desc 2: Status (Host Write-Only) */
    desc[2].addr  = (uint64_t)(uintptr_t)status;
    desc[2].len   = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next  = 0;

    /* Publish head descriptor to Avail Ring */
    uint16_t avail_idx = s_avail_rings[dev_idx].idx;
    s_avail_rings[dev_idx].ring[avail_idx % VIRTIO_BLK_QUEUE_SIZE] = 0;
    __asm__ volatile("" ::: "memory");
    s_avail_rings[dev_idx].idx = avail_idx + 1;
    __asm__ volatile("" ::: "memory");

    /* Ring Doorbell */
    *d->notify_addr = 0;

    /* Spin wait with pause */
    uint16_t expected_used = d->last_used_idx + 1;
    for (uint32_t spin = 0; spin < 5000000; spin++) {
        __asm__ volatile("" ::: "memory");
        if (s_used_rings[dev_idx].idx == expected_used) {
            d->last_used_idx = s_used_rings[dev_idx].idx;
            return (*status == VIRTIO_BLK_S_OK) ? 0 : -4;
        }
        cpu_relax();
    }

    uart_puts_raw("[VIRTIO-BLK] Write timeout on sector ");
    vblk_uart_dec((uint32_t)sector);
    uart_puts_raw("\r\n");
    return -5;
}

uint64_t virtio_blk_get_capacity(int dev_idx) {
    if (dev_idx >= 0 && dev_idx < s_blk_dev_count) {
        return s_blk_devices[dev_idx].capacity_sectors;
    }
    return 0;
}

int virtio_blk_get_device_count(void) {
    return s_blk_dev_count;
}

const virtio_blk_pci_t *virtio_blk_get_device(int dev_idx) {
    if (dev_idx >= 0 && dev_idx < s_blk_dev_count) {
        return &s_blk_devices[dev_idx];
    }
    return NULL;
}

/* ── BlkDev Abstraction Implementation ───────────────────────────────── */

static int virtio_blkdev_read(BlkDev *dev, BLK lba, void *buf, UW count) {
    int dev_idx = (int)(intptr_t)dev->ctx;
    uint32_t spb = dev->block_size / 512;
    uint64_t sector = (uint64_t)lba * spb;
    uint32_t nsec   = count * spb;
    return virtio_blk_read_sectors(dev_idx, sector, buf, nsec);
}

static int virtio_blkdev_write(BlkDev *dev, BLK lba, const void *buf, UW count) {
    int dev_idx = (int)(intptr_t)dev->ctx;
    uint32_t spb = dev->block_size / 512;
    uint64_t sector = (uint64_t)lba * spb;
    uint32_t nsec   = count * spb;
    return virtio_blk_write_sectors(dev_idx, sector, buf, nsec);
}

BlkDev *blk_virtio_create(int dev_idx) {
    if (dev_idx < 0 || dev_idx >= s_blk_dev_count) return NULL;
    virtio_blk_pci_t *d = &s_blk_devices[dev_idx];
    if (!d->is_initialized) return NULL;

    BlkDev *dev = &s_blk_dev_handles[dev_idx];
    dev->read       = virtio_blkdev_read;
    dev->write      = virtio_blkdev_write;
    dev->block_size = 512;  /* Standard sector size for partition detection */
    dev->nblocks    = (UW)d->capacity_sectors;
    dev->ctx        = (void *)(intptr_t)dev_idx;

    return dev;
}
