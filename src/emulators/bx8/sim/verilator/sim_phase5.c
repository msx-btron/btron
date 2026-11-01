/*
 * sim_phase5.c — Phase 5 Pure C99 Co-Simulation Testbench: Audio & I/O.
 *
 * Verifies:
 *   1. PSG register write & readback (R7 mixer, R8 volume, R0/R1 period)
 *   2. PSG noise LFSR initial seed (0x0ACE after reset)
 *   3. PSG PCM sample activity (non-zero after tone channel setup + ticks)
 *   4. PCM DAC write & hold (write 0xAA, poll until sample = 0xAA)
 *   5. PPI port A slot-select latch (write 0x84, readback)
 *   6. PPI port C keyboard row select (write 0x05, low nibble = 5)
 *   7. Keyboard matrix key press (row 0 col 5 = SPACE, active low)
 *
 * Strict constraint: -std=c99 pure C. Uses Z80 opcodes loaded into ROM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "vl_bridge_p5.h"
#include "mem_model.h"

/* ── Global memory model ──────────────────────────────────────────── */
static mem_model_t g_mem;

/* ── Memory service loop (same pattern as sim_phase4.c) ────────────── */
static void service_mem(void *vl)
{
    while (vl_p5_has_mem_req(vl)) {
        vl_mem_req_t req;
        if (vl_p5_get_mem_req(vl, &req)) {
            if (req.write) {
                mem_model_write(&g_mem, req.addr, req.data);
                vl_p5_put_mem_resp(vl, req.tag, req.data, true);
            } else {
                uint8_t d = mem_model_read(&g_mem, req.addr);
                vl_p5_put_mem_resp(vl, req.tag, d, true);
            }
        }
        vl_p5_step(vl);
    }
}

/* ── Step CPU until N instructions retire ────────────────────────── */
static void run_instructions(void *vl, uint32_t count)
{
    uint32_t retired = 0;
    uint64_t cycles  = 0;
    const uint64_t MAX_CYCLES = 80000;

    while (retired < count && cycles < MAX_CYCLES) {
        vl_p5_step(vl);
        service_mem(vl);
        cycles++;

        if (vl_p5_inst_retired(vl)) {
            retired++;
            vl_p5_step(vl);
            service_mem(vl);
            cycles++;
        }
    }
}

/* ── Test 1: PSG Register Write & Readback ────────────────────────── */
static bool test_psg_registers(void *vl)
{
    printf("[Phase 5] Test 1: PSG Register Write & Readback...\n");

    /*
     * PSG port select = 0xA0, write = 0xA1.
     * Program:
     *   OUT 0xA0, 7    ; select R7
     *   OUT 0xA1, 0xB8 ; R7 = 0xB8 (all channels silenced: tone+noise disabled)
     *   OUT 0xA0, 8    ; select R8
     *   OUT 0xA1, 0x0F ; R8 = 0x0F (ch A vol = 15)
     *   OUT 0xA0, 0    ; select R0
     *   OUT 0xA1, 0x00 ; R0 = 0x00 (tone period low = 0)
     *   OUT 0xA0, 1    ; select R1
     *   OUT 0xA1, 0x01 ; R1 = 0x01 (tone period high = 1 → TP = 0x100)
     *   HALT
     */
    uint8_t prog[] = {
        0x3E, 0x07, 0xD3, 0xA0,  /* LD A,7 / OUT (0xA0),A */
        0x3E, 0xB8, 0xD3, 0xA1,  /* LD A,0xB8 / OUT (0xA1),A */
        0x3E, 0x08, 0xD3, 0xA0,  /* LD A,8 / OUT (0xA0),A */
        0x3E, 0x0F, 0xD3, 0xA1,  /* LD A,0x0F / OUT (0xA1),A */
        0x3E, 0x00, 0xD3, 0xA0,  /* LD A,0 / OUT (0xA0),A */
        0x3E, 0x00, 0xD3, 0xA1,  /* LD A,0x00 / OUT (0xA1),A */
        0x3E, 0x01, 0xD3, 0xA0,  /* LD A,1 / OUT (0xA0),A */
        0x3E, 0x01, 0xD3, 0xA1,  /* LD A,0x01 / OUT (0xA1),A */
        0x76                     /* HALT */
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);

    run_instructions(vl, 20);
    vl_p5_step(vl);

    uint8_t r7 = vl_p5_psg_peek_reg(vl, 7);
    uint8_t r8 = vl_p5_psg_peek_reg(vl, 8);
    uint8_t r0 = vl_p5_psg_peek_reg(vl, 0);
    uint8_t r1 = vl_p5_psg_peek_reg(vl, 1);

    printf("  R7 = 0x%02X (expected 0xB8)\n", r7);
    printf("  R8 = 0x%02X (expected 0x0F)\n", r8);
    printf("  R0 = 0x%02X (expected 0x00)\n", r0);
    printf("  R1 = 0x%02X (expected 0x01)\n", r1);

    if (r7 != 0xB8 || r8 != 0x0F || r0 != 0x00 || r1 != 0x01) {
        printf("  [FAIL] PSG register mismatch!\n\n");
        return false;
    }
    printf("  [PASS] Test 1: PSG Register Write & Readback verified.\n\n");
    return true;
}

