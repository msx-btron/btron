/*
 * core_boot.c — Multiboot 1 & QEMU Direct Kernel Boot Loader Entry
 * Full interactive PS/2 Keyboard & UART Serial engine.
 * Includes VGA 80x25 text mode engine (0xB8000) and VESA VBE 1024x768 32-bpp driver for 'startx'/'desktop'.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/core.h>
#include <btron/desktop.h>
#include <btron/wnd.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/smp.h>
#include <btron/workbench.h>
#include <drivers/vesa/vesa.h>
#include <drivers/virtio/virtio_gpu.h>
#include <drivers/virtio/virtio_blk.h>
#include <fs/vol_api.h>
#include <fs/volume.h>
#include <drivers/uefi/ps2_mouse.h>
#include <drivers/pc98/pc98_mouse.h>
#include <btron/libc_shim.h>

extern void orchestra_ui_tick(void);
extern void cassette_service(void);

#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003

struct multiboot_header {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
} __attribute__((packed));

__attribute__((section(".multiboot"), used))
const struct multiboot_header g_multiboot_header = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    (uint32_t)(-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS))
};

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define COM1_PORT 0x3F8

static void uart_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void uart_putc(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0);
    outb(COM1_PORT, (uint8_t)c);
}

void uart_puts_raw(const char *str) {
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

static int uart_has_char(void) {
    return (inb(COM1_PORT + 5) & 0x01) ? 1 : 0;
}

static char uart_getc(void) {
    if (!uart_has_char()) return 0;
    return (char)inb(COM1_PORT);
}

/* ═══════════════════════════════════════════════════════════════════
 * PS/2 Keyboard Controller
 * ═══════════════════════════════════════════════════════════════════ */

static int ps2_has_key(void) {
    /* Bit 0 = Output Buffer Full, Bit 5 = Auxiliary (Mouse) data */
    for (int i = 0; i < 16; i++) {
        uint8_t st = inb(0x64);
        if (!(st & 0x01)) return 0;
        if (st & 0x20) {
            (void)inb(0x60); /* Drain auxiliary mouse byte so keyboard is never blocked */
            continue;
        }
        return 1;
    }
    return 0;
}

static uint8_t ps2_get_scancode(void) {
    if (!ps2_has_key()) return 0;
    return inb(0x60);
}

static char ps2_scancode_to_ascii(uint8_t sc, int shift) {
    if (sc & 0x80) return 0;
    switch (sc) {
        case 0x1E: return shift ? 'A' : 'a';
        case 0x30: return shift ? 'B' : 'b';
        case 0x2E: return shift ? 'C' : 'c';
        case 0x20: return shift ? 'D' : 'd';
        case 0x12: return shift ? 'E' : 'e';
        case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g';
        case 0x23: return shift ? 'H' : 'h';
        case 0x17: return shift ? 'I' : 'i';
        case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k';
        case 0x26: return shift ? 'L' : 'l';
        case 0x32: return shift ? 'M' : 'm';
        case 0x31: return shift ? 'N' : 'n';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x10: return shift ? 'Q' : 'q';
        case 0x13: return shift ? 'R' : 'r';
        case 0x1F: return shift ? 'S' : 's';
        case 0x14: return shift ? 'T' : 't';
        case 0x16: return shift ? 'U' : 'u';
        case 0x2F: return shift ? 'V' : 'v';
        case 0x11: return shift ? 'W' : 'w';
        case 0x2D: return shift ? 'X' : 'x';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x2C: return shift ? 'Z' : 'z';
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x0F: return '\t';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x0E: return '\b';
        case 0x01: return 0x1B; /* ESC */
        default: return 0;
    }
}

#if defined(BTRON_PC98_TARGET)
#include <drivers/pc98/pc98_kbd.h>
#define btron_kbd_init()                       pc98_kbd_init()
#define btron_kbd_has_key()                    pc98_kbd_has_key()
#define btron_kbd_get_scancode()               pc98_kbd_get_scancode()
#define btron_kbd_scancode_to_ascii(sc, shift) pc98_kbd_scancode_to_ascii(sc, shift)
#else
#define btron_kbd_init()                       ((void)0)
#define btron_kbd_has_key()                    ps2_has_key()
#define btron_kbd_get_scancode()               ps2_get_scancode()
#define btron_kbd_scancode_to_ascii(sc, shift) ps2_scancode_to_ascii(sc, shift)
#endif

/* ═══════════════════════════════════════════════════════════════════
 * VGA 80x25 Text Mode Driver (0xB8000)
 * ═══════════════════════════════════════════════════════════════════ */

#define VGA_VRAM ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static uint16_t s_vga_shadow[VGA_COLS * VGA_ROWS];
static uint16_t vga_cursor_x = 0;
static uint16_t vga_cursor_y = 0;
static uint8_t  vga_attr = 0x07;

static inline void vga_write_cell(int idx, uint16_t val) {
    if (idx >= 0 && idx < VGA_COLS * VGA_ROWS) {
        s_vga_shadow[idx] = val;
        VGA_VRAM[idx] = val;
    }
}

static void vga_update_cursor(void) {
    uint16_t pos = vga_cursor_y * VGA_COLS + vga_cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_clear(uint8_t attr) {
    vga_attr = attr;
    uint16_t fill = (uint16_t)(' ') | ((uint16_t)attr << 8);
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        vga_write_cell(i, fill);
    }
    vga_cursor_x = 0;
    vga_cursor_y = 0;
    vga_update_cursor();
}

static void vga_scroll(void) {
    if (vga_cursor_y >= VGA_ROWS) {
        for (int y = 0; y < VGA_ROWS - 1; y++) {
            for (int x = 0; x < VGA_COLS; x++) {
                vga_write_cell(y * VGA_COLS + x, s_vga_shadow[(y + 1) * VGA_COLS + x]);
            }
        }
        uint16_t fill = (uint16_t)(' ') | ((uint16_t)vga_attr << 8);
        for (int x = 0; x < VGA_COLS; x++) {
            vga_write_cell((VGA_ROWS - 1) * VGA_COLS + x, fill);
        }
        vga_cursor_y = VGA_ROWS - 1;
    }
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_cursor_x = 0;
        vga_cursor_y++;
        vga_scroll();
    } else if (c == '\r') {
        vga_cursor_x = 0;
    } else if (c == '\b') {
        if (vga_cursor_x > 0) {
            vga_cursor_x--;
            vga_write_cell(vga_cursor_y * VGA_COLS + vga_cursor_x, (uint16_t)(' ') | ((uint16_t)vga_attr << 8));
        }
    } else {
        vga_write_cell(vga_cursor_y * VGA_COLS + vga_cursor_x, (uint16_t)(uint8_t)c | ((uint16_t)vga_attr << 8));
        vga_cursor_x++;
        if (vga_cursor_x >= VGA_COLS) {
            vga_cursor_x = 0;
            vga_cursor_y++;
            vga_scroll();
        }
    }
    vga_update_cursor();
}

static void vga_sync_to_virtio_gpu(void);

static void kprint(const char *str, uint8_t attr) {
    vga_attr = attr;
    int saw_nl = 0;
    while (*str) {
        char c = *str++;
        if (c == '\n') { uart_putc('\r'); saw_nl = 1; }
        uart_putc(c);
        vga_putc(c);
    }
    if (saw_nl) vga_sync_to_virtio_gpu();
}

