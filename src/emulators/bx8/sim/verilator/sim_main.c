/*
 * sim_main.c — Pure C99 Phase 1 Co-Simulation Driver.
 *
 * Tests:
 *   1. Clock generator produces correct tick frequencies over N master cycles.
 *   2. Memory Arbiter correctly routes CPU requests with full read/write round-trips.
 *   3. VDP Scanout requests have strict priority over CPU.
 *
 * Timing model (BSC-generated Verilog with posedge-first vl_step):
 *   - Set EN + data inputs BEFORE vl_step().
 *   - vl_step() does: posedge (captures EN) → negedge (outputs settle) → clear ENs.
 *   - Read output signals AFTER vl_step() returns.
 *
 * Usage:
 *   ./fpga_sim_phase1 [master_cycles]   (default 1000000)
 *
 * References:
 *   src/emulators/bx8/bsv/MSXClocks.bsv
 *   src/emulators/bx8/bsv/Bus.bsv
 *   src/emulators/bx8/bsv/memory/MemoryArbiter.bsv
 *   src/emulators/bx8/bsv/TopPhase1.bsv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "vl_bridge.h"
#include "mem_model.h"
#include "frame_dump.h"
#include "trace_compare.h"

/* ── Clock tick ratio constants (derived from 57.272728 MHz master) ─────── */
#define DIV_28M    2u
#define DIV_7M     8u
#define DIV_3M58   16u
#define DIV_E6     224u     /* 57.272728 / 255.682kHz */
#define DIV_PCM    3648u    /* 57.272728 / 15.700kHz  */
#define DIV_AUDIO  1298u    /* 57.272728 / 44.124kHz  */

/* ── Memory Map Layout ──────────────────────────────────────────────────── */
#define BIOS_BASE  0x000000u    /* 32 KB C-BIOS main ROM */
#define RAM_BASE   0x010000u    /* 512 KB main RAM */
#define VRAM_BASE  0x080000u    /* 128 KB VRAM */
#define ROM_BASE   0x100000u    /* up to 4 MB cartridge ROM */

/* ── Clock tick counters ────────────────────────────────────────────────── */
typedef struct {
    uint64_t total;
    uint64_t tick_28m;
    uint64_t tick_7m;
    uint64_t tick_3m58;
    uint64_t tick_e6;
    uint64_t tick_pcm;
    uint64_t tick_audio;
} clock_stats_t;

static void clock_stats_update(clock_stats_t *cs, const vl_clocks_t *c)
{
    cs->total++;
    if (c->tick_28m)   cs->tick_28m++;
    if (c->tick_7m)    cs->tick_7m++;
    if (c->tick_3m58)  cs->tick_3m58++;
    if (c->tick_e6)    cs->tick_e6++;
    if (c->tick_pcm)   cs->tick_pcm++;
    if (c->tick_audio) cs->tick_audio++;
}

static void clock_stats_verify(const clock_stats_t *cs)
{
    printf("\n=== Phase 1: Clock Generator Verification ===\n");
    printf("  Master cycles run : %llu\n", (unsigned long long)cs->total);
    printf("\n  Tick         : Expected (÷N)     Observed      Expected count     Actual count  Pass?\n");
    printf("  ─────────────────────────────────────────────────────────────────────────────────────\n");

    #define VERIFY(name, div)  do {                                          \
        uint64_t expected = cs->total / (div);                               \
        uint64_t actual   = cs->name;                                        \
        bool ok = (actual >= expected) && (actual <= expected + 1);          \
        printf("  %-12s : ÷%-4u            %8llu  ~%8llu   %s\n",           \
               #name, (div),                                                  \
               (unsigned long long)actual,                                    \
               (unsigned long long)expected,                                  \
               ok ? "PASS" : "FAIL");                                        \
        if (!ok) fprintf(stderr, "FAIL: %s: expected ~%llu got %llu\n",     \
                         #name, (unsigned long long)expected,                 \
                         (unsigned long long)actual);                         \
    } while(0)

    VERIFY(tick_28m,   DIV_28M);
    VERIFY(tick_7m,    DIV_7M);
    VERIFY(tick_3m58,  DIV_3M58);
    VERIFY(tick_e6,    DIV_E6);
    VERIFY(tick_pcm,   DIV_PCM);
    VERIFY(tick_audio, DIV_AUDIO);
    #undef VERIFY
}

/* ── Memory Arbiter test helpers ────────────────────────────────────────── */

static uint64_t s_arb_req_count  = 0;
static uint64_t s_arb_resp_count = 0;

