/*
 * sim_rom.c — Pure C99 Headless Cartridge ROM Runner for MSX turbo R SoC.
 *
 * Capabilities:
 *   1. Loads C-BIOS turboR firmware (cbios_main_msxtr.rom + cbios_sub.rom).
 *   2. Loads plain (16KB/32KB/48KB) or ASCII16 MegaROM cartridges from ./assets/msx/.
 *   3. Executes mkTopMSXTurboR SoC with cycle-accurate memory arbiter servicing.
 *   4. Monitors cold boot sequence, slot search, and cartridge initialization entry.
 *   5. Captures real-time RGB565 video beam output and dumps frames to PPM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "vl_bridge_p6.h"
#include "mem_model.h"

// ── Physical Address Constants ──────────────────────────────────────────────
#define ADDR_BIOS        0x000000u // 32 KB Main BIOS
#define ADDR_SUBROM      0x008000u // 16 KB Sub-ROM / BASIC
#define ADDR_RAM         0x010000u // Base 512 KB RAM (Slot 3)
#define ADDR_SHADOW_DRAM 0x080000u // turbo R fast DRAM shadow
#define ADDR_CART_A      0x100000u // Primary Slot 1 Cartridge A
#define ADDR_VRAM        0x300000u // 128 KB VRAM

// ── Cartridge Mapper State ──────────────────────────────────────────────────
typedef enum {
    MAP_PLAIN,
    MAP_ASCII16
} cart_type_t;

static cart_type_t g_cart_type = MAP_PLAIN;
static uint8_t    *g_cart_buf  = NULL;
static size_t      g_cart_size = 0;
static uint32_t    g_num_16k   = 1;
static uint8_t     g_bank1     = 0; // Page 1 (0x4000..0x7FFF)
static uint8_t     g_bank2     = 0; // Page 2 (0x8000..0xBFFF)

/* M5..M1 packed the same way as v9938_mode(): R#0[3:1], R#1[3], R#1[4]. */
static void print_vdp_mode(void *vl, const char *tag)
{
    uint8_t r0 = vl_p6_vdp_peek_reg(vl, 0);
    uint8_t r1 = vl_p6_vdp_peek_reg(vl, 1);
    uint8_t r2 = vl_p6_vdp_peek_reg(vl, 2);
    uint8_t r3 = vl_p6_vdp_peek_reg(vl, 3);
    uint8_t r4 = vl_p6_vdp_peek_reg(vl, 4);
    uint8_t r7 = vl_p6_vdp_peek_reg(vl, 7);
    uint8_t r8 = vl_p6_vdp_peek_reg(vl, 8);
    uint8_t r9 = vl_p6_vdp_peek_reg(vl, 9);
    uint8_t r20 = vl_p6_vdp_peek_reg(vl, 20);
    uint8_t r23 = vl_p6_vdp_peek_reg(vl, 23);
    uint8_t r25 = vl_p6_vdp_peek_reg(vl, 25);
    uint8_t mode = (uint8_t)((((r0 & 0x0Eu) << 1) |
                             ((r1 & 0x08u) >> 2) |
                             ((r1 & 0x10u) >> 4)) & 0x1Fu);
    printf("  [VDP %s] R0=%02X R1=%02X R2=%02X R3=%02X R4=%02X R7=%02X R8=%02X R9=%02X R20=%02X R23=%02X R25=%02X mode=%02X disp=%u\n",
           tag, r0, r1, r2, r3, r4, r7, r8, r9, r20, r23, r25, mode, (unsigned)((r1 >> 6) & 1));
}
static uint16_t    g_cart_init = 0;
static bool        g_cart_booted = false;

/* MSX_KEY=space|a|both. Pressed only after the cartridge has run for two
 * frames, so C-BIOS's boot HALT is left alone. Each press is one master
 * clock in place of vl_p6_step, so a memory request from that clock is
 * still serviced. SPACE is row 8 col 0 (0xFE). A is row 2 col 6 (0xBF). */
static int g_key_space = 0;
static int g_key_a = 0;
static int g_key_arm = 0;
static int g_space_sent = 0;
static int g_a_sent = 0;
static int g_keys_down = 0;
static uint32_t g_kbd_strobe_r8 = 0;
static uint32_t g_kbd_strobe_r2 = 0;
static int g_kbd_saw_r8 = 0;
static int g_kbd_saw_r2 = 0;

static void parse_msx_key(void)
{
    const char *k = getenv("MSX_KEY");
    if (!k || !k[0]) return;
    if (!strcmp(k, "space") || !strcmp(k, "SPACE")) g_key_space = 1;
    else if (!strcmp(k, "a") || !strcmp(k, "A")) g_key_a = 1;
    else if (!strcmp(k, "both") || !strcmp(k, "BOTH")) {
        g_key_space = 1;
        g_key_a = 1;
    } else {
        fprintf(stderr, "MSX_KEY=%s ignored (space, a, or both)\n", k);
    }
}

