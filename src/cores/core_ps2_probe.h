/* ── BTRON PlayStation 2 Core Diagnostic Probe & Benchmark Definitions ──── */
#ifndef CORE_PS2_PROBE_H
#define CORE_PS2_PROBE_H

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include "ps2_usb.h"
#include "ps2_gs_present.h"

#if defined(BTRON_PS2_PROBE) || defined(BTRON_PS2_BENCH)

#define PS2_STACK_PROBE_BYTES (512u * 1024u)
#define PS2_STACK_PROBE_MARK  0xA55AA55Au

/* Cycle profiler causes */
#define PS2_CYCLE_WHY_EVENT     1u
#define PS2_CYCLE_WHY_TIMER     2u
#define PS2_CYCLE_WHY_GMENU     4u
#define PS2_CYCLE_WHY_TRACKER   8u
#define PS2_CYCLE_WHY_WM       16u
#define PS2_CYCLE_WHY_TIP      32u
#define PS2_CYCLE_WHY_OVERFLOW 64u
#define PS2_CYCLE_WHY_MERGED  128u
#define PS2_CYCLE_WHY_ROTATE  256u
#define PS2_CYCLE_WHY_PAINTW  512u

/* Forward declarations of diagnostic probes and benchmarks */
static void ps2_stack_probe_arm(void);
static unsigned ps2_stack_probe_depth(void);
static void ps2_log_ohci_probe(const ps2_ohci_probe_t *p);
static void ps2_gsquad_probe(void);
static void ps2_gsread_probe(void);
static void ps2_gsalpha_probe(void);
static void ps2_gstex_probe(void);
static void ps2_ptrcal_probe(void);

static void ps2_cycle_mark(void);
static void ps2_cycle_begin(int task_driven);
static void ps2_cycle_why(uint32_t bits);
static void ps2_cycle_damage(const ps2_bandset_t *set);
static void ps2_cycle_merged(const ps2_bandset_t *set);
static void ps2_cycle_stamp(uint32_t r_us, uint32_t s_us, uint32_t u_us, uint32_t px, int bands, int full);
static void ps2_cycle_print(int asked_ms);
static uint32_t ps2_cycle_hold_begin(void);
static void ps2_cycle_hold_end(uint32_t t0, int asked_ms);

void ps2_shell_gsstat(void);
void ps2_shell_page(const char *arg);
void ps2_shell_paintw(const char *arg);

#if BTRON_PS2_BENCH_GS >= 5
static void ps2_bench_dump_groups(void);
static void ps2_bench_gs_race_probe(const RECT *cl, int cut);
#endif

#if BTRON_PS2_BENCH
static void ps2_bench_kbd_cadence(void);
static void ps2_bench_run(GDEV *screen);
#if BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6
static void ps2_bench_gs_census(void);
#endif
#if BTRON_PS2_BENCH_APP
static int s_bench_app_active;
static void ps2_bench_app_frame_hook(GDEV *screen);
#endif
#endif

#else /* Release builds: zero-overhead probe & cycle stubs */

#define PS2_STACK_PROBE_BYTES (512u * 1024u)
#define PS2_CYCLE_WHY_EVENT     1u
#define PS2_CYCLE_WHY_TIMER     2u
#define PS2_CYCLE_WHY_GMENU     4u
#define PS2_CYCLE_WHY_TRACKER   8u
#define PS2_CYCLE_WHY_WM       16u
#define PS2_CYCLE_WHY_TIP      32u
#define PS2_CYCLE_WHY_OVERFLOW 64u
#define PS2_CYCLE_WHY_MERGED  128u
#define PS2_CYCLE_WHY_ROTATE  256u
#define PS2_CYCLE_WHY_PAINTW  512u

#define ps2_stack_probe_arm()        ((void)0)
#define ps2_stack_probe_depth()      (0u)
#define ps2_log_ohci_probe(p)        ((void)(p))
#define ps2_cycle_mark()             ((void)0)
#define ps2_cycle_begin(td)          ((void)(td))
#define ps2_cycle_why(bits)          ((void)(bits))
#define ps2_cycle_damage(set)        ((void)(set))
#define ps2_cycle_merged(set)        ((void)(set))
#define ps2_cycle_stamp(r,s,u,p,b,f) ((void)0)
#define ps2_cycle_print(ms)          ((void)(ms))
#define ps2_cycle_hold_begin()       (0u)
#define ps2_cycle_hold_end(t0, ms)   ((void)(t0), (void)(ms))

#endif /* BTRON_PS2_PROBE || BTRON_PS2_BENCH */

#endif /* CORE_PS2_PROBE_H */