/* ── Test 2: PSG LFSR Initial Seed ───────────────────────────────── */
static bool test_psg_lfsr_seed(void *vl)
{
    printf("[Phase 5] Test 2: PSG Noise LFSR Initial Seed...\n");

    /* Fresh reset — LFSR should be 0x0ACE before any noise ticks */
    uint8_t prog[] = { 0x76 }; /* HALT immediately */
    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);

    /* Step just enough to settle, but PSG divider (/16) means few PSG ticks */
    for (int i = 0; i < 10; i++) {
        vl_p5_step(vl);
        service_mem(vl);
    }

    uint32_t lfsr = vl_p5_psg_peek_lfsr(vl);
    printf("  LFSR = 0x%05X (expected 0x0ACE or shifted)\n", lfsr);

    /* After reset and <16 master ticks, PSG div counter hasn't wrapped yet,
     * so no LFSR shift has occurred.  Seed must still be 0x0ACE. */
    if (lfsr != 0x0ACEu) {
        printf("  [FAIL] LFSR seed mismatch!\n\n");
        return false;
    }
    printf("  [PASS] Test 2: PSG LFSR seed = 0x0ACE verified.\n\n");
    return true;
}

/* ── Test 3: PSG PCM Sample Activity ─────────────────────────────── */
static bool test_psg_sample(void *vl)
{
    printf("[Phase 5] Test 3: PSG PCM Sample Activity...\n");

    /*
     * Setup channel A tone: period=1 (min), vol=15, mixer tone A enabled,
     * noise disabled.  Run 2000 cycles to collect multiple PSG ticks.
     * R7 = 0x3E = 0b00111110: noise off for all, tone A on (bit 0 = 0).
     */
    uint8_t prog[] = {
        0x3E, 0x00, 0xD3, 0xA0,  /* R0 = 0x00 (period low = 0) */
        0x3E, 0x00, 0xD3, 0xA1,
        0x3E, 0x01, 0xD3, 0xA0,  /* R1 = 0x01 (period high = 1 → TP=256) */
        0x3E, 0x00, 0xD3, 0xA1,  /* Wait: actually select R1 then write */
        0x3E, 0x07, 0xD3, 0xA0,  /* R7: select */
        0x3E, 0x3E, 0xD3, 0xA1,  /* R7 = 0x3E (tone A on, all noise off) */
        0x3E, 0x08, 0xD3, 0xA0,  /* R8: select */
        0x3E, 0x0F, 0xD3, 0xA1,  /* R8 = 0x0F vol=15 */
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);

    run_instructions(vl, 9);

    /* Period 1 is a square wave: the sample holds one half-cycle and a
     * single read is zero on the low half. Two PSG ticks are 512 clocks. */
    for (int i = 0; i < 2000; i++) {
        vl_p5_step(vl);
        service_mem(vl);
    }

    uint16_t samp = 0;
    for (int i = 0; i < 512; i++) {
        vl_p5_step(vl);
        service_mem(vl);
        uint16_t s = vl_p5_psg_sample(vl);
        if (s > samp) samp = s;
    }
    printf("  PSG peak = 0x%04X (expected > 0)\n", samp);

    if (samp == 0) {
        printf("  [FAIL] PSG sample still zero after a full period!\n\n");
        return false;
    }
    printf("  [PASS] Test 3: PSG PCM sample is non-zero.\n\n");
    return true;
}

/* ── Test 4: PCM DAC Write & Latched Hold ────────────────────────── */
static bool test_pcm_dac(void *vl)
{
    printf("[Phase 5] Test 4: PCM DAC Write & Latched Hold...\n");

    /*
     * Write ctrl = 0x03 (latched, enabled) to 0xA5, then sample = 0xAA to 0xA4.
     * After 1 PCM tick (228 T-states), hold = 0xAA.
     * We run 500 cycles (>> 228) to ensure the tick fires.
     */
    uint8_t prog[] = {
        0x3E, 0x03, 0xD3, 0xA5,  /* ctrl = 0x03 */
        0x3E, 0xAA, 0xD3, 0xA4,  /* data = 0xAA */
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);

    run_instructions(vl, 6);

    /* Run enough for PCM tick (228 Z80 T-states = 228 * 16 = 3648 master clocks) */
    for (int i = 0; i < 10000; i++) {
        vl_p5_step(vl);
        service_mem(vl);
    }

    uint8_t samp = vl_p5_pcm_sample(vl);
    printf("  PCM sample = 0x%02X (expected 0xAA)\n", samp);

    if (samp != 0xAAu) {
        printf("  [FAIL] PCM latched sample mismatch!\n\n");
        return false;
    }
    printf("  [PASS] Test 4: PCM DAC latched hold = 0xAA verified.\n\n");
    return true;
}