/* A 48 KiB image whose "AB" header sits at file+0x4000, with INIT inside
 * that page, occupies CPU pages 0..2. C-BIOS looks for the header at
 * $4000, so the file's first byte must land at CPU address 0. */
static int plain_48k_from_0000(void)
{
    uint16_t init;
    if (g_cart_size <= 32768u || g_cart_size > 49152u) return 0;
    if (g_cart_size < 0x4004u) return 0;
    if (g_cart_buf[0x4000] != (uint8_t)'A' || g_cart_buf[0x4001] != (uint8_t)'B')
        return 0;
    init = (uint16_t)(g_cart_buf[0x4002] | ((uint16_t)g_cart_buf[0x4003] << 8));
    return init >= 0x4000u && init < 0x8000u;
}

// ── Framebuffer Capture ─────────────────────────────────────────────────────
#define FB_W 256
#define FB_H 212
static uint16_t g_fb[FB_W * FB_H];

// ── Global Memory Model ─────────────────────────────────────────────────────
static mem_model_t g_mem;

/* Last physical transactions. The Bad Apple break executes from mapper RAM,
 * so the cartridge file at the same CPU address is the wrong image. */
typedef struct { uint32_t addr; uint8_t data; uint8_t wr; uint16_t pc; } mem_tr_t;
static mem_tr_t g_mring[64];
static uint32_t g_mring_n = 0;

static int g_do_trace = 0;

static void note_mem(void *vl, uint32_t addr, uint8_t data, int wr)
{
    /* Always keep the last transactions. PC is filled in only for the hunt. */
    g_mring[g_mring_n & 63] = (mem_tr_t){ addr, data, (uint8_t)wr, 0 };
    g_mring_n++;
    /* PC tagging is only needed once the cartridge is in the trace.
     * Doing it on every fetch from reset dominates the run. */
    if (!(g_do_trace && g_cart_booted) && !getenv("MSX_HUNT")) return;
    trace_regs_t r;
    vl_p6_get_regs(vl, &r);
    g_mring[(g_mring_n - 1) & 63].pc = r.pc;
}

/* MSX_TRACE records every retirement, including the steps service_mem
 * takes while a request is in flight. The outer loop only samples the
 * last step, so the instruction that enters the $4849 data blob was
 * invisible. Stop on that entry and dump the ring. */
static int g_trace_stop = 0;
static uint16_t g_trace_prev_pc = 0xFFFF;
static int g_mile = 0;

typedef struct {
    uint16_t pc, sp, hl, ix, bc, de;
    uint8_t a, ppi;
} rsnap_t;
static rsnap_t g_rsnap[64];
static uint32_t g_rsnap_n = 0;

static void trace_dump(const char *why)
{
    uint32_t n = g_rsnap_n < 64 ? g_rsnap_n : 64;
    uint32_t start = g_rsnap_n - n;
    printf("  [TRACE] %s\n", why);
    for (uint32_t i = 0; i < n; i++) {
        rsnap_t *s = &g_rsnap[(start + i) & 63];
        printf("  R-%02u PC=%04X SP=%04X A=%02X HL=%04X DE=%04X BC=%04X PPI=%02X\n",
               n - 1 - i, s->pc, s->sp, s->a, s->hl, s->de, s->bc, s->ppi);
    }
    if (n >= 1) {
        rsnap_t *cur = &g_rsnap[(g_rsnap_n - 1) & 63];
        uint16_t prev_pc = (n >= 2) ? g_rsnap[(g_rsnap_n - 2) & 63].pc : 0;
        uint16_t pcs[2] = { prev_pc, cur->pc };
        uint16_t sps[2] = { (n >= 2) ? g_rsnap[(g_rsnap_n - 2) & 63].sp : cur->sp, cur->sp };
        for (int k = 0; k < 2; k++) {
            uint16_t pc = pcs[k];
            uint16_t off = (uint16_t)(pc & 0x3FFF);
            printf("  [OPC] PC=%04X", pc);
            for (int seg = 0; seg < 8; seg++) {
                uint32_t phys = ADDR_RAM + (uint32_t)seg * 0x4000u + off;
                printf(" s%02X:", seg);
                for (int i = 0; i < 4; i++)
                    printf("%02X", mem_model_read(&g_mem, phys + (uint32_t)i));
            }
            printf("\n");
            uint16_t sp = sps[k];
            off = (uint16_t)(sp & 0x3FFF);
            printf("  [STK] SP=%04X", sp);
            for (int seg = 0; seg < 8; seg++) {
                uint32_t phys = ADDR_RAM + (uint32_t)seg * 0x4000u + off;
                uint8_t lo = mem_model_read(&g_mem, phys);
                uint8_t hi = mem_model_read(&g_mem, phys + 1);
                printf(" s%02X:%02X%02X", seg, hi, lo);
            }
            printf("\n");
        }
    }
    {
        uint32_t mn = g_mring_n < 40 ? g_mring_n : 40;
        uint32_t ms = g_mring_n - mn;
        for (uint32_t i = 0; i < mn; i++) {
            mem_tr_t *t = &g_mring[(ms + i) & 63];
            printf("  MEM %s phys=%06X data=%02X PC=%04X\n",
                   t->wr ? "W" : "R", t->addr, t->data, t->pc);
        }
    }
    fflush(stdout);
}