static void vga_print_at(int x, int y, const char *str, uint8_t attr) {
    int idx = y * VGA_COLS + x;
    while (*str && idx < VGA_COLS * VGA_ROWS) {
        char c = *str++;
        vga_write_cell(idx++, (uint16_t)(uint8_t)c | ((uint16_t)attr << 8));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Ski Bootloader
 * ═══════════════════════════════════════════════════════════════════ */

#define SKI_MAX_TARGETS 24

typedef struct {
    char        title[64];
    char        cmdline[128];
    int         cores;
    int         dev_idx;       /* VirtIO block device index or -1 */
    int         part_idx;      /* Partition slot (0..3) or -1 */
    int         is_bootable;   /* 1 if active bootable B-System partition */
    int         is_installer;  /* 1 if installer mode */
    uint8_t     status;        /* MBR partition status (0x80 = active boot) */
} ski_target_t;

static ski_target_t s_targets[SKI_MAX_TARGETS];
static int          s_target_count = 0;

static void safe_strcpy_boot(char *dst, const char *src, size_t dst_sz) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i + 1 < dst_sz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

extern Volume *g_sys_vol;
extern Volume *g_anders_vol;
extern WND* open_installer_window(void) __attribute__((weak));
extern void b_installer_ui_tick(void) __attribute__((weak));

#ifdef BTRON_ALLOW_VOL_BOOT
static int s_slice_boot_enabled = 1;
#else
static int s_slice_boot_enabled = 0;
#endif

static void uart_log_er(const char *subsys, const char *op, ER er) {
    uart_puts_raw("[");
    uart_puts_raw(subsys);
    uart_puts_raw("] ");
    uart_puts_raw(op);
    uart_puts_raw(": ");
    uart_puts_raw(btron_err_name(er));
    uart_puts_raw(" - ");
    uart_puts_raw(btron_err_str(er));
    uart_puts_raw("\r\n");
}

static void btron_storage_probe_and_mount(void) {
#if !defined(BTRON_PC98_TARGET)
    if (virtio_blk_get_device_count() == 0) {
        virtio_blk_pci_probe_all();
    }
    int vcnt = virtio_blk_get_device_count();
    for (int i = 0; i < vcnt; i++) {
        BlkDev *vdev = blk_virtio_create(i);
        if (!vdev) continue;

        int had_sys = (g_sys_vol != NULL);
        int had_anders = (g_anders_vol != NULL);
        ER er = blk_mount_disk_partitions(vdev, -1);
        if (er > 0) {
            if (!had_sys && g_sys_vol) {
                uart_puts_raw("[HFDS] Mounted /SYS on VirtIO-Block device\r\n");
            }
            if (!had_anders && g_anders_vol) {
                uart_puts_raw("[HFDS] Mounted /ANDERS on VirtIO-Block device\r\n");
            }
        } else if (er == ER_PART_NOMBR) {
            if (s_slice_boot_enabled && !g_sys_vol) {
                BlkDev *raw_slice = blk_partition_create(vdev, 0, vdev->nblocks / 2, 1024);
                if (raw_slice) {
                    Volume *v = vol_mount(raw_slice);
                    if (v) {
                        g_sys_vol = v;
                        uart_puts_raw("[HFDS] Mounted direct .vol slice (slice_boot=1)\r\n");
                    } else {
                        blk_partition_release(raw_slice);
                        uart_log_er("HFDS", "direct slice mount", ER_FS_BADMAGIC);
                    }
                }
            } else {
                uart_log_er("HFDS", "virtio disk probe", ER_PART_SLICE_DIS);
            }
        } else if (er < 0 && er != ER_PART_NOTFOUND) {
            uart_log_er("HFDS", "virtio disk probe", er);
        }
    }
#endif
}

volatile int g_reboot_requested = 0;
static int s_selected = 0;
static int s_scroll_offset = 0;
static int s_boot_triggered = 0;

void btron_core_reboot(void) {
    uart_puts_raw("\r\n[SYSTEM] Reboot requested. Returning to Ski Bootloader...\r\n");
    g_reboot_requested = 1;
}

static void ski_init_targets(void) {
    s_target_count = 0;

#if !defined(BTRON_PC98_TARGET)
    if (virtio_blk_get_device_count() == 0) {
        virtio_blk_pci_probe_all();
    }
    int vcnt = virtio_blk_get_device_count();
    int found_bootable = 0;

    for (int d = 0; d < vcnt && s_target_count < SKI_MAX_TARGETS; d++) {
        uint8_t sec0[512];
        char d_c = (char)('0' + d);
        if (virtio_blk_read_sectors(d, 0, sec0, 1) != 0) continue;
        if (sec0[510] != 0x55 || sec0[511] != 0xAA) {
            /* Unpartitioned disk (or raw .vol slice). On UEFI, direct .vol boot requires slice_boot=1 */
            uint16_t m0 = (uint16_t)sec0[0] | ((uint16_t)sec0[1] << 8);
            bool raw_btron = (m0 == 0x52FE || m0 == 0x42FE || m0 == 0x62FE ||
                              m0 == 0xFE52 || m0 == 0xFE42 || m0 == 0xFE62);
            ski_target_t *t = &s_targets[s_target_count++];
            if (raw_btron && s_slice_boot_enabled) {
                safe_strcpy_boot(t->title, "[virtio0] Direct .vol Slice (BTRON OS)", sizeof(t->title));
                t->title[7] = d_c;
                safe_strcpy_boot(t->cmdline, "btron3 root=virtio0 slice_boot=1 smp=4", sizeof(t->cmdline));
                t->cmdline[18] = d_c;
                t->cores = 4; t->dev_idx = d; t->part_idx = -1;
                t->is_bootable = 1; t->is_installer = 0; t->status = 0x80;
                found_bootable++;
            } else {
                safe_strcpy_boot(t->title, "[virtio0] Unpartitioned disk (not bootable)", sizeof(t->title));
                t->title[7] = d_c;
                safe_strcpy_boot(t->cmdline, raw_btron ? "(direct .vol disabled; pass slice_boot=1)" : "(no partition table)", sizeof(t->cmdline));
                t->cores = 1; t->dev_idx = d; t->part_idx = -1;
                t->is_bootable = 0; t->is_installer = 0; t->status = 0;
            }
            continue;
        }

        /* Check for Protective MBR (0xEE) -> GPT enumeration */
        bool is_gpt = false;
        for (int p = 0; p < 4; p++) {
            if (sec0[446 + p * 16 + 4] == 0xEE) { is_gpt = true; break; }
        }
        if (is_gpt) {
            uint8_t gpt_hdr[512];
            if (virtio_blk_read_sectors(d, 1, gpt_hdr, 1) == 0 &&
                gpt_hdr[0] == 'E' && gpt_hdr[1] == 'F' && gpt_hdr[2] == 'I' && gpt_hdr[3] == ' ' &&
                gpt_hdr[4] == 'P' && gpt_hdr[5] == 'A' && gpt_hdr[6] == 'R' && gpt_hdr[7] == 'T') {
                uint32_t part_lba  = (uint32_t)gpt_hdr[72] | ((uint32_t)gpt_hdr[73] << 8) |
                                     ((uint32_t)gpt_hdr[74] << 16) | ((uint32_t)gpt_hdr[75] << 24);
                uint32_t num_parts = (uint32_t)gpt_hdr[80] | ((uint32_t)gpt_hdr[81] << 8) |
                                     ((uint32_t)gpt_hdr[82] << 16) | ((uint32_t)gpt_hdr[83] << 24);
                if (num_parts > 16) num_parts = 16;
                for (uint32_t gp = 0; gp < num_parts && s_target_count < SKI_MAX_TARGETS; gp++) {
                    uint8_t esec[512];
                    if (virtio_blk_read_sectors(d, part_lba + (gp / 4), esec, 1) != 0) break;
                    const uint8_t *ent = esec + (gp % 4) * 128;
                    uint32_t start = (uint32_t)ent[32] | ((uint32_t)ent[33] << 8) |
                                     ((uint32_t)ent[34] << 16) | ((uint32_t)ent[35] << 24);
                    uint32_t end   = (uint32_t)ent[40] | ((uint32_t)ent[41] << 8) |
                                     ((uint32_t)ent[42] << 16) | ((uint32_t)ent[43] << 24);
                    if (start == 0 || end < start) continue;
                    uint8_t probe[512];
                    bool is_btron = false;
                    if (virtio_blk_read_sectors(d, start, probe, 1) == 0) {
                        uint16_t m = (uint16_t)probe[0] | ((uint16_t)probe[1] << 8);
                        if (m == 0x52FE || m == 0x42FE || m == 0x62FE ||
                            m == 0xFE52 || m == 0xFE42 || m == 0xFE62) {
                            is_btron = true;
                        }
                    }
                    ski_target_t *t = &s_targets[s_target_count++];
                    char title[64];
                    int tp = 0;
                    #define TPUT(ch) do { if (tp < 60) title[tp++] = (ch); } while (0)
                    #define TSTR(str) do { const char *q_ = (str); while (*q_) TPUT(*q_++); } while (0)
                    TSTR("[virtio"); TPUT(d_c); TSTR(":gpt"); TPUT((char)('1' + (gp % 9))); TSTR("] ");
                    if (is_btron) {
                        const VolumeHeader *vh = (const VolumeHeader *)probe;
                        int li = 0;
                        while (li < 24 && vh->vol_name[li] >= 32 && vh->vol_name[li] <= 126) {
                            TPUT((char)vh->vol_name[li++]);
                        }
                        if (li == 0) TSTR("BTRON GPT Volume");
                        TSTR(" (BTRON GPT)");
                    } else {
                        TSTR("GPT Partition");
                    }
                    title[tp] = '\0';
                    safe_strcpy_boot(t->title, title, sizeof(t->title));
                    #undef TPUT
                    #undef TSTR
                    safe_strcpy_boot(t->cmdline, is_btron ? "btron3 root=gpt smp=4 acpi=6.5" : "(not a B-System volume - cannot boot)", sizeof(t->cmdline));
                    t->cores = 4; t->dev_idx = d; t->part_idx = (int)gp;
                    t->is_bootable = is_btron ? 1 : 0; t->is_installer = 0; t->status = is_btron ? 0x80 : 0;
                    if (is_btron) found_bootable++;
                }
                continue;
            }
        }

        for (int p = 0; p < 4 && s_target_count < SKI_MAX_TARGETS; p++) {
            const uint8_t *ent = sec0 + 446 + p * 16;
            uint8_t  status = ent[0];
            uint8_t  type   = ent[4];
            uint32_t start  = (uint32_t)ent[8] | ((uint32_t)ent[9] << 8) |
                              ((uint32_t)ent[10] << 16) | ((uint32_t)ent[11] << 24);
            uint32_t count  = (uint32_t)ent[12] | ((uint32_t)ent[13] << 8) |
                              ((uint32_t)ent[14] << 16) | ((uint32_t)ent[15] << 24);
            if (count == 0) continue;
            char p_c = (char)('1' + p);

            bool is_btron = false;
            uint8_t probe[512];
            bool probe_ok = (virtio_blk_read_sectors(d, start, probe, 1) == 0);
            if (probe_ok) {
                uint16_t m = (uint16_t)probe[0] | ((uint16_t)probe[1] << 8);
                bool magic = (m == 0x52FE || m == 0x42FE || m == 0x62FE ||
                              m == 0xFE52 || m == 0xFE42 || m == 0xFE62);
                if (!magic && count > 8) {
                    /* Volumes formatted into a raw slice start 8 sectors in (see blk_part.c) */
                    uint8_t probe2[512];
                    if (virtio_blk_read_sectors(d, start + 8, probe2, 1) == 0) {
                        uint16_t m2 = (uint16_t)probe2[0] | ((uint16_t)probe2[1] << 8);
                        if (m2 == 0x52FE || m2 == 0x42FE || m2 == 0x62FE ||
                            m2 == 0xFE52 || m2 == 0xFE42 || m2 == 0xFE62) {
                            for (int k = 0; k < 512; k++) probe[k] = probe2[k];
                            magic = true;
                        }
                    }
                }
                if (magic) is_btron = true;
            }
            if (!probe_ok && blk_is_btron_mbr_type(type)) is_btron = true;

            ski_target_t *t = &s_targets[s_target_count++];
            char title[64];
            int tp = 0;
            #define TPUT(ch) do { if (tp < 60) title[tp++] = (ch); } while (0)
            #define TSTR(str) do { const char *q_ = (str); while (*q_) TPUT(*q_++); } while (0)
            TSTR("[virtio"); TPUT(d_c); TPUT(':'); TPUT('p'); TPUT(p_c); TSTR("] ");

            if (is_btron) {
                char label[40]; int li = 0;
                if (probe_ok) {
                    const VolumeHeader *vh = (const VolumeHeader *)probe;
                    while (li < 28 && vh->vol_name[li] >= 32 && vh->vol_name[li] <= 126) {
                        label[li] = (char)vh->vol_name[li]; li++;
                    }
                }
                label[li] = '\0';
                TSTR(li ? label : "BTRON Volume");
                TSTR(" (BTRON OS)");
            } else {
                const char *tn;
                switch (type) {
                    case 0x01: case 0x04: case 0x06: case 0x0E: tn = "FAT16 volume"; break;
                    case 0x0B: case 0x0C: tn = "FAT32 volume"; break;
                    case 0x07: tn = "NTFS/exFAT volume"; break;
                    case 0x05: case 0x0F: tn = "Extended partition"; break;
                    case 0x13: case 0x61: case 0x62: tn = "Unformatted B-FS slice"; break;
                    case 0x27: tn = "RAW data slice"; break;
                    case 0x82: tn = "Linux swap"; break;
                    case 0x83: tn = "Linux volume"; break;
                    case 0xEF: tn = "EFI System Partition"; break;
                    case 0xAF: tn = "HFS/APFS volume"; break;
                    default:   tn = "Unknown volume"; break;
                }
                TSTR(tn);
                static const char hx[] = "0123456789ABCDEF";
                TSTR(" (type 0x"); TPUT(hx[type >> 4]); TPUT(hx[type & 15]); TPUT(')');
            }
            title[tp] = '\0';
            safe_strcpy_boot(t->title, title, sizeof(t->title));
            #undef TPUT
            #undef TSTR

            if (is_btron) {
                safe_strcpy_boot(t->cmdline, "btron3 root=virtio0:p1 smp=4 acpi=6.5", sizeof(t->cmdline));
                t->cmdline[18] = d_c;
                t->cmdline[21] = p_c;
            } else {
                safe_strcpy_boot(t->cmdline, "(not a B-System volume - cannot boot)", sizeof(t->cmdline));
            }
            t->cores = 4;
            t->dev_idx = d;
            t->part_idx = p;
            t->is_bootable = is_btron ? 1 : 0;
            t->is_installer = 0;
            t->status = status;
            if (is_btron) found_bootable++;

            uart_puts_raw("[SKI] Volume: ");
            uart_puts_raw(t->title);
            uart_puts_raw("\r\n");
        }
    }
    if (found_bootable > 0) {
        s_selected = 0;
        s_scroll_offset = 0;
        for (int i = 0; i < s_target_count; i++) {
            if (s_targets[i].is_bootable && s_targets[i].status == 0x80) { s_selected = i; break; }
        }
        if (!s_targets[s_selected].is_bootable) {
            for (int i = 0; i < s_target_count; i++) if (s_targets[i].is_bootable) { s_selected = i; break; }
        }
    }

    if (found_bootable == 0 && s_target_count < SKI_MAX_TARGETS) {
        ski_target_t *t = &s_targets[s_target_count++];
        safe_strcpy_boot(t->title, "B-System BTRON3 Workstation (x86_64 UEFI SMP)", sizeof(t->title));
        safe_strcpy_boot(t->cmdline, "btron3 root=virtio0 smp=4 acpi=6.5", sizeof(t->cmdline));
        t->cores = 4;
        t->dev_idx = 0;
        t->part_idx = -1;
        t->is_bootable = 0;
        t->is_installer = 0;
    }
#else
    ski_target_t *t0 = &s_targets[s_target_count++];
    safe_strcpy_boot(t0->title, "B-System BTRON3 Workstation (PC-98 µITRON SMP)", sizeof(t0->title));
    safe_strcpy_boot(t0->cmdline, "btron3 root=pc98 smp=1", sizeof(t0->cmdline));
    t0->cores = 1;
    t0->dev_idx = -1;
    t0->part_idx = -1;
    t0->is_bootable = 0;
    t0->is_installer = 0;
#endif

    if (s_target_count < SKI_MAX_TARGETS) {
        ski_target_t *t = &s_targets[s_target_count++];
        safe_strcpy_boot(t->title, "B-System BTRON3 Workstation Installer (Live)", sizeof(t->title));
        safe_strcpy_boot(t->cmdline, "btron3 bootmode=installer smp=4", sizeof(t->cmdline));
        t->cores = 4;
        t->dev_idx = -1;
        t->part_idx = -1;
        t->is_bootable = 0;
        t->is_installer = 1;
    }

#if defined(BTRON_PC98_TARGET)
    if (s_target_count < SKI_MAX_TARGETS) {
        ski_target_t *t = &s_targets[s_target_count++];
        safe_strcpy_boot(t->title, "NEC PC-9801 / PC-9821 VM (Awe Morris Kernel)", sizeof(t->title));
        safe_strcpy_boot(t->cmdline, "btron3 pc98 gdc=0xa0000", sizeof(t->cmdline));
        t->cores = 1;
        t->dev_idx = -1;
        t->part_idx = -1;
        t->is_bootable = 0;
        t->is_installer = 0;
    }
#endif
}

#define SKI_VISIBLE_ITEMS 6

static void ski_update_scroll(void) {
    if (s_selected < s_scroll_offset) {
        s_scroll_offset = s_selected;
    } else if (s_selected >= s_scroll_offset + SKI_VISIBLE_ITEMS) {
        s_scroll_offset = s_selected - SKI_VISIBLE_ITEMS + 1;
    }
    int max_scroll = (s_target_count > SKI_VISIBLE_ITEMS) ? s_target_count - SKI_VISIBLE_ITEMS : 0;
    if (s_scroll_offset > max_scroll) s_scroll_offset = max_scroll;
    if (s_scroll_offset < 0) s_scroll_offset = 0;
}

static void render_ski_menu(void) {
    vga_clear(0x1F);

    vga_print_at(0, 0, "================================================================================", 0x1E);
    vga_print_at(2, 1, "  B-System (BTRON3 3.20 HMI) Ski Bootloader", 0x1F);
    vga_print_at(2, 2, "  Platforms: [AArch32] [AArch64] [EM64T] [PC-98]", 0x1B);
    vga_print_at(2, 3, "----------------------------------------------------------------------------", 0x17);

    vga_print_at(2, 4, " Copyright (c) 2026 Synrc Research Center · Kyiv Ukraine", 0x1A);
    vga_print_at(2, 5, "----------------------------------------------------------------------------", 0x17);

    char hdr[80];
    for (int k = 0; k < 76; k++) hdr[k] = ' ';
    hdr[76] = '\0';
    const char *h1 = "Select Operating System Target to Boot:";
    int hp = 0;
    while (*h1 && hp < 40) hdr[hp++] = *h1++;

    hdr[48] = '[';
    hdr[49] = 'T'; hdr[50] = 'a'; hdr[51] = 'r'; hdr[52] = 'g'; hdr[53] = 'e'; hdr[54] = 't'; hdr[55] = ' ';
    hdr[56] = (char)('0' + (s_selected + 1) / 10);
    hdr[57] = (char)('0' + (s_selected + 1) % 10);
    hdr[58] = '/';
    hdr[59] = (char)('0' + s_target_count / 10);
    hdr[60] = (char)('0' + s_target_count % 10);
    hdr[61] = ']';
    if (s_target_count > SKI_VISIBLE_ITEMS) {
        hdr[63] = '['; hdr[64] = '^'; hdr[65] = 'v'; hdr[66] = ' ';
        hdr[67] = 'S'; hdr[68] = 'c'; hdr[69] = 'r'; hdr[70] = 'o'; hdr[71] = 'l'; hdr[72] = 'l'; hdr[73] = ']';
    }
    vga_print_at(2, 6, hdr, 0x1E);
    vga_print_at(2, 7, "----------------------------------------------------------------------------", 0x17);

    for (int r = 0; r < SKI_VISIBLE_ITEMS; r++) {
        int i = s_scroll_offset + r;
        int row_y = 8 + r;
        if (i < s_target_count) {
            uint8_t attr = (i == s_selected) ? 0x70 : 0x1F;
            char row[80];
            for (int k = 0; k < 73; k++) row[k] = ' ';
            row[73] = '\0';

            row[0] = (i == s_selected) ? '>' : ' ';
            row[1] = ' ';
            row[2] = '[';
            row[3] = (i + 1 >= 10) ? (char)('0' + (i + 1) / 10) : ' ';
            row[4] = (char)('0' + (i + 1) % 10);
            row[5] = ']';
            row[6] = ' ';

            const char *t = s_targets[i].title;
            int p = 7;
            while (*t && p < 56) row[p++] = *t++;

            row[57] = '(';
            row[58] = (char)('0' + s_targets[i].cores);
            row[59] = ' ';
            row[60] = 'C';
            row[61] = 'P';
            row[62] = 'U';
            row[63] = 's';
            row[64] = ')';

            vga_print_at(2, row_y, row, attr);
        } else {
            char blank[80];
            for (int k = 0; k < 73; k++) blank[k] = ' ';
            blank[73] = '\0';
            vga_print_at(2, row_y, blank, 0x1F);
        }

        /* Render vertical scrollbar elevator on right side (column 76) */
        char sb_char = '|';
        uint8_t sb_attr = 0x17;
        if (s_target_count > SKI_VISIBLE_ITEMS) {
            if (r == 0) {
                sb_char = (s_scroll_offset > 0) ? '^' : '-';
                sb_attr = (s_scroll_offset > 0) ? 0x1E : 0x17;
            } else if (r == SKI_VISIBLE_ITEMS - 1) {
                sb_char = (s_scroll_offset + SKI_VISIBLE_ITEMS < s_target_count) ? 'v' : '-';
                sb_attr = (s_scroll_offset + SKI_VISIBLE_ITEMS < s_target_count) ? 0x1E : 0x17;
            } else {
                int track_len = SKI_VISIBLE_ITEMS - 2;
                int max_s = s_target_count - SKI_VISIBLE_ITEMS;
                int thumb_pos = (max_s > 0) ? (s_scroll_offset * (track_len - 1)) / max_s : 0;
                if (r - 1 == thumb_pos) {
                    sb_char = '#';
                    sb_attr = 0x1F;
                } else {
                    sb_char = ':';
                    sb_attr = 0x13;
                }
            }
        }
        char sb_str[2] = { sb_char, '\0' };
        vga_print_at(76, row_y, sb_str, sb_attr);
    }

    vga_print_at(2, 15, "----------------------------------------------------------------------------", 0x17);
    vga_print_at(2, 16, "Active Command Line:", 0x1B);
    vga_print_at(4, 17, s_targets[s_selected].cmdline, 0x1E);

    vga_print_at(2, 19, "----------------------------------------------------------------------------", 0x17);
    vga_print_at(2, 20, "Keys: [Up/Down, W/S] Select/Scroll   [+/-] Adjust Cores   [Enter] Boot OS", 0x1B);
    vga_print_at(2, 21, "      [PgUp/PgDn] Page Scroll        [Home/End] First/Last Target", 0x17);
    vga_print_at(0, 23, "================================================================================", 0x1E);

    vga_cursor_x = 0;
    vga_cursor_y = 22;
    vga_update_cursor();
    vga_sync_to_virtio_gpu();
}

static void uart_send_menu_update(void) {
    uart_putc('\r');
    uart_putc('\n');
    uart_putc('>');
    uart_putc(' ');
    const char *t = s_targets[s_selected].title;
    while (*t) uart_putc(*t++);
    uart_puts_raw(" [Target ");
    uart_putc((char)('1' + s_selected));
    uart_putc('/');
    uart_putc((char)('0' + s_target_count));
    uart_puts_raw("]\r\n");
}

/* ═══════════════════════════════════════════════════════════════════
 * BTRON3 Terminal Shell & Graphical Desktop ('startx' / 'desktop')
 * ═══════════════════════════════════════════════════════════════════ */

/* Multiboot info captured from bootloader */
uint32_t g_multiboot_magic = 0;
void    *g_multiboot_info = NULL;
int      g_bootmode_installer = 0;

static const char* strstr_k(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

static uint32_t ps2_scancode_to_btron_key(uint8_t sc, int is_ext, int shift) {
    if (is_ext) {
        switch (sc) {
            case 0x48: return BTRON_KEY_UP;
            case 0x50: return BTRON_KEY_DOWN;
            case 0x4B: return BTRON_KEY_LEFT;
            case 0x4D: return BTRON_KEY_RIGHT;
            case 0x49: return BTRON_KEY_PAGE_UP;
            case 0x51: return BTRON_KEY_PAGE_DOWN;
            case 0x47: return BTRON_KEY_HOME;
            case 0x4F: return BTRON_KEY_END;
            case 0x53: return BTRON_KEY_DELETE;
            case 0x1C: return BTRON_KEY_KP_ENTER;
            default: return 0;
        }
    }
    switch (sc) {
        case 0x01: return BTRON_KEY_ESCAPE;
        case 0x1C: return BTRON_KEY_RETURN;
        case 0x0E: return BTRON_KEY_BACKSPACE;
        case 0x0F: return BTRON_KEY_TAB;
        case 0x39: return BTRON_KEY_SPACE;
        case 0x3B: return BTRON_KEY_F1;
        case 0x3C: return BTRON_KEY_F2;
        case 0x3D: return BTRON_KEY_F3;
        case 0x3E: return BTRON_KEY_F4;
        case 0x3F: return BTRON_KEY_F5;
        case 0x40: return BTRON_KEY_F6;
        case 0x41: return BTRON_KEY_F7;
        case 0x42: return BTRON_KEY_F8;
        case 0x43: return BTRON_KEY_F9;
        case 0x44: return BTRON_KEY_F10;
        default: {
            char c = ps2_scancode_to_ascii(sc, shift);
            return (uint32_t)(uint8_t)c;
        }
    }
}

static int strcmp_k(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static void render_btron_text_desktop(int active_cores) {
    vga_clear(0x07);

    for (int x = 0; x < VGA_COLS; x++) {
        vga_write_cell(x, (uint16_t)(' ') | ((uint16_t)0x70 << 8));
    }
    vga_print_at(1, 0, "BTRON3 3.20  [実身・仮身]  [編集]  [表示]  [設定]  [端末]", 0x70);
    char core_str[32];
    core_str[0] = 'S'; core_str[1] = 'M'; core_str[2] = 'P'; core_str[3] = ':';
    core_str[4] = ' '; core_str[5] = (char)('0' + active_cores);
    core_str[6] = ' '; core_str[7] = 'C'; core_str[8] = 'o'; core_str[9] = 'r'; core_str[10] = 'e'; core_str[11] = 's'; core_str[12] = '\0';
    vga_print_at(64, 0, core_str, 0x70);

    for (int x = 0; x < VGA_COLS; x++) {
        vga_write_cell((VGA_ROWS - 1) * VGA_COLS + x, (uint16_t)(' ') | ((uint16_t)0x70 << 8));
    }
    vga_print_at(1, VGA_ROWS - 1, "[GTerm Console]  [Mozc IME: かな]  [HFDS Storage: 0x10001000]  12:00", 0x70);

    vga_cursor_x = 0;
    vga_cursor_y = 2;
    vga_update_cursor();
    vga_sync_to_virtio_gpu();
}

extern void render_desktop_background(GDEV *dev);
extern void render_system_panel(GDEV *dev);
extern void redraw_all_windows(void);
extern void draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H w, H h);
extern BTRON_DESKTOP* get_btron_desktop(void);
extern WND* open_vobj_manager_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
#if defined(BTRON_UEFI_TARGET)
extern WND* open_glgears_window(void);
extern void btron_scheduler_dispatch(void);
#endif

static COLOR s_desktop_backbuffer[1024 * 768] __attribute__((aligned(16)));

static const COLOR s_vga_palette[16] = {
    0xFF000000, /* 0: Black */
    0xFF0000AA, /* 1: Blue */
    0xFF00AA00, /* 2: Green */
    0xFF00AAAA, /* 3: Cyan */
    0xFFAA0000, /* 4: Red */
    0xFFAA00AA, /* 5: Magenta */
    0xFFAA5500, /* 6: Brown */
    0xFFAAAAAA, /* 7: Light Gray */
    0xFF555555, /* 8: Dark Gray */
    0xFF5555FF, /* 9: Light Blue */
    0xFF55FF55, /* A: Light Green */
    0xFF55FFFF, /* B: Light Cyan */
    0xFFFF5555, /* C: Light Red */
    0xFFFF55FF, /* D: Light Magenta */
    0xFFFFFF55, /* E: Yellow */
    0xFFFFFFFF  /* F: White */
};

static void vga_sync_to_virtio_gpu(void) {
#if !defined(BTRON_PC98_TARGET)
    if (!g_virtio_gpu.is_active) return;
    if (g_virtio_gpu.width != 640 || g_virtio_gpu.height != 400) {
        virtio_gpu_pci_init(640, 400, s_desktop_backbuffer);
        init_desktop_vram(640, 400, s_desktop_backbuffer);
    }
    BTRON_DESKTOP *dt = get_btron_desktop();
    if (!dt || !dt->screen) return;
    GDEV *dev = dt->screen;
    uint8_t def_bg = (uint8_t)((s_vga_shadow[0] >> 12) & 0x0F);
    RECT full_r = { 0, 0, 640, 400 };
    fill_rec(dev, &full_r, s_vga_palette[def_bg]);
    int cw = 8;
    int ch_h = 16;
    for (int y = 0; y < VGA_ROWS; y++) {
        for (int x = 0; x < VGA_COLS; x++) {
            uint16_t cell = s_vga_shadow[y * VGA_COLS + x];
            uint8_t ch = (uint8_t)(cell & 0xFF);
            uint8_t attr = (uint8_t)(cell >> 8);
            COLOR fg = s_vga_palette[attr & 0x0F];
            COLOR bg = s_vga_palette[(attr >> 4) & 0x0F];
            RECT cr = { (H)(x * cw), (H)(y * ch_h),
                        (H)((x + 1) * cw), (H)((y + 1) * ch_h) };
            if (bg != s_vga_palette[def_bg]) {
                fill_rec(dev, &cr, bg);
            }
            if (ch > 0x20 && ch < 0x7F) {
                char ch_s[2] = { (char)ch, '\0' };
                drw_tc_string(dev, (H)(x * cw), (H)(y * ch_h), ch_s, fg, bg);
            }
        }
    }
    virtio_gpu_pci_flush_all();
#endif
}

static void launch_vesa_desktop_session(int active_cores) {
    (void)active_cores;
    uart_puts_raw("\r\n[VESA] Switching to B-System 1024x768x32 Linear Framebuffer Desktop...\r\n");
    uart_puts_raw("[VESA] Disabling VGA text mode (0xB8000)...\r\n");
    uart_puts_raw("[VESA] Programming Bochs DISPI registers: 1024x768x32 LFB\r\n");
    uart_puts_raw("[VESA] Backbuffer allocated: 3 MB @ s_desktop_backbuffer\r\n");
    uart_puts_raw("[VESA] Double-buffer compositor: ACTIVE (tear-free)\r\n");
    kprint("\n[VESA] Switching to B-System 1024x768x32 Linear Framebuffer Desktop...\n", 0x0E);
    vesa_init(1024, 768, 32);

#if !defined(BTRON_PC98_TARGET)
    /* Initialize VirtIO-GPU 2D accelerator if detected on PCI bus */
    if (virtio_gpu_pci_probe() == 0 || g_virtio_gpu.is_detected) {
        virtio_gpu_pci_init(1024, 768, s_desktop_backbuffer);
    }
#endif

    extern int g_cursor_in_backbuffer;
    g_cursor_in_backbuffer = 1;

    /* Initialize authentic BTRON desktop with off-screen backbuffer for tear-free rendering */
    init_desktop_vram(1024, 768, s_desktop_backbuffer);
    BTRON_DESKTOP *dt = get_btron_desktop();
    if (!dt || !dt->screen) {
        return;
    }

    tip_init();
    init_evt_sys();

#if defined(BTRON_PC98_TARGET)
    pc98_mouse_init(1024, 768);
#else
    ps2_mouse_init(1024, 768);
#endif
    workbench_init(1024);

    /* Open authentic B-System windows */
    if ((s_selected >= 0 && s_selected < s_target_count && s_targets[s_selected].is_installer) || g_bootmode_installer) {
        desktop_set_icons_visible(FALSE);
        if (open_installer_window) {
            open_installer_window();
        }
    } else {
        desktop_set_icons_visible(TRUE);
        open_vobj_manager_window();
        open_t_editor_window();
        open_gterm_window();
#if defined(BTRON_UEFI_TARGET)
        open_glgears_window();
#endif
    }

    /* Initial paint of authentic B-System desktop to backbuffer */
    workbench_render(dt->screen, 1024, 768);

    /* Flush frame to VirtIO-GPU hardware and/or VESA LFB */
#if !defined(BTRON_PC98_TARGET)
    if (g_virtio_gpu.is_active) {
        virtio_gpu_pci_flush_all();
    }
#endif
    if (g_vesa.framebuffer && g_vesa.framebuffer != (uint32_t *)s_desktop_backbuffer) {
        memcpy((void *)g_vesa.framebuffer, s_desktop_backbuffer, 1024 * 768 * sizeof(COLOR));
    }

    uart_puts_raw("\n==========================================================\n");
    uart_puts_raw(" [B-SYSTEM] Authentic BTRON3 Desktop Active (1024x768x32)!\n");
    uart_puts_raw("   * Wallpaper: Sakamura B-TRON Teal with Retro Grid\n");
    uart_puts_raw("   * Double Buffering: Active (Tear-Free Compositor)\n");
    uart_puts_raw("   * Icons    : Real Body Cabinet, Editor, GTerm, Audio, Chat\n");
    uart_puts_raw("   * Panel    : [BTRON] System Menu & Japanese JIS Fonts\n");
    uart_puts_raw("   * Windows  : HFDS Cabinet Explorer, Editor, GTerm Shell\n");
    uart_puts_raw(" Controls: Mouse click, type in active window, arrow keys navigate.\n");
    uart_puts_raw("==========================================================\n\n");

    /* Drain any pending keypresses */
    while (btron_kbd_has_key()) (void)btron_kbd_get_scancode();
    while (uart_has_char()) (void)uart_getc();

    int shift = 0;
    int s_ps2_extended = 0;
    EVT ev;

    for (;;) {
        if (g_reboot_requested) {
            break;
        }
        int need_redraw = 0;

#if defined(BTRON_PC98_TARGET)
        pc98_mouse_poll();
#else
        ps2_mouse_poll();
#endif

        H mouse_x = 512, mouse_y = 384;
        get_baremetal_mouse_pos(&mouse_x, &mouse_y);

        while (btron_kbd_has_key()) {
            uint8_t sc = btron_kbd_get_scancode();
            if (sc == 0xE0) {
                s_ps2_extended = 1;
            } else if (sc == 0x2A || sc == 0x36 || sc == 0x70) {
                shift = 1;
            } else if (sc == 0xAA || sc == 0xB6 || sc == 0xF0) {
                shift = 0;
            } else {
                int is_release = (sc & 0x80) ? 1 : 0;
                uint8_t clean_sc = sc & 0x7F;
                uint32_t b_key = ps2_scancode_to_btron_key(clean_sc, s_ps2_extended, shift);
                s_ps2_extended = 0;

                if (b_key != 0) {
                    ev.type = is_release ? EV_KEY_UP : EV_KEY_DOWN;
                    ev.key = b_key;
                    ev.pos.x = mouse_x;
                    ev.pos.y = mouse_y;
                    ev.data = shift ? (VW)(uintptr_t)BTRON_KMOD_LSHIFT : (VW)0;
                    snd_evt(&ev);
                    need_redraw = 1;
                }
            }
        }

        if (uart_has_char()) {
            char uc = uart_getc();
            if (uc == 0x1B) {
                /* ANSI escape sequence check (e.g. \x1B [ A for Up arrow) */
                uint32_t uart_k = BTRON_KEY_ESCAPE;
                if (uart_has_char()) {
                    char c2 = uart_getc();
                    if (c2 == '[') {
                        if (uart_has_char()) {
                            char c3 = uart_getc();
                            if (c3 == 'A') uart_k = BTRON_KEY_UP;
                            else if (c3 == 'B') uart_k = BTRON_KEY_DOWN;
                            else if (c3 == 'C') uart_k = BTRON_KEY_RIGHT;
                            else if (c3 == 'D') uart_k = BTRON_KEY_LEFT;
                        }
                    }
                }
                ev.type = EV_KEY_DOWN;
                ev.key = uart_k;
                ev.pos.x = mouse_x;
                ev.pos.y = mouse_y;
                ev.data = (VW)0;
                snd_evt(&ev);
                need_redraw = 1;
            } else if (uc) {
                if (uc == '\r') uc = '\n';
                ev.type = EV_KEY_DOWN;
                ev.key = (uc == '\n') ? BTRON_KEY_RETURN : (UW)(uint8_t)uc;
                ev.pos.x = mouse_x;
                ev.pos.y = mouse_y;
                ev.data = (VW)0;
                snd_evt(&ev);
                need_redraw = 1;
            }
        }

        while (get_evt(&ev, 0) == E_OK) {
            workbench_process_event(dt->screen, &ev);
            need_redraw = 1;
        }

        orchestra_ui_tick();
        cassette_service();
        if (b_installer_ui_tick) b_installer_ui_tick();

#if defined(BTRON_UEFI_TARGET)
        /* Dispatch tasks in the UEFI scheduler (Local APIC timer driven) */
        btron_scheduler_dispatch();
#endif

        /* Commit any pending window resize once per frame.
         * wnd_mgr_handle_event(MOUSE_MOVE) only accumulates pending_w/h;
         * flush_resize() is what calls rsz_wnd() and makes the window
         * actually change size.  Without this the resize grip is a no-op. */
        if (wnd_mgr_flush_resize()) {
            need_redraw = 1;
        }

        if (wnd_any_image_invalid()) {
            need_redraw = 1;
        }

        if (need_redraw) {
            workbench_render(dt->screen, 1024, 768);
#if !defined(BTRON_PC98_TARGET)
            if (g_virtio_gpu.is_active) {
                virtio_gpu_pci_flush_all();
            }
#endif
            if (g_vesa.framebuffer && g_vesa.framebuffer != (uint32_t *)s_desktop_backbuffer) {
                memcpy((void *)g_vesa.framebuffer, s_desktop_backbuffer, 1024 * 768 * sizeof(COLOR));
            }
        }

        for (volatile int d = 0; d < 10000; d++) {
            __asm__ volatile("pause");
        }
    }

    while (get_wnd_list()) {
        cls_wnd(get_wnd_list());
    }

    vesa_restore_text();
    extern int g_cursor_in_backbuffer;
    g_cursor_in_backbuffer = 0;

#if !defined(BTRON_PC98_TARGET)
    /* Disable PS/2 mouse interface and drain controller buffers to return cleanly to text mode */
    outb(0x64, 0xA7);
    for (int d = 0; d < 32; d++) {
        if (inb(0x64) & 0x01) {
            (void)inb(0x60);
        }
    }
#endif
    render_btron_text_desktop(active_cores);
    kprint("\n[B-SYSTEM] Returned from VESA Desktop to Console Shell.\n\n", 0x0A);
}

static void boot_shell_out(const char *line, COLOR col, void *ud) {
    (void)ud;
    uint8_t attr = 0x07;
    if (col == COLOR_CYAN) attr = 0x0B;
    else if (col == COLOR_GREEN) attr = 0x0A;
    else if (col == COLOR_YELLOW) attr = 0x0E;
    else if (col == COLOR_RED) attr = 0x0C;
    else if (col == COLOR_WHITE) attr = 0x0F;
    kprint(line, attr);
    kprint("\n", attr);
}

static int s_boot_active_cores = 1;
static void boot_gui_launcher_callback(void *ctx) {
    (void)ctx;
    launch_vesa_desktop_session(s_boot_active_cores);
}

static void run_btron_shell(int active_cores) {
    s_boot_active_cores = active_cores;
    btron_register_gui_launcher(boot_gui_launcher_callback, NULL);

    /* Drain any leftover key from bootloader */
    while (btron_kbd_has_key()) (void)btron_kbd_get_scancode();
    while (uart_has_char()) (void)uart_getc();

    render_btron_text_desktop(active_cores);

    kprint("\n==========================================================\n", 0x0A);
#if defined(BTRON_PC98_TARGET)
    kprint(" BTRON3 3.20 NEC PC-9801/PC-9821 Kernel (Awe Morris Engine)\n", 0x0A);
    kprint(" PC-98 µITRON 3.0 Scheduler  — 1 Core (i386 ISA Planar)\n", 0x0E);
#else
    kprint(" BTRON3 3.20 UEFI Workstation Kernel (Kota Uchida Engine)\n", 0x0A);
    kprint(" SMP Multi-Core Scheduler Active — 4 Cores Live\n", 0x0E);
#endif
    kprint(" Type 'desktop' or 'startx' to launch VESA 1024x768 GUI!\n", 0x0B);
    kprint(" Commands: ps, mem, ski, ver, desktop, startx, clear, help\n", 0x07);
    kprint("==========================================================\n\n", 0x0A);

    kprint("btron3# ", 0x0F);

    char cmd_buf[64];
    int  cmd_len = 0;
    int  shift = 0;

    for (;;) {
        if (g_reboot_requested) {
            break;
        }
        char ch = 0;

        if (ps2_has_key()) {
            uint8_t sc = ps2_get_scancode();
            if (sc == 0x2A || sc == 0x36) {
                shift = 1;
            } else if (sc == 0xAA || sc == 0xB6) {
                shift = 0;
            } else if (!(sc & 0x80)) {
                ch = ps2_scancode_to_ascii(sc, shift);
            }
        }

        if (!ch && uart_has_char()) {
            ch = uart_getc();
            if (ch == '\r') ch = '\n';
        }

        if (ch) {
            if (ch == '\n') {
                kprint("\n", 0x07);
                cmd_buf[cmd_len] = '\0';

                if (strcmp_k(cmd_buf, "clear") == 0) {
                    render_btron_text_desktop(active_cores);
                } else if (strcmp_k(cmd_buf, "desktop") == 0 || strcmp_k(cmd_buf, "startx") == 0 || strcmp_k(cmd_buf, "gui") == 0) {
                    launch_vesa_desktop_session(active_cores);
                } else if (strcmp_k(cmd_buf, "reboot") == 0 || strcmp_k(cmd_buf, "restart") == 0) {
                    btron_core_reboot();
                } else {
                    shell_execute_cmd(cmd_buf, boot_shell_out, NULL, NULL);
                }

                if (g_reboot_requested) {
                    break;
                }

                cmd_len = 0;
                kprint("btron3# ", 0x0F);
            } else if (ch == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    kprint("\b \b", 0x07);
                }
            } else if (cmd_len < 60 && ch >= 32 && ch <= 126) {
                cmd_buf[cmd_len++] = ch;
                char echo[2] = {ch, '\0'};
                kprint(echo, 0x0F);
            }
        }

        for (volatile int d = 0; d < 2000; d++) {
            __asm__ volatile("pause");
        }
    }
}

static uint8_t s_boot_stack[65536] __attribute__((aligned(16)));

void kernel_main(void) {
    uart_init();
    btron_kbd_init();

    btron_core_banner();

    /* CPU / SMP / IRQ — core_smp.c (UEFI) or core_pc98.c (PC-98) */
    btron_core_init();

    /* Memory map — core_smp.c (UEFI) or core_pc98.c (PC-98) */
    uart_puts_raw("\r\n");
    btron_core_mem_log();

    /* Storage / HFDS — core_smp.c (UEFI) or core_pc98.c (PC-98) */
    uart_puts_raw("\r\n");
    btron_core_hfds_log();
    btron_storage_probe_and_mount();

    /* Graphics — VirtIO-GPU & VESA (real PCI BAR0 scan) */
    uart_puts_raw("\r\n");
#if !defined(BTRON_PC98_TARGET)
    virtio_gpu_pci_probe();
#endif
    vesa_probe_log();



    /* ── Phase 6.5: Multiboot Cmdline & Installer Detection ────────── */
    if (g_multiboot_magic == 0x2BADB002 && g_multiboot_info) {
        uint32_t *mbi = (uint32_t *)g_multiboot_info;
        uint32_t flags = mbi[0];
        if (flags & (1 << 2)) {
            const char *cmd = (const char *)(uintptr_t)mbi[4];
            if (cmd) {
                uart_puts_raw("[BOOT] Multiboot cmdline: ");
                uart_puts_raw(cmd);
                uart_puts_raw("\r\n");
                if (strstr_k(cmd, "bootmode=installer") || strstr_k(cmd, "installer")) {
                    uart_puts_raw("[BOOT] Installer mode detected via cmdline. Bypassing console shell.\r\n");
                    g_bootmode_installer = 1;
                }
                if (strstr_k(cmd, "slice_boot=1")) {
                    uart_puts_raw("[BOOT] Direct .vol slice granularity enabled via cmdline (slice_boot=1).\r\n");
                    s_slice_boot_enabled = 1;
                }
            }
        }
    }

    for (;;) {
        g_reboot_requested = 0;
        s_boot_triggered = 0;

        ski_init_targets();

        if (g_bootmode_installer) {
            for (int t = 0; t < s_target_count; t++) {
                if (s_targets[t].is_installer) {
                    s_selected = t;
                    break;
                }
            }
            s_boot_triggered = 1;
            g_bootmode_installer = 0;
        }

        /* ── Phase 7: Ski Bootloader Menu ───────────────────────────────── */
        if (!s_boot_triggered) {
            render_ski_menu();

            uart_puts_raw("==========================================================\r\n");
            uart_puts_raw(" Ski Bootloader Active — Multi-OS Boot Manager\r\n");
            uart_puts_raw(" Controls: [W/S] or [Up/Down] Navigate, [+/-] Cores, [Enter] Boot\r\n");
            uart_puts_raw("==========================================================\r\n");
            uart_send_menu_update();

            uint8_t prev_scancode = 0;

            while (!s_boot_triggered) {
                int state_changed = 0;

                if (btron_kbd_has_key()) {
                    uint8_t sc = btron_kbd_get_scancode();
                    if (sc != prev_scancode) {
                        prev_scancode = sc;
                        if (sc == 0x11 || sc == 0x48 || sc == 0x3A) {
                            s_selected = (s_selected + s_target_count - 1) % s_target_count;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc == 0x1F || sc == 0x50 || sc == 0x3D) {
                            s_selected = (s_selected + 1) % s_target_count;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc == 0x49) { /* Page Up */
                            s_selected = (s_selected >= SKI_VISIBLE_ITEMS) ? s_selected - SKI_VISIBLE_ITEMS : 0;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc == 0x51) { /* Page Down */
                            s_selected = (s_selected + SKI_VISIBLE_ITEMS < s_target_count) ? s_selected + SKI_VISIBLE_ITEMS : s_target_count - 1;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc == 0x47) { /* Home */
                            s_selected = 0;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc == 0x4F) { /* End */
                            s_selected = s_target_count - 1;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc >= 0x02 && sc < 0x02 + s_target_count) {
                            s_selected = sc - 0x02;
                            ski_update_scroll();
                            state_changed = 1;
                        } else if (sc == 0x4E || sc == 0x0D || sc == 0x0B) {
                            if (s_targets[s_selected].cores < 16) s_targets[s_selected].cores++;
                            state_changed = 1;
                        } else if (sc == 0x4A || sc == 0x0C) {
                            if (s_targets[s_selected].cores > 1) s_targets[s_selected].cores--;
                            state_changed = 1;
                        } else if (sc == 0x1C || sc == 0x39 || sc == 0x34) {
                            s_boot_triggered = 1;
                        }
                    }
                } else {
                    prev_scancode = 0;
                }

                if (uart_has_char()) {
                    char uc = uart_getc();
                    if (uc == 'w' || uc == 'W' || uc == 'k') {
                        s_selected = (s_selected + s_target_count - 1) % s_target_count;
                        ski_update_scroll();
                        state_changed = 1;
                    } else if (uc == 's' || uc == 'S' || uc == 'j') {
                        s_selected = (s_selected + 1) % s_target_count;
                        ski_update_scroll();
                        state_changed = 1;
                    } else if (uc >= '1' && uc < '1' + s_target_count) {
                        s_selected = uc - '1';
                        ski_update_scroll();
                        state_changed = 1;
                    } else if (uc == '+' || uc == '=') {
                        if (s_targets[s_selected].cores < 16) s_targets[s_selected].cores++;
                        state_changed = 1;
                    } else if (uc == '-' || uc == '_') {
                        if (s_targets[s_selected].cores > 1) s_targets[s_selected].cores--;
                        state_changed = 1;
                    } else if (uc == '\r' || uc == '\n' || uc == ' ') {
                        s_boot_triggered = 1;
                    } else if (uc == 0x1B) {
                        char c2 = uart_getc();
                        if (c2 == '[') {
                            char c3 = uart_getc();
                            if (c3 == 'A') {
                                s_selected = (s_selected + s_target_count - 1) % s_target_count;
                                ski_update_scroll();
                                state_changed = 1;
                            } else if (c3 == 'B') {
                                s_selected = (s_selected + 1) % s_target_count;
                                ski_update_scroll();
                                state_changed = 1;
                            } else if (c3 == '5') {
                                s_selected = (s_selected >= SKI_VISIBLE_ITEMS) ? s_selected - SKI_VISIBLE_ITEMS : 0;
                                ski_update_scroll();
                                state_changed = 1;
                            } else if (c3 == '6') {
                                s_selected = (s_selected + SKI_VISIBLE_ITEMS < s_target_count) ? s_selected + SKI_VISIBLE_ITEMS : s_target_count - 1;
                                ski_update_scroll();
                                state_changed = 1;
                            } else if (c3 == 'H') {
                                s_selected = 0;
                                ski_update_scroll();
                                state_changed = 1;
                            } else if (c3 == 'F') {
                                s_selected = s_target_count - 1;
                                ski_update_scroll();
                                state_changed = 1;
                            }
                        }
                    }
                }

                if (state_changed) {
                    render_ski_menu();
                    uart_send_menu_update();
                }

                for (volatile int d = 0; d < 10000; d++) {
                    __asm__ volatile("pause");
                }
            }
        }

        if (!s_targets[s_selected].is_installer && s_targets[s_selected].dev_idx >= 0 && !s_targets[s_selected].is_bootable) {
            uart_puts_raw("[BOOT] Selected volume is not a B-System volume; returning to Ski menu.\r\n");
            continue;
        }

        if (s_targets[s_selected].is_installer) {
            uart_puts_raw("[BOOT] Installer mode selected. Launching VESA desktop directly.\r\n");
            launch_vesa_desktop_session(s_targets[s_selected].cores);
        } else {
            if (s_targets[s_selected].dev_idx >= 0 && s_targets[s_selected].part_idx >= 0) {
                BlkDev *vdev = blk_virtio_create(s_targets[s_selected].dev_idx);
                if (vdev) {
                    /* Reset sibling mounts so switching disks in Ski never leaves stale volumes */
                    g_sys_vol = NULL;
                    g_anders_vol = NULL;
                    ER er = blk_mount_disk_partitions(vdev, s_targets[s_selected].part_idx);
                    if (er > 0) {
                        uart_puts_raw("[BOOT] Mounted boot root /SYS and sibling partitions from VirtIO disk ");
                        uart_putc((char)('0' + s_targets[s_selected].dev_idx));
                        uart_puts_raw("\r\n");
                    } else {
                        uart_log_er("BOOT", "selected partition mount", er);
                    }
                }
            }
            run_btron_shell(s_targets[s_selected].cores);
        }
    }
}

__attribute__((naked, weak)) void _start(void) {
#if defined(__x86_64__)
    __asm__ volatile(
        "movq %0, %%rsp\n"
        "xorq %%rbp, %%rbp\n"
        "movq %%cr0, %%rax\n"
        "andq $~0x04, %%rax\n"
        "orq $0x02, %%rax\n"
        "movq %%rax, %%cr0\n"
        "movq %%cr4, %%rax\n"
        "orq $0x600, %%rax\n"
        "movq %%rax, %%cr4\n"
        "call kernel_main\n"
        "1: hlt\n"
        "jmp 1b\n"
        :
        : "r"(s_boot_stack + sizeof(s_boot_stack))
    );
#elif defined(__i386__)
    __asm__ volatile(
        "movl %%eax, g_multiboot_magic\n"
        "movl %%ebx, g_multiboot_info\n"
        "movl %0, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "movl %%cr0, %%eax\n"
        "andl $~0x04, %%eax\n"
        "orl $0x02, %%eax\n"
        "movl %%eax, %%cr0\n"
        "movl %%cr4, %%eax\n"
        "orl $0x600, %%eax\n"
        "movl %%eax, %%cr4\n"
        "call kernel_main\n"
        "1: hlt\n"
        "jmp 1b\n"
        :
        : "r"(s_boot_stack + sizeof(s_boot_stack))
        : "memory"
    );
#else
    kernel_main();
#endif
}

void multiboot_main(void) {
    _start();
}