/* ── Test 5: PPI Port A Slot-Select Latch ────────────────────────── */
static bool test_ppi_slot_select(void *vl)
{
    printf("[Phase 5] Test 5: PPI Port A Slot-Select Latch...\n");

    /* Write 0x84 to port 0xA8 (PPI port A) */
    uint8_t prog[] = {
        0x3E, 0x84, 0xD3, 0xA8,  /* OUT (0xA8), 0x84 */
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);

    run_instructions(vl, 2);
    vl_p5_step(vl);

    uint8_t pa = vl_p5_ppi_read_a(vl);
    printf("  PPI Port A = 0x%02X (expected 0x84)\n", pa);

    if (pa != 0x84u) {
        printf("  [FAIL] PPI port A latch mismatch!\n\n");
        return false;
    }
    printf("  [PASS] Test 5: PPI Port A slot-select latch verified.\n\n");
    return true;
}

/* ── Test 6: PPI Port C Keyboard Row Select ──────────────────────── */
static bool test_ppi_kbd_row(void *vl)
{
    printf("[Phase 5] Test 6: PPI Port C Keyboard Row Select...\n");

    /* Write 0x05 to 0xAA (PPI port C) → row select = 5 */
    uint8_t prog[] = {
        0x3E, 0x05, 0xD3, 0xAA,  /* OUT (0xAA), 0x05 */
        0x76
    };

    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);

    run_instructions(vl, 2);
    vl_p5_step(vl);

    uint8_t pc = vl_p5_ppi_peek_pc(vl);
    printf("  PPI Port C = 0x%02X  row nibble = %d (expected 5)\n", pc, pc & 0x0F);

    if ((pc & 0x0F) != 5) {
        printf("  [FAIL] PPI Port C row nibble mismatch!\n\n");
        return false;
    }
    printf("  [PASS] Test 6: PPI Port C row select = 5 verified.\n\n");
    return true;
}

/* ── Test 7: Keyboard Matrix Key Press ───────────────────────────── */
static bool test_keyboard(void *vl)
{
    printf("[Phase 5] Test 7: Keyboard Matrix Key Press (row 0, col 5)...\n");

    uint8_t prog[] = { 0x76 }; /* just idle */
    mem_model_reset(&g_mem);
    mem_model_load_buf(&g_mem, 0x000000, prog, sizeof(prog));
    vl_p5_reset(vl);
    for (int i = 0; i < 20; i++) { vl_p5_step(vl); service_mem(vl); }

    /* Idle: row 0 should be 0xFF */
    uint8_t idle = vl_p5_kbd_peek_row(vl, 0);
    printf("  Row 0 idle = 0x%02X (expected 0xFF)\n", idle);
    if (idle != 0xFF) {
        printf("  [FAIL] Keyboard row 0 not idle!\n\n");
        return false;
    }

    /* Press SPACE = row 0, col 5 */
    vl_p5_kbd_key_down(vl, 0, 5);
    for (int i = 0; i < 20; i++) { vl_p5_step(vl); service_mem(vl); }

    uint8_t pressed  = vl_p5_kbd_peek_row(vl, 0);
    uint8_t expected = (uint8_t)(0xFF & ~(1u << 5));  /* 0xDF */
    printf("  Row 0 pressed = 0x%02X (expected 0x%02X)\n", pressed, expected);
    if (pressed != expected) {
        printf("  [FAIL] Keyboard press not registered!\n\n");
        return false;
    }

    /* Release */
    vl_p5_kbd_key_up(vl, 0, 5);
    for (int i = 0; i < 20; i++) { vl_p5_step(vl); service_mem(vl); }

    uint8_t released = vl_p5_kbd_peek_row(vl, 0);
    printf("  Row 0 released = 0x%02X (expected 0xFF)\n", released);
    if (released != 0xFF) {
        printf("  [FAIL] Keyboard release not registered!\n\n");
        return false;
    }

    printf("  [PASS] Test 7: Keyboard matrix key press/release verified.\n\n");
    return true;
}

/* ── Main ─────────────────────────────────────────────────────────── */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("================================================================\n");
    printf(" MSX turbo R Phase 5: Audio & I/O Co-Simulation Suite\n");
    printf("================================================================\n\n");

    mem_model_init(&g_mem);
    void *vl = vl_p5_create();
    if (!vl) {
        fprintf(stderr, "Failed to initialize Verilator model!\n");
        return 1;
    }

    bool all_pass = true;
    all_pass &= test_psg_registers(vl);
    all_pass &= test_psg_lfsr_seed(vl);
    all_pass &= test_psg_sample(vl);
    all_pass &= test_pcm_dac(vl);
    all_pass &= test_ppi_slot_select(vl);
    all_pass &= test_ppi_kbd_row(vl);
    all_pass &= test_keyboard(vl);

    vl_p5_destroy(vl);
    mem_model_destroy(&g_mem);

    printf("================================================================\n");
    if (all_pass) {
        printf(" ALL PHASE 5 TESTS PASSED!\n");
    } else {
        printf(" PHASE 5 TESTS FAILED!\n");
    }
    printf("================================================================\n");
    return all_pass ? 0 : 1;
}