static void trace_step(void *vl)
{
    if (!g_do_trace || !g_cart_booted || g_trace_stop) return;
    if (!vl_p6_inst_retired(vl)) return;
    trace_regs_t r;
    vl_p6_get_regs(vl, &r);
    uint8_t ppi = vl_p6_ppi_read_a(vl);
    uint16_t hl = (uint16_t)(((uint16_t)r.h << 8) | r.l);
    uint16_t bc = (uint16_t)(((uint16_t)r.b << 8) | r.c);
    uint16_t de = (uint16_t)(((uint16_t)r.d << 8) | r.e);
    g_rsnap[g_rsnap_n & 63] = (rsnap_t){ r.pc, r.sp, hl, r.ix, bc, de, r.a, ppi };
    g_rsnap_n++;

    int mile = (r.pc == 0x6016u || r.pc == 0x6277u || r.pc == 0x654Au ||
                r.pc == 0x656Eu || r.pc == 0x6594u || r.pc == 0xC092u ||
                r.pc == 0xC097u || r.pc == 0xC0A9u || r.pc == 0xC0D1u);
    if (mile && g_mile < 60) {
        printf("  [MILE] PC=%04X prev=%04X SP=%04X A=%02X HL=%04X IX=%04X PPI=%02X banks=%02X/%02X\n",
               r.pc, g_trace_prev_pc, r.sp, r.a, hl, r.ix, ppi, g_bank1, g_bank2);
        g_mile++;
        fflush(stdout);
    }

    int page1_ram = ((ppi & 0x0Cu) == 0x0Cu);
    int in_blob = (r.pc >= 0x4800u && r.pc < 0x4900u);
    int prev_in = (g_trace_prev_pc >= 0x4800u && g_trace_prev_pc < 0x4900u);
    if ((page1_ram && in_blob && !prev_in && g_rsnap_n > 8) || r.pc == 0x36B5u) {
        char why[96];
        snprintf(why, sizeof(why), "stop PC=%04X prev=%04X PPI=%02X banks=%02X/%02X",
                 r.pc, g_trace_prev_pc, ppi, g_bank1, g_bank2);
        trace_dump(why);
        g_trace_stop = 1;
    }
    g_trace_prev_pc = r.pc;
}

/* Screen 8 byte is GGGRRRBB. Same expansion as v9938.c. */
static uint16_t rgb332_to_rgb565(uint8_t c)
{
    uint8_t g = (uint8_t)(((c >> 5) & 7) * 255 / 7);
    uint8_t r = (uint8_t)(((c >> 2) & 7) * 255 / 7);
    uint8_t b = (uint8_t)((c & 3) * 255 / 3);
    return (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3));
}

/* The beam visits 168 positions across 256 pixels. Screen 8 detail then
 * smears. The bytes already sit in VRAM at the address the scanner uses,
 * so paint all 256 of them. */
static void render_screen8(void *vl)
{
    uint8_t r1 = vl_p6_vdp_peek_reg(vl, 1);
    uint8_t r2 = vl_p6_vdp_peek_reg(vl, 2);
    uint8_t r9 = vl_p6_vdp_peek_reg(vl, 9);
    uint8_t r20 = vl_p6_vdp_peek_reg(vl, 20);
    uint8_t r23 = vl_p6_vdp_peek_reg(vl, 23);
    int lines = (r9 & 0x80) ? 212 : 192;
    uint32_t page = (r2 & 0x20u) ? 0x10000u : 0u;
    int epal = (r20 & 0x10) != 0;
    uint16_t pal[256];
    if ((r1 & 0x40) == 0) return;
    if (epal) {
        for (int i = 0; i < 256; i++)
            pal[i] = vl_p6_vdp_peek_pal256(vl, (uint8_t)i);
    }
    for (int y = 0; y < lines && y < FB_H; y++) {
        uint32_t sy = ((uint32_t)y + r23) & 0xFFu;
        uint32_t base = ADDR_VRAM + page + sy * 256u;
        for (int x = 0; x < FB_W; x++) {
            uint8_t c = mem_model_read(&g_mem, base + (uint32_t)x);
            g_fb[y * FB_W + x] = epal ? pal[c] : rgb332_to_rgb565(c);
        }
    }
}

