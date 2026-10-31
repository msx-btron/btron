/*
 * sim_phase2.c — Pure C99 Phase 2 Co-Simulation Driver.
 *
 * Verifies:
 *   1. CPU Instruction Fetch & Execution Pipeline (NOP, LD, ALU, JR, Jumps).
 *   2. R800 Hardware Multiplier (MULUB 8x8 -> HL, MULUQ 16x16 -> DE:HL).
 *   3. S1990 / T9769C dynamic CPU clock & mode switching (Z80A 3.58MHz <-> R800 7.16MHz).
 *   4. Memory bus transactions against the flat C99 memory model.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "vl_bridge_p2.h"
#include "mem_model.h"
#include "trace_compare.h"

// Service pending memory requests from CPU Arbiter
static void service_mem(void *vl, mem_model_t *mem)
{
    while (vl_p2_has_mem_req(vl)) {
        vl_mem_req_t req;
        if (vl_p2_get_mem_req(vl, &req)) {
            if (req.write) {
                mem_model_write(mem, req.addr, req.data);
                mem->writes_serviced++;
                vl_p2_put_mem_resp(vl, req.tag, req.data, true);
            } else {
                uint8_t d = mem_model_read(mem, req.addr);
                mem->reads_serviced++;
                vl_p2_put_mem_resp(vl, req.tag, d, true);
            }
        }
        vl_p2_step(vl);
    }
}

// Step until N instructions retire or safety limit reached with debug tracing
static uint64_t run_instructions_trace(void *vl, mem_model_t *mem, uint64_t count, uint64_t max_cycles, bool trace)
{
    uint64_t retired = 0;
    uint64_t cycles = 0;
    trace_regs_t regs;

    while (retired < count && cycles < max_cycles) {
        vl_p2_step(vl);
        service_mem(vl, mem);
        cycles++;
        if (vl_p2_inst_retired(vl)) {
            retired++;
            // Step one cycle to commit architectural writeback to register outputs
            vl_p2_step(vl);
            service_mem(vl, mem);
            cycles++;

            if (trace) {
                vl_p2_get_regs(vl, &regs);
                printf("    [RETIRE #%llu @ cycle %llu] state=%u opcode=0x%02X PC=0x%04X A=0x%02X F=0x%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X\n",
                       (unsigned long long)retired, (unsigned long long)cycles,
                       vl_p2_get_state(vl), vl_p2_get_opcode(vl),
                       regs.pc, regs.a, regs.f, regs.b, regs.c, regs.d, regs.e, regs.h, regs.l);
            }
        }
    }
    return retired;
}

static uint64_t run_instructions(void *vl, mem_model_t *mem, uint64_t count, uint64_t max_cycles)
{
    return run_instructions_trace(vl, mem, count, max_cycles, false);
}

int main(void)
{
    void        *vl = NULL;
    mem_model_t  mem;
    trace_regs_t regs;
    int pass = 1;

    printf("MSX turboR FPGA Phase 2 Co-Sim: Unified CPU Core & S1990 Engine\n");
    printf("===============================================================\n\n");

    mem_model_init(&mem);
    vl = vl_p2_create();
    if (!vl) {
        fprintf(stderr, "Failed to create Verilated TopPhase2 model\n");
        return 1;
    }

    // ── Test 1: Reset Vector & NOP Sequence ─────────────────────────────────
    printf("Test 1: Reset Vector & NOP execution...\n");
    mem_model_reset(&mem);
    // Fill reset vector with NOPs (0x00)
    for (uint32_t a = 0; a < 16; a++) mem_model_write(&mem, a, 0x00);
    vl_p2_reset(vl);

    vl_p2_get_regs(vl, &regs);
    printf("  Initial PC: 0x%04X, SP: 0x%04X\n", regs.pc, regs.sp);

    uint64_t ret = run_instructions(vl, &mem, 5, 2000);
    vl_p2_get_regs(vl, &regs);
    printf("  Retired %llu instructions, PC: 0x%04X (expected 0x0005)\n",
           (unsigned long long)ret, regs.pc);
    if (ret == 5 && regs.pc == 0x0005) {
        printf("  [PASS] Reset vector and NOP execution verified OK\n");
    } else {
        printf("  [FAIL] NOP execution divergence\n");
        pass = 0;
    }

    // ── Test 2: Arithmetic & Immediate Execution (LD A,n ; ADD A,n) ─────────
    printf("\nTest 2: Basic Arithmetic (LD A, 0x10; ADD A, 0x25)...\n");
    mem_model_reset(&mem);
    // Program:
    //   0x0000: 3E 10        LD A, 0x10
    //   0x0002: C6 25        ADD A, 0x25   (result should be 0x35)
    //   0x0004: 00           NOP
    uint8_t prog1[] = { 0x3E, 0x10, 0xC6, 0x25, 0x00 };
    mem_model_load_buf(&mem, 0x0000, prog1, sizeof(prog1));
    vl_p2_reset(vl);

    ret = run_instructions_trace(vl, &mem, 2, 2000, true);
    vl_p2_get_regs(vl, &regs);
    printf("  After 2 instructions: A=0x%02X (expected 0x35), PC=0x%04X\n", regs.a, regs.pc);
    if (regs.a == 0x35) {
        printf("  [PASS] ALU ADD immediate verified OK\n");
    } else {
        printf("  [FAIL] ALU result mismatch: got 0x%02X expected 0x35\n", regs.a);
        pass = 0;
    }

    // ── Test 3: R800 Hardware Multiplier (MULUB A, B) ───────────────────────
    printf("\nTest 3: R800 Hardware Multiplier (MULUB A, B: 0x12 * 0x34)...\n");
    mem_model_reset(&mem);
    // Program:
    //   0x0000: 3E 12        LD A, 0x12
    //   0x0002: 06 34        LD B, 0x34
    //   0x0004: ED C1        MULUB A, B    (0x12 * 0x34 = 0x03A8 -> HL)
    //   0x0006: 00           NOP
    uint8_t prog_mul[] = { 0x3E, 0x12, 0x06, 0x34, 0xED, 0xC1, 0x00 };
    mem_model_load_buf(&mem, 0x0000, prog_mul, sizeof(prog_mul));
    vl_p2_reset(vl);

    ret = run_instructions_trace(vl, &mem, 3, 3000, true);
    vl_p2_get_regs(vl, &regs);
    uint16_t hl = ((uint16_t)regs.h << 8) | regs.l;
    printf("  After MULUB: HL=0x%04X (expected 0x03A8)\n", hl);
    if (hl == 0x03A8) {
        printf("  [PASS] R800 8x8 Hardware Multiplier verified OK\n");
    } else {
        printf("  [FAIL] Multiplier mismatch: got 0x%04X expected 0x03A8\n", hl);
        pass = 0;
    }

    // ── Test 4: R800 16x16 Multiplier (MULUQ HL, BC) ────────────────────────
    printf("\nTest 4: R800 16x16 Multiplier (MULUQ HL, BC: 0x0100 * 0x0200)...\n");
    mem_model_reset(&mem);
    // Program:
    //   0x0000: 21 00 01     LD HL, 0x0100
    //   0x0003: 01 00 02     LD BC, 0x0200
    //   0x0006: ED C3        MULUQ HL, BC  (0x0100 * 0x0200 = 0x00020000 -> DE:HL)
    //   0x0008: 00           NOP
    uint8_t prog_muluq[] = { 0x21, 0x00, 0x01, 0x01, 0x00, 0x02, 0xED, 0xC3, 0x00 };
    mem_model_load_buf(&mem, 0x0000, prog_muluq, sizeof(prog_muluq));
    vl_p2_reset(vl);

    ret = run_instructions(vl, &mem, 3, 4000);
    vl_p2_get_regs(vl, &regs);
    uint32_t de_hl = ((uint32_t)regs.d << 24) | ((uint32_t)regs.e << 16) |
                     ((uint32_t)regs.h << 8)  | (uint32_t)regs.l;
    printf("  After MULUQ: DE:HL=0x%08X (expected 0x00020000)\n", de_hl);
    if (de_hl == 0x00020000) {
        printf("  [PASS] R800 16x16 Hardware Multiplier verified OK\n");
    } else {
        printf("  [FAIL] Multiplier mismatch: got 0x%08X expected 0x00020000\n", de_hl);
        pass = 0;
    }

    // ── Test 5: S1990 Dynamic CPU Mode Switch (Z80 -> R800) ─────────────────
    printf("\nTest 5: S1990 CPU Mode Switch (Port 0xE4/0xE5)...\n");
    mem_model_reset(&mem);
    // Program:
    //   0x0000: 3E 06        LD A, 6       (S1990 reg 6: CPU mode control)
    //   0x0002: D3 E4        OUT (0xE4), A
    //   0x0004: 3E 40        LD A, 0x40    (bit 5=0: R800 mode, bit 6=1: ROM mode)
    //   0x0006: D3 E5        OUT (0xE5), A
    //   0x0008: 00           NOP
    uint8_t prog_s1990[] = { 0x3E, 0x06, 0xD3, 0xE4, 0x3E, 0x40, 0xD3, 0xE5, 0x00 };
    mem_model_load_buf(&mem, 0x0000, prog_s1990, sizeof(prog_s1990));
    vl_p2_reset(vl);

    bool init_r800 = vl_p2_is_r800(vl);
    printf("  Initial CPU mode: %s\n", init_r800 ? "R800" : "Z80A (expected)");

    ret = run_instructions(vl, &mem, 4, 3000);
    bool final_r800 = vl_p2_is_r800(vl);
    printf("  Post-switch CPU mode: %s\n", final_r800 ? "R800 (expected)" : "Z80A");

    if (!init_r800 && final_r800) {
        printf("  [PASS] S1990 Dynamic CPU handoff verified OK\n");
    } else {
        printf("  [FAIL] S1990 handoff failed: init=%d final=%d\n", init_r800, final_r800);
        pass = 0;
    }

    // ── Summary ─────────────────────────────────────────────────────────────
    printf("\n=== Phase 2 CPU Core Result: %s ===\n", pass ? "ALL PASS" : "SOME FAILURES");

    vl_p2_destroy(vl);
    mem_model_destroy(&mem);
    return pass ? 0 : 1;
}
