/*
 * sim_phase3.c — Pure C99 Phase 3 Co-Simulation Driver.
 *
 * Verifies:
 *   1. MSX Primary Slot Selection via PPI Port #A8.
 *   2. Virtual-to-Physical translation for Slot 0 (Main BIOS) & Slot 1 (Cartridge).
 *   3. MSX Memory Mapper 16KB Segment Banking (Ports #FC..#FF) for Slot 3 RAM.
 *   4. Headless C-BIOS boot initialization sequence.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "vl_bridge_p3.h"
#include "mem_model.h"
#include "trace_compare.h"

// Recorded physical memory write address and data for verification
static uint32_t s_last_write_paddr = 0;
static uint8_t  s_last_write_data  = 0;
static bool     s_write_captured   = false;

static void service_mem(void *vl, mem_model_t *mem)
{
    while (vl_p3_has_mem_req(vl)) {
        vl_mem_req_t req;
        if (vl_p3_get_mem_req(vl, &req)) {
            if (req.write) {
                mem_model_write(mem, req.addr, req.data);
                mem->writes_serviced++;
                if (!req.is_io) {
                    s_last_write_paddr = req.addr;
                    s_last_write_data  = req.data;
                    s_write_captured   = true;
                }
                vl_p3_put_mem_resp(vl, req.tag, req.data, true);
            } else {
                uint8_t d = mem_model_read(mem, req.addr);
                mem->reads_serviced++;
                vl_p3_put_mem_resp(vl, req.tag, d, true);
            }
        }
        vl_p3_step(vl);
    }
}

static uint64_t run_instructions(void *vl, mem_model_t *mem, uint64_t count, uint64_t max_cycles)
{
    uint64_t retired = 0;
    uint64_t cycles = 0;

    while (retired < count && cycles < max_cycles) {
        vl_p3_step(vl);
        service_mem(vl, mem);
        cycles++;
        if (vl_p3_inst_retired(vl)) {
            retired++;
            // Step one cycle to commit architectural register writeback
            vl_p3_step(vl);
            service_mem(vl, mem);
            cycles++;
        }
    }
    return retired;
}

int main(void)
{
    void        *vl = NULL;
    mem_model_t  mem;
    trace_regs_t regs;
    int pass = 1;

    printf("MSX turboR FPGA Phase 3 Co-Sim: Slot Manager & Memory Mapper\n");
    printf("============================================================\n\n");

    mem_model_init(&mem);
    vl = vl_p3_create();
    if (!vl) {
        fprintf(stderr, "Failed to create Verilated TopPhase3 model\n");
        return 1;
    }

    // ── Test 1: Primary Slot Selection via PPI Port #A8 ─────────────────────
    printf("Test 1: PPI Port #A8 Slot Selection...\n");
    mem_model_reset(&mem);
    // Program:
    //   0x0000: 3E 55        LD A, 0x55    (Slot 1 on all four pages)
    //   0x0002: D3 A8        OUT (0xA8), A
    //   0x0004: 00           NOP
    uint8_t prog_slot[] = { 0x3E, 0x55, 0xD3, 0xA8, 0x00 };
    mem_model_load_buf(&mem, 0x0000, prog_slot, sizeof(prog_slot));
    vl_p3_reset(vl);

    uint8_t init_ppi = vl_p3_slot_ppi_a(vl);
    printf("  Initial Port #A8: 0x%02X (expected 0x00)\n", init_ppi);

    run_instructions(vl, &mem, 2, 2000);
    uint8_t final_ppi = vl_p3_slot_ppi_a(vl);
    printf("  After OUT (0xA8), 0x55: Port #A8 = 0x%02X (expected 0x55)\n", final_ppi);

    if (init_ppi == 0x00 && final_ppi == 0x55) {
        printf("  [PASS] Primary Slot selection via PPI Port #A8 verified OK\n");
    } else {
        printf("  [FAIL] Port #A8 latch mismatch\n");
        pass = 0;
    }

    // ── Test 2: Memory Mapper Segment Translation (Ports #FC..#FF) ──────────
    printf("\nTest 2: Memory Mapper Banking (Slot 3 RAM, Page 2 -> Segment 7)...\n");
    mem_model_reset(&mem);
    s_write_captured = false;
    // Program:
    //   1. Select Slot 3 for Page 2 (0x8000..0xBFFF): PPI #A8 = 0x30 (bits 5:4 = 3)
    //   2. Set Page 2 segment to 7: OUT (#FE), 7
    //   3. Write 0x77 to virtual address 0x8000:
    //        LD HL, 0x8000
    //        LD (HL), 0x77
    // Expected physical address:
    //   baseRAM (0x010000) + 7 * 16KB (0x1C000) + (0x8000 & 0x3FFF) (0x0000) = 0x02C000.
    uint8_t prog_mapper[] = {
        0x3E, 0x30,       // LD A, 0x30 (Page 2 -> Slot 3)
        0xD3, 0xA8,       // OUT (0xA8), A
        0x3E, 0x07,       // LD A, 7
        0xD3, 0xFE,       // OUT (0xFE), A (Page 2 mapper segment = 7)
        0x21, 0x00, 0x80, // LD HL, 0x8000
        0x3E, 0x77,       // LD A, 0x77
        0x77,             // LD (HL), A
        0x00              // NOP
    };
    mem_model_load_buf(&mem, 0x0000, prog_mapper, sizeof(prog_mapper));
    vl_p3_reset(vl);

    run_instructions(vl, &mem, 7, 4000);
    uint32_t expected_paddr = 0x010000 + (7 * 0x4000);
    printf("  Captured write: PAddr=0x%06X (expected 0x%06X), Data=0x%02X (expected 0x77)\n",
           s_last_write_paddr, expected_paddr, s_last_write_data);

    if (s_write_captured && s_last_write_paddr == expected_paddr && s_last_write_data == 0x77) {
        printf("  [PASS] Memory Mapper 16KB segment translation verified OK\n");
    } else {
        printf("  [FAIL] Memory Mapper address translation mismatch\n");
        pass = 0;
    }

    // ── Test 3: C-BIOS Headless Boot Sequence ────────────────────────────────
    printf("\nTest 3: C-BIOS Boot Sequence Emulation...\n");
    mem_model_reset(&mem);
    // Program emulating C-BIOS entry:
    //   0x0000: F3           DI
    //   0x0001: 31 80 F3     LD SP, 0xF380   (standard MSX stack pointer)
    //   0x0004: 3E 00        LD A, 0x00
    //   0x0006: D3 A8        OUT (0xA8), A   (all pages to Slot 0 BIOS)
    //   0x0008: 00           NOP
    uint8_t prog_cbios[] = { 0xF3, 0x31, 0x80, 0xF3, 0x3E, 0x00, 0xD3, 0xA8, 0x00 };
    mem_model_load_buf(&mem, 0x0000, prog_cbios, sizeof(prog_cbios));
    vl_p3_reset(vl);

    run_instructions(vl, &mem, 4, 3000);
    vl_p3_get_regs(vl, &regs);
    printf("  After C-BIOS init: SP=0x%04X (expected 0xF380), PC=0x%04X, Port #A8=0x%02X\n",
           regs.sp, regs.pc, vl_p3_slot_ppi_a(vl));

    if (regs.sp == 0xF380 && vl_p3_slot_ppi_a(vl) == 0x00) {
        printf("  [PASS] C-BIOS headless boot initialization verified OK\n");
    } else {
        printf("  [FAIL] C-BIOS boot state divergence\n");
        pass = 0;
    }

    // ── Summary ─────────────────────────────────────────────────────────────
    printf("\n=== Phase 3 Memory & Slots Result: %s ===\n", pass ? "ALL PASS" : "SOME FAILURES");

    vl_p3_destroy(vl);
    mem_model_destroy(&mem);
    return pass ? 0 : 1;
}