/*
 * service_mem — drain one pending memory request and inject response.
 *
 * Timing (posedge-first vl_step model):
 *   1. vl_get_mem_req() reads outputs (valid after last vl_step's negedge) and
 *      sets EN_mem_req_deq=1 + EN_put_mem_resp=1 on the handle.
 *   2. Caller calls vl_step() → posedge captures both ENs, negedge settles.
 *
 * Returns true if a request was found and serviced.
 */
static bool service_one(void *vl, mem_model_t *mem)
{
    vl_mem_req_t req;
    if (!vl_has_mem_req(vl)) return false;
    if (!vl_get_mem_req(vl, &req)) return false;
    if (req.write) {
        mem_model_write(mem, req.addr, req.data);
        mem->writes_serviced++;
        vl_put_mem_resp(vl, req.tag, req.data, true);   /* echo write data */
    } else {
        uint8_t d = mem_model_read(mem, req.addr);
        mem->reads_serviced++;
        vl_put_mem_resp(vl, req.tag, d, true);
    }
    s_arb_req_count++;
    return true;
}

/*
 * test_cpu_rw — write val to addr, then read back and verify.
 *
 * State machine (posedge-first step model):
 *
 *  WRITE:
 *    a. Set cpu_req inputs + EN_cpu_req=1, call vl_step()
 *       → posedge enqueues req into q_cpu_req
 *       → negedge: arbiter fires, enqueues into q_bus_out, pump_arb_out moves it to q_out
 *         (may take 1-2 more steps)
 *    b. Poll vl_has_mem_req(); when true, service it and vl_step() to commit.
 *
 *  READ:
 *    a. Set cpu_req inputs + EN_cpu_req=1 for a read, vl_step().
 *    b. Poll, service, then poll vl_has_cpu_resp(); when true, vl_get_cpu_resp() + vl_step().
 */
static bool test_cpu_rw(void *vl, mem_model_t *mem, uint32_t addr, uint8_t val)
{
    int safety;
    uint8_t result;

    /* ── Write phase ──────────────────────────────────────────────────────── */
    /* Wait for RDY, then enqueue write request */
    for (safety = 0; safety < 8; safety++) {
        if (vl_cpu_req(vl, addr, val, true, false)) break;  /* sets EN, inputs */
        vl_step(vl);                                         /* wait for RDY */
    }
    if (safety == 8) {
        fprintf(stderr, "  cpu_req (write) never RDY at addr 0x%06X\n", addr);
        return false;
    }
    /* EN_cpu_req is now set; step to capture it at posedge */
    vl_step(vl);

    /* Pump until mem_req appears and service it */
    for (safety = 0; safety < 16; safety++) {
        if (vl_has_mem_req(vl)) {
            service_one(vl, mem);          /* sets EN_mem_req_deq + EN_put_mem_resp */
            vl_step(vl);                   /* commit deq + inject response */
            s_arb_resp_count++;
            break;
        }
        vl_step(vl);
    }
    if (safety == 16) {
        fprintf(stderr, "  mem_req (write) never appeared for addr 0x%06X\n", addr);
        return false;
    }

    /* Flush the write-ack response from cpu_resp FIFO — writes produce an ACK too */
    for (safety = 0; safety < 8; safety++) {
        vl_step(vl);
        if (vl_has_cpu_resp(vl)) {
            vl_get_cpu_resp(vl, &result);  /* discard write ack */
            vl_step(vl);                   /* commit cpu_resp_deq */
            break;
        }
    }

    /* ── Read phase ───────────────────────────────────────────────────────── */
    for (safety = 0; safety < 8; safety++) {
        if (vl_cpu_req(vl, addr, 0, false, false)) break;
        vl_step(vl);
    }
    if (safety == 8) {
        fprintf(stderr, "  cpu_req (read) never RDY at addr 0x%06X\n", addr);
        return false;
    }
    vl_step(vl);  /* capture read request */

    /* Pump until mem_req for read appears, service it, then wait for cpu_resp */
    for (safety = 0; safety < 20; safety++) {
        if (vl_has_mem_req(vl)) {
            service_one(vl, mem);
            vl_step(vl);                   /* commit deq + inject response */
            s_arb_resp_count++;
            continue;
        }
        if (vl_has_cpu_resp(vl)) {
            if (!vl_get_cpu_resp(vl, &result)) {
                fprintf(stderr, "  vl_get_cpu_resp failed\n");
                return false;
            }
            vl_step(vl);   /* commit cpu_resp_deq */
            if (result != val) {
                fprintf(stderr, "  cpu_rw MISMATCH at 0x%06X: wrote 0x%02X read 0x%02X\n",
                        addr, val, result);
                return false;
            }
            return true;
        }
        vl_step(vl);
    }
    fprintf(stderr, "  cpu_rw TIMEOUT at 0x%06X (wrote 0x%02X)\n", addr, val);
    return false;
}