// ── Intercepted Physical Memory Service ─────────────────────────────────────
static void service_mem(void *vl)
{
    vl_mem_req_t req;
    while (vl_p6_has_mem_req(vl)) {
        if (vl_p6_get_mem_req(vl, &req)) {
            if (req.write) {
                // Cartridge ROM is not RAM. CHKRAM probes from 0xFF00 downward
                // and would overwrite a plain image if these writes stuck.
                // ASCII16 bank latches are the only writes that take effect.
                bool cart = (req.addr >= 0x100000u && req.addr < 0x110000u);
                if (cart && g_cart_type == MAP_ASCII16) {
                    uint16_t cart_reg = (uint16_t)(req.addr & 0xFFFF);
                    if (cart_reg >= 0x6000 && cart_reg <= 0x67FF) {
                        g_bank1 = req.data % g_num_16k;
                    } else if (cart_reg >= 0x7000 && cart_reg <= 0x77FF) {
                        g_bank2 = req.data % g_num_16k;
                    }
                }
                if (!cart) mem_model_write(&g_mem, req.addr, req.data);
                note_mem(vl, req.addr, req.data, 1);
                vl_p6_put_mem_resp(vl, req.tag, req.data, true);
            } else {
                uint8_t d;
                // Intercept Cartridge Reads for ASCII16 (Slot 1)
                if (g_cart_type == MAP_ASCII16 && req.addr >= 0x100000 && req.addr < 0x200000) {
                    uint16_t vaddr = req.addr & 0xFFFF;
                    if (vaddr >= 0x4000 && vaddr <= 0x7FFF) {
                        uint32_t offset = (g_bank1 * 0x4000) + (vaddr - 0x4000);
                        d = (offset < g_cart_size) ? g_cart_buf[offset] : 0xFF;
                    } else if (vaddr >= 0x8000 && vaddr <= 0xBFFF) {
                        uint32_t offset = (g_bank2 * 0x4000) + (vaddr - 0x8000);
                        d = (offset < g_cart_size) ? g_cart_buf[offset] : 0xFF;
                    } else {
                        d = mem_model_read(&g_mem, req.addr);
                    }
                } else {
                    d = mem_model_read(&g_mem, req.addr);
                }
                note_mem(vl, req.addr, d, 0);
                vl_p6_put_mem_resp(vl, req.tag, d, true);
            }
        }
        vl_p6_step(vl);
        trace_step(vl);
    }
}

// ── Save Framebuffer to PPM ─────────────────────────────────────────────────
static void dump_frame_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", path);
        return;
    }
    fprintf(fp, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint16_t c = g_fb[i];
        // Unpack RGB565 -> RGB888
        uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
        uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
        uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
        fputc(r, fp);
        fputc(g, fp);
        fputc(b, fp);
    }
    fclose(fp);
    printf("  [FRAME CAPTURE] Successfully dumped %dx%d RGB frame to %s\n", FB_W, FB_H, path);
}

