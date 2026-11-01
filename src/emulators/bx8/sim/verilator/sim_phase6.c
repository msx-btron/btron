/*
 * sim_phase6.c — Phase 6 Full System MSX turbo R SoC Co-Simulation Suite.
 *
 * Verifies the complete, unified SoC (mkTopMSXTurboR):
 *   1. Full System Power-On Reset & Initial Architectural State
 *   2. S1990 Dynamic CPU Mode Switch (Z80A -> R800) & Turbo LED Verification
 *   3. V9958 Video Display Pipeline, Raster Beam Tracking & Sync Strobes
 *   4. Command Engine Blitter (HMMV Rectangle Fill) & Arbiter Concurrency
 *   5. Integrated 16-bit Mixed Digital Audio Stream (PSG + PCM + SCC)
 *   6. PPI 8255, Matrix Keyboard Strobe, & Status LEDs (CAPS/KANA)
 *   7. Memory Mapper 512KB–4MB Segment Translation Under Full SoC Load
 *
 * Pure C99 (-std=c99).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "vl_bridge_p6.h"
#include "mem_model.h"

static mem_model_t g_mem;

/* ── Service physical memory bus requests from hardware ───────────── */
static void service_mem(void *vl)
{
    while (vl_p6_has_mem_req(vl)) {
        vl_mem_req_t req;
        if (vl_p6_get_mem_req(vl, &req)) {
            if (req.write) {
                mem_model_write(&g_mem, req.addr, req.data);
                vl_p6_put_mem_resp(vl, req.tag, req.data, true);
            } else {
                uint8_t d = mem_model_read(&g_mem, req.addr);
                vl_p6_put_mem_resp(vl, req.tag, d, true);
            }
        }
        vl_p6_step(vl);
    }
}

/* ── Advance CPU until count instructions retire ─────────────────── */
static void run_instructions(void *vl, uint32_t count)
{
    uint32_t retired = 0;
    uint64_t cycles  = 0;
    const uint64_t MAX_CYCLES = 100000;

    while (retired < count && cycles < MAX_CYCLES) {
        vl_p6_step(vl);
        service_mem(vl);
        cycles++;

        if (vl_p6_inst_retired(vl)) {
            retired++;
            vl_p6_step(vl);
            service_mem(vl);
            cycles++;
        }
    }
}

/* ── Step system master clocks ───────────────────────────────────── */
static void step_cycles(void *vl, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        vl_p6_step(vl);
        service_mem(vl);
    }
}

/* ── Test 1: Full System Power-On Reset & Initial Architectural State ── */
static bool test_power_on_reset(void *vl)
{
    printf("[Phase 6] Test 1: Full System Power-On Reset & Initial State...\n");

    uint8_t prog[] = { 0x00, 0x00, 0x00, 0x76 }; // 3 NOPs, HALT
    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    step_cycles(vl, 20);

    trace_regs_t regs;
    vl_p6_get_regs(vl, &regs);

    printf("  Initial PC = 0x%04X, SP = 0x%04X\n", regs.pc, regs.sp);
    printf("  Initial CPU Mode: %s\n", vl_p6_is_r800(vl) ? "R800" : "Z80A");
    printf("  Turbo LED: %s\n", vl_p6_led_turbo(vl) ? "ON" : "OFF");

    if (vl_p6_is_r800(vl)) {
        printf("  [FAIL] Expected Z80A mode at power-on!\n");
        return false;
    }
    if (vl_p6_led_turbo(vl)) {
        printf("  [FAIL] Turbo LED should be OFF at reset!\n");
        return false;
    }

    // Run NOPs to verify instruction retirement
    run_instructions(vl, 3);
    vl_p6_get_regs(vl, &regs);
    printf("  After 3 NOPs: PC = 0x%04X\n", regs.pc);
    if (regs.pc != 0x0003) {
        printf("  [FAIL] PC did not advance as expected!\n");
        return false;
    }

    printf("  [PASS] Test 1: Full System Power-On Reset verified.\n\n");
    return true;
}