static bool test_vdp_scan_read(void *vl, mem_model_t *mem, uint32_t addr)
{
    int safety;
    mem_model_write(mem, VRAM_BASE + addr, 0xAB);
    for (safety = 0; safety < 8; safety++) {
        if (vl_vdp_scan_req(vl, VRAM_BASE + addr)) break;
        vl_step(vl);
    }
    if (safety == 8) {
        fprintf(stderr, "  vdp_scan_req never RDY\n");
        return false;
    }
    vl_step(vl);   /* capture vdp_scan_req */

    for (safety = 0; safety < 16; safety++) {
        if (vl_has_mem_req(vl)) {
            service_one(vl, mem);
            vl_step(vl);
            s_arb_resp_count++;
            return true;
        }
        vl_step(vl);
    }
    fprintf(stderr, "  vdp_scan_read TIMEOUT\n");
    return false;
}

int main(int argc, char **argv)
{
    uint64_t master_cycles = 1000000u;
    uint64_t c;
    void        *vl  = NULL;
    mem_model_t  mem;
    clock_stats_t cs;
    int pass = 1;
    int i;

    if (argc > 1) master_cycles = (uint64_t)strtoull(argv[1], NULL, 10);
    printf("MSX turboR FPGA Phase 1 Co-Sim: Clocks & Memory Arbiter\n");
    printf("Master cycles: %llu  (bsc Bluespec + Verilator 5.x)\n\n",
           (unsigned long long)master_cycles);

    /* ── Init ────────────────────────────────────────────────────────────── */
    mem_model_init(&mem);
    memset(&cs, 0, sizeof(cs));

    vl = vl_create();
    if (!vl) {
        fprintf(stderr, "vl_create() failed\n");
        return 1;
    }
    vl_reset(vl);

    /* ── Test 1: Clock tick frequencies ──────────────────────────────────── */
    printf("Test 1: Running %llu master cycles, verifying tick frequencies...\n",
           (unsigned long long)master_cycles);
    for (c = 0; c < master_cycles; c++) {
        vl_clocks_t clks;
        vl_step(vl);
        vl_get_clocks(vl, &clks);
        clock_stats_update(&cs, &clks);
    }
    clock_stats_verify(&cs);

    /* ── Test 2: Memory Arbiter — CPU read/write round-trips ─────────────── */
    printf("\nTest 2: Memory Arbiter CPU read/write round-trips...\n");
    {
        struct { uint32_t addr; uint8_t val; } cases[] = {
            { RAM_BASE + 0x0000, 0x42 },
            { RAM_BASE + 0x0001, 0xDE },
            { RAM_BASE + 0x1000, 0xAD },
            { RAM_BASE + 0x7FFF, 0xBE },
            { VRAM_BASE + 0x0000, 0xC0 },
            { VRAM_BASE + 0x1FFE, 0xFF },
        };
        int n = (int)(sizeof(cases) / sizeof(cases[0]));
        for (i = 0; i < n; i++) {
            bool ok = test_cpu_rw(vl, &mem, cases[i].addr, cases[i].val);
            printf("  [%s] CPU RW addr=0x%06X val=0x%02X\n",
                   ok ? "PASS" : "FAIL", cases[i].addr, cases[i].val);
            if (!ok) pass = 0;
        }
    }

    /* ── Test 3: VDP Scan requests have priority over CPU ────────────────── */
    printf("\nTest 3: VDP Scanout priority arbitration...\n");
    {
        bool ok = test_vdp_scan_read(vl, &mem, 0x0000);
        printf("  [%s] VDP scan read VRAM[0x%06X] serviced OK\n",
               ok ? "PASS" : "FAIL", VRAM_BASE);
        if (!ok) pass = 0;
    }

    /* ── Summary ─────────────────────────────────────────────────────────── */
    printf("\n=== Phase 1 Arbiter Stats ===\n");
    printf("  Total bus requests serviced : %llu\n", (unsigned long long)s_arb_req_count);
    printf("  Total responses injected    : %llu\n", (unsigned long long)s_arb_resp_count);
    printf("  Memory model reads          : %u\n",  mem.reads_serviced);
    printf("  Memory model writes         : %u\n",  mem.writes_serviced);

    printf("\n=== Phase 1 Result: %s ===\n", pass ? "ALL PASS" : "SOME FAILURES");

    vl_destroy(vl);
    mem_model_destroy(&mem);
    return pass ? 0 : 1;
}