static const char *find_file(const char *rel_path)
{
    static char buf[1024];
    // Check direct
    FILE *f = fopen(rel_path, "rb");
    if (f) { fclose(f); return rel_path; }

    /* bx8/sim/verilator sits five levels under the depot root. */
    snprintf(buf, sizeof(buf), "../../../../../%s", rel_path);
    f = fopen(buf, "rb");
    if (f) { fclose(f); return buf; }

    // Check absolute depot root
    snprintf(buf, sizeof(buf), "/Users/tonpa/depot/bitedits/btron/%s", rel_path);
    f = fopen(buf, "rb");
    if (f) { fclose(f); return buf; }

    return rel_path;
}
int main(int argc, char **argv)
{
    const char *cart_path = NULL;
    const char *bios_path = "third_party/cbios-nextor_FDD-and-turboR/cbios/derived/bin/cbios_main_msxtr.rom";
    const char *sub_path  = "third_party/cbios-nextor_FDD-and-turboR/cbios/derived/bin/cbios_sub.rom";
    const char *out_ppm   = "cart_frame.ppm";
    /* C-BIOS wait_b HALTs for 120 frames (~1.15e8 master cycles) before
     * it scans slots. Stop early once the cartridge is running. */
    uint64_t    max_cycles = 200000000ULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) {
            bios_path = argv[++i];
        } else if (!strcmp(argv[i], "--sub") && i + 1 < argc) {
            sub_path = argv[++i];
        } else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) {
            out_ppm = argv[++i];
        } else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) {
            max_cycles = strtoull(argv[++i], NULL, 0);
        } else if (argv[i][0] != '-') {
            cart_path = argv[i];
        }
    }

    if (!cart_path) {
        printf("Usage: %s <cartridge.rom> [--bios <bios.rom>] [--sub <sub.rom>] [--ppm <output.ppm>] [--cycles <N>]\n", argv[0]);
        return 1;
    }

    printf("================================================================\n");
    printf(" MSX turbo R FPGA: Headless Cartridge ROM Verification Runner\n");
    printf("================================================================\n");
    printf("Cartridge: %s\n", cart_path);
    printf("BIOS     : %s\n", bios_path);
    printf("Sub-ROM  : %s\n", sub_path);
    printf("Target   : %llu master cycles\n\n", max_cycles);

    // 1. Initialize physical memory model
    mem_model_init(&g_mem);

    // 2. Load C-BIOS main & sub ROMs
    const char *actual_bios = find_file(bios_path);
    if (!mem_model_load_rom(&g_mem, ADDR_BIOS, actual_bios)) {
        fprintf(stderr, "Error: Failed to load BIOS from %s\n", actual_bios);
        return 1;
    }
    printf("  [OK] Loaded BIOS into 0x%06X (32 KB)\n", ADDR_BIOS);

    const char *actual_sub = find_file(sub_path);
    if (!mem_model_load_rom(&g_mem, ADDR_SUBROM, actual_sub)) {
        fprintf(stderr, "Error: Failed to load Sub-ROM from %s\n", actual_sub);
        return 1;
    }
    printf("  [OK] Loaded Sub-ROM into 0x%06X (16 KB)\n", ADDR_SUBROM);

    // 3. Load Cartridge ROM
    const char *actual_cart = find_file(cart_path);
    FILE *fc = fopen(actual_cart, "rb");
    if (!fc) {
        fprintf(stderr, "Error: Failed to open cartridge %s\n", cart_path);
        return 1;
    }
    fseek(fc, 0, SEEK_END);
    g_cart_size = ftell(fc);
    fseek(fc, 0, SEEK_SET);

    g_cart_buf = (uint8_t *)malloc(g_cart_size);
    if (!g_cart_buf || fread(g_cart_buf, 1, g_cart_size, fc) != g_cart_size) {
        fprintf(stderr, "Error reading cartridge file\n");
        fclose(fc);
        return 1;
    }
    fclose(fc);

    g_num_16k = (uint32_t)(g_cart_size / 0x4000);
    if (g_num_16k == 0) g_num_16k = 1;

    // Inspect MSX Cartridge Header
    if (g_cart_buf[0] == 'A' && g_cart_buf[1] == 'B') {
        g_cart_init = (uint16_t)(g_cart_buf[2] | (g_cart_buf[3] << 8));
        printf("  [CARTRIDGE HEADER] Valid 'AB' signature detected. Init Vector: 0x%04X\n", g_cart_init);
    } else {
        printf("  [WARNING] 'AB' header not at offset 0 (Raw binary or custom loader)\n");
    }

    if (g_cart_size > 49152) {
        g_cart_type = MAP_ASCII16;
        printf("  [MAPPER] Detected MegaROM (%zu KB, %u banks of 16KB). Using ASCII16 controller.\n",
               g_cart_size / 1024, g_num_16k);
        // Preload initial banks (bank 0 in Page 1 and Page 2)
        mem_model_load_buf(&g_mem, ADDR_CART_A + 0x4000, g_cart_buf, 0x4000);
        mem_model_load_buf(&g_mem, ADDR_CART_A + 0x8000, g_cart_buf, 0x4000);
    } else if (g_cart_size <= 16384) {
        g_cart_type = MAP_PLAIN;
        printf("  [MAPPER] Detected Plain 16KB ROM. Mapped to Page 1 and mirrored to Page 2.\n");
        mem_model_load_buf(&g_mem, ADDR_CART_A + 0x4000, g_cart_buf, g_cart_size);
        mem_model_load_buf(&g_mem, ADDR_CART_A + 0x8000, g_cart_buf, g_cart_size);
    } else if (plain_48k_from_0000()) {
        uint16_t init = (uint16_t)(g_cart_buf[0x4002] | ((uint16_t)g_cart_buf[0x4003] << 8));
        g_cart_type = MAP_PLAIN;
        g_cart_init = init;
        printf("  [MAPPER] Plain 48KB image mapped from CPU page 0. Init Vector: 0x%04X\n",
               g_cart_init);
        mem_model_load_buf(&g_mem, ADDR_CART_A, g_cart_buf, g_cart_size);
    } else {
        g_cart_type = MAP_PLAIN;
        printf("  [MAPPER] Detected Plain %zuKB ROM. Mapped linearly from page 1.\n",
               g_cart_size / 1024);
        mem_model_load_buf(&g_mem, ADDR_CART_A + 0x4000, g_cart_buf, g_cart_size);
    }

    // 4. Instantiate Verilated SoC
    void *vl = vl_p6_create();
    vl_p6_reset(vl);
    printf("  [OK] Verilated SoC mkTopMSXTurboR initialized and reset.\n\n");

    // 5. Execution Loop
    printf("--- Beginning Execution ---\n");
    uint64_t cycles = 0;
    uint32_t retired_insts = 0;
    uint32_t frame_count = 0;
    uint32_t frames_after_boot = 0;
    const char *frames_env = getenv("MSX_FRAMES");
    uint32_t frames_limit = frames_env ? (uint32_t)atoi(frames_env) : 20;
    bool     prev_vsync = false;
    trace_regs_t regs;

    memset(g_fb, 0, sizeof(g_fb));

    g_do_trace = getenv("MSX_TRACE") && getenv("MSX_TRACE")[0];
    parse_msx_key();

    while (cycles < max_cycles && !g_trace_stop) {
        int injected = 0;
        if (g_key_arm) {
            if (g_key_space && !g_space_sent) {
                vl_p6_kbd_key_down(vl, 8, 0);
                g_space_sent = 1;
                injected = 1;
            } else if (g_key_a && !g_a_sent) {
                vl_p6_kbd_key_down(vl, 2, 6);
                g_a_sent = 1;
                injected = 1;
            }
            if ((!g_key_space || g_space_sent) && (!g_key_a || g_a_sent)) {
                g_key_arm = 0;
                g_keys_down = 1;
                vl_p6_get_regs(vl, &regs);
                printf("  [KBD] latched space=%d A=%d row8=%02X row2=%02X PC=%04X\n",
                       g_key_space, g_key_a,
                       vl_p6_kbd_peek_row(vl, 8),
                       vl_p6_kbd_peek_row(vl, 2),
                       regs.pc);
            }
        }
        if (!injected)
            vl_p6_step(vl);
        trace_step(vl);
        service_mem(vl);
        if (g_trace_stop) break;
        cycles++;

        // Track CPU Instruction Retirement
        if (vl_p6_inst_retired(vl)) {
            retired_insts++;
            vl_p6_get_regs(vl, &regs);
            if (retired_insts <= 24 || (retired_insts % 20000) == 0)
                printf("  RET %-7u PC=%04X SP=%04X\n", retired_insts, regs.pc, regs.sp);

            /* MSX_HUNT=1: after the cartridge is entered, keep a short
             * window and dump it when PC falls into the interrupt page
             * or SP leaves the stack RAM. That is the Bad Apple break. */
            if (g_cart_booted && getenv("MSX_HUNT") && getenv("MSX_HUNT")[0]) {
                static trace_regs_t ring[64];
                static uint32_t ring_n = 0;
                static int hunt = 0;
                ring[ring_n & 63] = regs;
                ring_n++;
                if (regs.pc >= 0x0038u && regs.pc <= 0x003Cu && hunt == 0) {
                    static uint32_t vecs = 0;
                    if (vecs < 8)
                        printf("  VEC %u ret %u PC=%04X SP=%04X IRQ=%d\n",
                               vecs, retired_insts, regs.pc, regs.sp,
                               vl_p6_vdp_irq(vl) ? 1 : 0);
                    vecs++;
                }
                /* The stack lives in Fxxx. Leaving that RAM is the break. */
                int hot = (regs.sp < 0xC000u);
                if (hot && hunt == 0) {
                    uint32_t n = ring_n < 64 ? ring_n : 64;
                    uint32_t start = ring_n - n;
                    printf("  [HUNT] trigger PC=%04X SP=%04X IX=%04X IY=%04X IRQ=%d banks=%02X/%02X at ret %u\n",
                           regs.pc, regs.sp, regs.ix, regs.iy,
                           vl_p6_vdp_irq(vl) ? 1 : 0, g_bank1, g_bank2, retired_insts);
                    {
                        uint32_t n = g_mring_n < 48 ? g_mring_n : 48;
                        uint32_t start = g_mring_n - n;
                        printf("  [HUNT] mem trail:\n");
                        for (uint32_t i = 0; i < n; i++) {
                            mem_tr_t *t = &g_mring[(start + i) & 63];
                            printf("  MEM %s phys=%06X data=%02X PC=%04X\n",
                                   t->wr ? "W" : "R", t->addr, t->data, t->pc);
                        }
                    }
                    for (uint32_t i = 0; i < n; i++) {
                        trace_regs_t *r = &ring[(start + i) & 63];
                        printf("  HUNT-%02u PC=%04X SP=%04X IX=%04X IY=%04X A=%02X HL=%02X%02X\n",
                               n - i, r->pc, r->sp, r->ix, r->iy, r->a, r->h, r->l);
                    }
                    hunt = 1;
                } else if (hunt > 0 && hunt < 400) {
                    printf("  HUNT+%03d PC=%04X SP=%04X A=%02X BC=%02X%02X HL=%02X%02X IRQ=%d\n",
                           hunt, regs.pc, regs.sp, regs.a, regs.b, regs.c, regs.h, regs.l,
                           vl_p6_vdp_irq(vl) ? 1 : 0);
                    hunt++;
                } else if (hunt >= 400) {
                    break;
                }
            }

            // Check for entry into cartridge execution space
            if (!g_cart_booted && regs.pc >= 0x4000 && regs.pc <= 0xBFFF) {
                // If it reached the init vector or cartridge page
                if (regs.pc == g_cart_init || (g_cart_init != 0 && abs((int)regs.pc - (int)g_cart_init) < 0x20)) {
                    printf("\n>>> [CARTRIDGE BOOT SUCCESS] PC=0x%04X entered cartridge code at cycle %llu! <<<\n\n",
                           regs.pc, cycles);
                    g_cart_booted = true;
                }
            }
        }

        // Active Display Video Sampling
        if (!vl_p6_video_blank(vl)) {
            uint16_t bx = vl_p6_video_beam_x(vl);
            uint8_t  by = vl_p6_video_beam_y(vl);
            if (bx < FB_W && by < FB_H) {
                /* The raster steps 168 times across 256 pixels, so beam_x
                 * skips. Paint the gap with this sample so the frame is solid.
                 * The last sample of a line is short of x=255; finish that
                 * line when y advances. */
                static int prev_x = -1, prev_y = -1;
                static uint16_t prev_color = 0;
                uint16_t color = vl_p6_video_rgb565(vl);
                if (prev_y >= 0 && prev_y != (int)by && prev_x >= 0 && prev_x < FB_W - 1) {
                    for (int x = prev_x + 1; x < FB_W; x++)
                        g_fb[prev_y * FB_W + x] = prev_color;
                }
                int x0 = (int)bx;
                if (prev_y == (int)by && prev_x >= 0 && prev_x < (int)bx)
                    x0 = prev_x + 1;
                for (int x = x0; x <= (int)bx; x++)
                    g_fb[by * FB_W + x] = color;
                prev_x = (int)bx;
                prev_y = (int)by;
                prev_color = color;
            }
        }

        // VSync Edge Detection (Frame boundary)
        bool vsync = vl_p6_video_vsync(vl);
        if (prev_vsync && !vsync) {
            frame_count++;
            if (g_cart_booted) frames_after_boot++;
            if (!g_keys_down && !g_key_arm &&
                g_cart_booted && frames_after_boot >= 2 &&
                (g_key_space || g_key_a))
                g_key_arm = 1;
            if (frame_count % 5 == 0) {
                vl_p6_get_regs(vl, &regs);
                printf("  [FRAME %02u] Cycle %-9llu | Ret: %-7u | PC=0x%04X SP=0x%04X Mode=%s TurboLED=%d\n",
                       frame_count, cycles, retired_insts, regs.pc, regs.sp,
                       vl_p6_is_r800(vl) ? "R800" : "Z80A",
                       vl_p6_led_turbo(vl));
                if (g_cart_booted) {
                    char tag[16];
                    snprintf(tag, sizeof(tag), "f%u", frame_count);
                    print_vdp_mode(vl, tag);
                }
            }
        }
        prev_vsync = vsync;

        if (g_keys_down) {
            uint8_t row = (uint8_t)(vl_p6_ppi_peek_pc(vl) & 0x0Fu);
            if (row == 8) {
                g_kbd_strobe_r8++;
                if (!g_kbd_saw_r8) {
                    trace_regs_t kr;
                    vl_p6_get_regs(vl, &kr);
                    printf("  [KBD] scan row 8 matrix=%02X PC=%04X\n",
                           vl_p6_kbd_peek_row(vl, 8), kr.pc);
                    g_kbd_saw_r8 = 1;
                }
            } else if (row == 2) {
                g_kbd_strobe_r2++;
                if (!g_kbd_saw_r2) {
                    trace_regs_t kr;
                    vl_p6_get_regs(vl, &kr);
                    printf("  [KBD] scan row 2 matrix=%02X PC=%04X\n",
                           vl_p6_kbd_peek_row(vl, 2), kr.pc);
                    g_kbd_saw_r2 = 1;
                }
            }
        }

        /* MSX_LOGO spends several frames copying VRAM after the init
         * vector. Keep two extra frames so the dumped picture is the
         * finished screen, not the first instruction of the cartridge. */
        if (g_cart_booted && frames_after_boot >= frames_limit) break;

        // Periodic heartbeat
        if (cycles % 2500000 == 0 && frame_count == 0) {
            vl_p6_get_regs(vl, &regs);
            printf("  [CYCLE %-9llu] Ret: %-7u | PC=0x%04X SP=0x%04X Mode=%s\n",
                   cycles, retired_insts, regs.pc, regs.sp,
                   vl_p6_is_r800(vl) ? "R800" : "Z80A");
        }
    }

    printf("\n--- Simulation Complete (%llu cycles, %u frames, %u instructions retired) ---\n",
           cycles, frame_count, retired_insts);
    print_vdp_mode(vl, "end");
    {
        uint8_t r0 = vl_p6_vdp_peek_reg(vl, 0);
        uint8_t r1 = vl_p6_vdp_peek_reg(vl, 1);
        uint8_t mode = (uint8_t)((((r0 & 0x0Eu) << 1) |
                                 ((r1 & 0x08u) >> 2) |
                                 ((r1 & 0x10u) >> 4)) & 0x1Fu);
        if (mode == 0x1Cu && (r1 & 0x40))
            render_screen8(vl);
    }
    if (getenv("MSX_VRAM")) {
        char vpath[1024];
        snprintf(vpath, sizeof(vpath), "%s.vram", out_ppm);
        FILE *vf = fopen(vpath, "wb");
        if (vf) {
            for (uint32_t i = 0; i < 0x20000u; i++)
                fputc(mem_model_read(&g_mem, ADDR_VRAM + i), vf);
            fclose(vf);
            printf("  [VRAM dump] %s\n", vpath);
        }
    }
    {
        trace_regs_t endr;
        vl_p6_get_regs(vl, &endr);
        printf("  [PPI] A=%02X PC=%04X banks=%02X/%02X\n",
               vl_p6_ppi_read_a(vl), endr.pc, g_bank1, g_bank2);
        if (g_key_space || g_key_a) {
            printf("  [KBD] end row8=%02X strobe_cycles=%u row2=%02X strobe_cycles=%u\n",
                   vl_p6_kbd_peek_row(vl, 8), g_kbd_strobe_r8,
                   vl_p6_kbd_peek_row(vl, 2), g_kbd_strobe_r2);
        }
        uint32_t n = g_mring_n < 16 ? g_mring_n : 16;
        uint32_t start = g_mring_n - n;
        printf("  [MEM tail]\n");
        for (uint32_t i = 0; i < n; i++) {
            mem_tr_t *t = &g_mring[(start + i) & 63];
            printf("  MEM %s phys=%06X data=%02X\n", t->wr ? "W" : "R", t->addr, t->data);
        }
        if (endr.pc >= 0x4000 && endr.pc < 0x8000) {
            uint16_t off = (uint16_t)(endr.pc - 0x4000);
            uint16_t baseoff = off >= 16 ? (uint16_t)(off - 16) : 0;
            printf("  [PAGE1] cpu %04X off %04X\n", endr.pc, baseoff);
            for (int seg = 0; seg < 32; seg++) {
                uint32_t phys = ADDR_RAM + (uint32_t)seg * 0x4000u + baseoff;
                int interesting = 0;
                uint8_t b[32];
                for (int i = 0; i < 32; i++) {
                    b[i] = mem_model_read(&g_mem, phys + (uint32_t)i);
                    if (b[i] != 0x00 && b[i] != 0xFF) interesting++;
                }
                if (interesting < 4) continue;
                printf("  SEG %02X", seg);
                for (int i = 0; i < 32; i++) printf(" %02X", b[i]);
                printf("\n");
            }
        }
    }
    {
        /* Screen 1 reads one pattern byte per name. Show that byte so a
         * solid frame can be told apart from a blank glyph. */
        uint8_t r2 = vl_p6_vdp_peek_reg(vl, 2);
        uint8_t r3 = vl_p6_vdp_peek_reg(vl, 3);
        uint8_t r4 = vl_p6_vdp_peek_reg(vl, 4);
        uint32_t nt = (uint32_t)(r2 & 0x7Fu) << 10;
        uint8_t name = mem_model_read(&g_mem, ADDR_VRAM + nt);
        uint32_t pt = (uint32_t)(r4 & 0x3Fu) << 11;
        uint32_t ct = (uint32_t)r3 << 6;
        printf("  [VDP tile] name@%05X=%02X pat", nt, name);
        for (int i = 0; i < 8; i++)
            printf(" %02X", mem_model_read(&g_mem, ADDR_VRAM + pt + (uint32_t)name * 8u + (uint32_t)i));
        uint32_t coff = ct + (uint32_t)(name >> 3);
        printf(" col@%05X=%02X\n", coff, mem_model_read(&g_mem, ADDR_VRAM + coff));
    }

    // Dump active display frame to PPM
    dump_frame_ppm(out_ppm);

    if (getenv("MSX_VRAM")) {
        static const struct { const char *name; uint32_t off; uint32_t len; } areas[] = {
            { "name",  0x1800, 768 },
            { "color", 0x2000, 0x1800 },
            { "pgen",  0x0000, 0x1800 },
            { "sat",   0x1B00, 128 },
            { "spat",  0x3800, 0x800 },
        };
        for (unsigned a = 0; a < 5; a++) {
            uint32_t nonzero = 0;
            printf("  [VRAM %s]", areas[a].name);
            for (uint32_t i = 0; i < areas[a].len; i++) {
                uint8_t v = mem_model_read(&g_mem, ADDR_VRAM + areas[a].off + i);
                if (i < 8) printf(" %02X", v);
                if (v) nonzero++;
            }
            printf(" ... nonzero %u/%u\n", nonzero, areas[a].len);
        }
    }

    // Cleanup
    vl_p6_destroy(vl);
    mem_model_destroy(&g_mem);
    if (g_cart_buf) free(g_cart_buf);

    if (!g_cart_booted) {
        fprintf(stderr, "Cartridge entry was not reached.\n");
        return 2;
    }
    return 0;
}