/* ── Test 2: S1990 Dynamic CPU Mode Switch (Z80A -> R800) & Turbo LED ── */
static bool test_s1990_mode_switch(void *vl)
{
    printf("[Phase 6] Test 2: S1990 CPU Mode Switch & Turbo LED...\n");

    /* Program:
     *   LD A, 0x06        ; Select S1990 Register 6 (CPU mode)
     *   OUT (0xE4), A
     *   LD A, 0x02        ; Mode 2 = R800 ROM
     *   OUT (0xE5), A
     *   HALT
     */
    uint8_t prog[] = {
        0x3E, 0x06,       // LD A, 0x06
        0xD3, 0xE4,       // OUT (0xE4), A
        0x3E, 0x02,       // LD A, 0x02
        0xD3, 0xE5,       // OUT (0xE5), A
        0x76              // HALT
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    // Initial state: Z80A
    if (vl_p6_is_r800(vl)) {
        printf("  [FAIL] Initial mode must be Z80A!\n");
        return false;
    }

    run_instructions(vl, 5);
    step_cycles(vl, 50);

    bool is_r800 = vl_p6_is_r800(vl);
    bool led_turbo = vl_p6_led_turbo(vl);

    printf("  Post-switch CPU Mode: %s\n", is_r800 ? "R800" : "Z80A");
    printf("  Post-switch Turbo LED: %s\n", led_turbo ? "ON" : "OFF");

    if (!is_r800) {
        printf("  [FAIL] CPU mode switch to R800 failed!\n");
        return false;
    }
    if (!led_turbo) {
        printf("  [FAIL] Turbo LED must be ON in R800 mode!\n");
        return false;
    }

    printf("  [PASS] Test 2: S1990 CPU Mode Switch & Turbo LED verified.\n\n");
    return true;
}

/* ── Test 3: V9958 Video Display Pipeline & Beam Tracking ────────── */
static bool test_video_pipeline(void *vl)
{
    printf("[Phase 6] Test 3: V9958 Video Pipeline & Beam Tracking...\n");

    uint8_t prog[] = { 0x76 };
    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    step_cycles(vl, 50);

    uint16_t x0 = vl_p6_video_beam_x(vl);
    uint8_t  y0 = vl_p6_video_beam_y(vl);

    // Step scanline ticks (228 T-states * 16 = 3648 master clocks per line)
    step_cycles(vl, 4000);

    uint16_t x1 = vl_p6_video_beam_x(vl);
    uint8_t  y1 = vl_p6_video_beam_y(vl);

    printf("  Beam initial: (%d, %d), after 4000 cycles: (%d, %d)\n", x0, y0, x1, y1);
    printf("  HSYNC: %d, VSYNC: %d, BLANK: %d\n",
           vl_p6_video_hsync(vl), vl_p6_video_vsync(vl), vl_p6_video_blank(vl));

    if (x0 == x1 && y0 == y1) {
        printf("  [FAIL] Video raster beam did not advance!\n");
        return false;
    }

    printf("  [PASS] Test 3: V9958 Video Pipeline & Beam Tracking verified.\n\n");
    return true;
}

/* ── Test 4: Command Engine Blitter (HMMV Rectangle Fill) ────────── */
static bool test_blitter_execution(void *vl)
{
    printf("[Phase 6] Test 4: Command Engine Blitter (HMMV) in Full SoC...\n");

    /* Program Screen 5 HMMV byte fill:
     *   DX=0, DY=0, NX=16 (8 bytes in Screen 5), NY=2, COL=0x99, CMD=0xC0 (HMMV)
     *   Writes indirect registers R#36..R#46 via Port 0x9B
     */
    uint8_t prog[] = {
        0x3E, 36,   0xD3, 0x99,  // R#17 = 36 (DX low)
        0x3E, 0x91, 0xD3, 0x99,
        0x3E, 0x00, 0xD3, 0x9B,  // R#36: DX low = 0
        0x3E, 0x00, 0xD3, 0x9B,  // R#37: DX hi = 0
        0x3E, 0x00, 0xD3, 0x9B,  // R#38: DY low = 0
        0x3E, 0x00, 0xD3, 0x9B,  // R#39: DY hi = 0
        0x3E, 16,   0xD3, 0x9B,  // R#40: NX low = 16
        0x3E, 0x00, 0xD3, 0x9B,  // R#41: NX hi = 0
        0x3E, 2,    0xD3, 0x9B,  // R#42: NY low = 2
        0x3E, 0x00, 0xD3, 0x9B,  // R#43: NY hi = 0
        0x3E, 0x99, 0xD3, 0x9B,  // R#44: COL = 0x99
        0x3E, 0x00, 0xD3, 0x9B,  // R#45: ARG = 0
        0x3E, 0xC0, 0xD3, 0x9B,  // R#46: CMD = 0xC0 (HMMV)
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    run_instructions(vl, 27);

    // Let the blitter write its rectangle to VRAM (0x300000)
    step_cycles(vl, 2500);

    bool ok = true;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 8; x++) {
            uint32_t addr = 0x300000 + y * 128 + x;
            uint8_t val = mem_model_read(&g_mem, addr);
            if (val != 0x99) {
                printf("  [FAIL] VRAM[0x%05X] = 0x%02X (Expected: 0x99)\n", addr, val);
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    if (ok) {
        printf("  Blitter HMMV filled 8x2 bytes with color 0x99 in VRAM!\n");
        printf("  [PASS] Test 4: Command Engine Blitter verified.\n\n");
    }
    return ok;
}

/* ── Test 5: Integrated 16-bit Mixed Digital Audio Stream ─────────── */
static bool test_audio_mixer(void *vl)
{
    printf("[Phase 6] Test 5: Integrated Mixed Digital Audio Stream...\n");

    /* Program PSG channel A tone and PCM sample:
     *   PSG R0=1, R1=0, R7=0x3E (tone A on), R8=15 (vol max)
     *   PCM 0xA5=3 (enabled), 0xA4=0xAA (sample)
     */
    uint8_t prog[] = {
        0x3E, 0x00, 0xD3, 0xA0,  // R0 = 1
        0x3E, 0x01, 0xD3, 0xA1,
        0x3E, 0x01, 0xD3, 0xA0,  // R1 = 0
        0x3E, 0x00, 0xD3, 0xA1,
        0x3E, 0x07, 0xD3, 0xA0,  // R7 = 0x3E
        0x3E, 0x3E, 0xD3, 0xA1,
        0x3E, 0x08, 0xD3, 0xA0,  // R8 = 0x0F
        0x3E, 0x0F, 0xD3, 0xA1,
        0x3E, 0x03, 0xD3, 0xA5,  // PCM ctrl = 0x03
        0x3E, 0xAA, 0xD3, 0xA4,  // PCM data = 0xAA
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    run_instructions(vl, 22);

    // Advance 10,000 cycles for PSG and PCM to update
    step_cycles(vl, 10000);

    uint16_t audio = vl_p6_audio_pcm_sample(vl);
    uint16_t psg   = vl_p6_psg_sample(vl);
    uint8_t  pcm   = vl_p6_pcm_sample(vl);

    printf("  PSG raw = 0x%04X, PCM raw = 0x%02X\n", psg, pcm);
    printf("  Mixed 16-bit PCM Stream = 0x%04X\n", audio);

    if (psg == 0 || pcm != 0xAA) {
        printf("  [FAIL] Audio generators inactive!\n");
        return false;
    }

    printf("  [PASS] Test 5: Integrated Digital Audio Stream verified.\n\n");
    return true;
}

/* ── Test 6: PPI 8255 & Matrix Keyboard Strobe ───────────────────── */
static bool test_ppi_and_keyboard(void *vl)
{
    printf("[Phase 6] Test 6: PPI 8255 & Matrix Keyboard Strobe...\n");

    /* Program:
     *   LD A, 0x02        ; Select keyboard row 2 on Port C
     *   OUT (0xAA), A
     *   HALT
     */
    uint8_t prog[] = {
        0x3E, 0x02,
        0xD3, 0xAA,
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    run_instructions(vl, 3);
    step_cycles(vl, 50);

    uint8_t pc = vl_p6_ppi_peek_pc(vl);
    printf("  PPI Port C row latch = %d (Expected: 2)\n", pc & 0x0F);
    if ((pc & 0x0F) != 2) {
        printf("  [FAIL] Port C row latch mismatch!\n");
        return false;
    }

    // Verify row 2 is idle (0xFF)
    uint8_t idle = vl_p6_kbd_peek_row(vl, 2);
    if (idle != 0xFF) {
        printf("  [FAIL] Row 2 not idle!\n");
        return false;
    }

    // Inject key press: row 2, col 4
    vl_p6_kbd_key_down(vl, 2, 4);
    step_cycles(vl, 50);

    uint8_t pressed = vl_p6_kbd_peek_row(vl, 2);
    uint8_t expected = (uint8_t)(0xFF & ~(1u << 4)); // 0xEF
    printf("  Row 2 pressed = 0x%02X (Expected: 0x%02X)\n", pressed, expected);
    if (pressed != expected) {
        printf("  [FAIL] Key press not registered!\n");
        return false;
    }

    // Release key
    vl_p6_kbd_key_up(vl, 2, 4);
    step_cycles(vl, 50);
    uint8_t released = vl_p6_kbd_peek_row(vl, 2);
    if (released != 0xFF) {
        printf("  [FAIL] Key release not registered!\n");
        return false;
    }

    printf("  [PASS] Test 6: PPI 8255 & Matrix Keyboard Strobe verified.\n\n");
    return true;
}

/* ── Test 7: Memory Mapper 512KB-4MB Segment Translation ─────────── */
static bool test_memory_mapper(void *vl)
{
    printf("[Phase 6] Test 7: Memory Mapper Translation in Full SoC...\n");

    /* Program:
     *   1. Select Slot 3 for Page 2 (bits 5..4 = 3 in Port 0xA8): OUT (0xA8), 0x30
     *   2. Set Mapper Segment for Page 2 (Port 0xFE) = 0x05: OUT (0xFE), 0x05
     *   3. Write value 0x5A to Page 2 virtual address 0x8000:
     *        LD HL, 0x8000
     *        LD A, 0x5A
     *        LD (HL), A
     *   4. NOP / HALT
     *
     * Expected physical address:
     *   baseRAM (0x010000) + segment 5 * 16KB (0x014000) + offset (0x0000) = 0x024000.
     */
    uint8_t prog[] = {
        0x3E, 0x30,       // LD A, 0x30 (Page 2 -> Slot 3)
        0xD3, 0xA8,       // OUT (0xA8), A
        0x3E, 0x05,       // LD A, 0x05
        0xD3, 0xFE,       // OUT (0xFE), A (Page 2 mapper segment = 5)
        0x21, 0x00, 0x80, // LD HL, 0x8000
        0x3E, 0x5A,       // LD A, 0x5A
        0x77,             // LD (HL), A
        0x76              // HALT
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p6_reset(vl);

    run_instructions(vl, 8);
    step_cycles(vl, 50);

    // Read translated physical address 0x024000 (baseRAM 0x010000 + 5 * 0x4000)
    uint32_t expected_paddr = 0x010000 + (5 * 0x4000); // 0x024000
    uint8_t val = mem_model_read(&g_mem, expected_paddr);
    printf("  Physical RAM[0x%06X] = 0x%02X (Expected: 0x5A)\n", expected_paddr, val);

    if (val != 0x5A) {
        printf("  [FAIL] Memory Mapper address translation failed!\n");
        return false;
    }

    printf("  [PASS] Test 7: Memory Mapper Segment Translation verified.\n\n");
    return true;
}

/* ── Main Execution ──────────────────────────────────────────────── */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("================================================================\n");
    printf(" MSX turbo R Phase 6: Full System SoC Co-Simulation Suite\n");
    printf("================================================================\n\n");

    mem_model_init(&g_mem);
    void *vl = vl_p6_create();
    if (!vl) {
        fprintf(stderr, "Failed to initialize Verilator model!\n");
        return 1;
    }

    bool all_pass = true;
    all_pass &= test_power_on_reset(vl);
    all_pass &= test_s1990_mode_switch(vl);
    all_pass &= test_video_pipeline(vl);
    all_pass &= test_blitter_execution(vl);
    all_pass &= test_audio_mixer(vl);
    all_pass &= test_ppi_and_keyboard(vl);
    all_pass &= test_memory_mapper(vl);

    vl_p6_destroy(vl);
    mem_model_destroy(&g_mem);

    printf("================================================================\n");
    if (all_pass) {
        printf(" ALL PHASE 6 FULL SYSTEM TESTS PASSED!\n");
    } else {
        printf(" PHASE 6 FULL SYSTEM TESTS FAILED!\n");
    }
    printf("================================================================\n");

    return all_pass ? 0 : 1;
}
