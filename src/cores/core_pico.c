/*
 * core_pico.c — Freestanding Clean-Room Cortex-M RTOS Core for B-System
 *
 * Target: Raw Pico Series QEMU (`make run-pico` / `make test-pico`) AND
 *         Base RTOS Kernel for ClockworkPi PicoCalc (`make picocalc`).
 *
 * Design Invariants (doc/txt/PICO.txt):
 *   - 100% freestanding, zero libc, zero post-boot heap (NASA JPL Rule 3).
 *   - Preemptive priority + round-robin scheduler via Cortex-M PendSV & SysTick.
 *   - Battery-first execution: dedicated Idle task sleeps in `wfi`.
 *   - Shared shell engine (`pico_shell_exec`) reused by UART0 and SegUI Terminal.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/pico_rtos.h>
#include "pico_hal.h"

extern uint8_t __data_start__[];
extern uint8_t __data_end__[];
extern uint8_t __bss_start__[];
extern uint8_t __bss_end__[];

/* ── Minimal Freestanding String & Formatting Helpers ──────────────────── */

static int k_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int k_starts_with(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++) return 0;
    }
    return 1;
}

void pico_u32_to_dec(uint32_t v, char *buf, size_t cap)
{
    char tmp[12];
    size_t n = 0, i;
    if (!buf || cap == 0) return;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    for (i = 0; i < n && i + 1 < cap; i++) {
        buf[i] = tmp[n - 1u - i];
    }
    buf[i] = '\0';
}

void pico_u32_to_hex(uint32_t v, char *buf, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    if (!buf || cap < 11) return;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> (28u - 4u * i)) & 0xFu];
    }
    buf[10] = '\0';
}

static void uart_out_cb(const char *s, void *ctx)
{
    (void)ctx;
    pico_hal_uart_puts(s);
}

static void out_dec(pico_out_fn out, void *ctx, uint32_t v)
{
    char buf[16];
    pico_u32_to_dec(v, buf, sizeof(buf));
    out(buf, ctx);
}

/* Fixed columns for the 40-character SegUI terminal (8px glyph, 320px). */
static void out_spaces(pico_out_fn out, void *ctx, int n)
{
    while (n-- > 0) out(" ", ctx);
}

static void out_field(pico_out_fn out, void *ctx, const char *s, int width, int right)
{
    int n = 0;
    if (!s) s = "";
    while (s[n]) n++;
    if (right && n < width) out_spaces(out, ctx, width - n);
    out(s, ctx);
    if (!right && n < width) out_spaces(out, ctx, width - n);
}

static void out_num(pico_out_fn out, void *ctx, uint32_t v, int width)
{
    char buf[12];
    pico_u32_to_dec(v, buf, sizeof(buf));
    out_field(out, ctx, buf, width, 1);
}

/* Picocalc links the strong copies in pico_core1.c. QEMU has no second core. */
__attribute__((weak)) int pico_core1_state(void) { return 0; }
__attribute__((weak)) unsigned pico_core1_queued(void) { return 0; }
__attribute__((weak)) unsigned pico_core1_refused(void) { return 0; }

static void out_hex(pico_out_fn out, void *ctx, uint32_t v)
{
    char buf[16];
    pico_u32_to_hex(v, buf, sizeof(buf));
    out(buf, ctx);
}

/* ── Critical Section Helpers (Cortex-M PRIMASK) ───────────────────────── */

static inline uint32_t irq_save(void)
{
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r"(primask) :: "memory");
    return primask;
}

static inline void irq_restore(uint32_t primask)
{
    __asm__ volatile ("msr primask, %0" :: "r"(primask) : "memory");
}

static inline void trigger_pendsv(void)
{
    CM_SCB_ICSR = CM_ICSR_PENDSVSET;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

/* ── Static Task Control Blocks & Stacks ───────────────────────────────── */

typedef struct {
    uint32_t       *psp;
    const char     *name;
    uint8_t         prio;        /* Lower number = higher priority; 255 = idle */
    PICO_TSK_STATE  state;
    uint32_t        wake_tick;
    PICO_SEM       *wait_sem;
    PICO_FLG       *wait_flg;
    uint32_t        wait_mask;
    uint32_t        switches;
} PICO_TCB;

static PICO_TCB s_tcb[PICO_MAX_TASKS];
static uint32_t s_stacks[PICO_MAX_TASKS][PICO_TASK_STACK_WORDS] __attribute__((aligned(8)));
static volatile int      s_cur_tid    = -1;
static volatile uint32_t s_ticks_ms   = 0;
static volatile uint32_t s_idle_wfi   = 0;
static volatile uint32_t s_total_sw   = 0;

static void task_exit_trap(void)
{
    uint32_t key = irq_save();
    if (s_cur_tid >= 0 && s_cur_tid < PICO_MAX_TASKS) {
        s_tcb[s_cur_tid].state = PICO_TSK_DORMANT;
    }
    irq_restore(key);
    trigger_pendsv();
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

int pico_tsk_create(const char *name, uint8_t prio, pico_task_fn fn, void *arg)
{
    int tid = -1;
    uint32_t key;
    uint32_t *sp;

    if (!fn) return -1;
    key = irq_save();
    for (int i = 0; i < PICO_MAX_TASKS; i++) {
        if (s_tcb[i].state == PICO_TSK_UNUSED || s_tcb[i].state == PICO_TSK_DORMANT) {
            tid = i;
            break;
        }
    }
    if (tid < 0) {
        irq_restore(key);
        return -1;
    }

    /* Build initial Cortex-M exception frame + software-saved {r4-r11} */
    sp = &s_stacks[tid][PICO_TASK_STACK_WORDS];
    /* Hardware-stacked frame (popped on EXC_RETURN=0xFFFFFFFD) */
    *(--sp) = 0x01000000UL;               /* xPSR (Thumb bit T=1)          */
    *(--sp) = ((uint32_t)(uintptr_t)fn) & ~1UL; /* PC                      */
    *(--sp) = (uint32_t)(uintptr_t)task_exit_trap; /* LR                   */
    *(--sp) = 0;                          /* R12                           */
    *(--sp) = 0;                          /* R3                            */
    *(--sp) = 0;                          /* R2                            */
    *(--sp) = 0;                          /* R1                            */
    *(--sp) = (uint32_t)(uintptr_t)arg;   /* R0 (task argument)            */
    /* Software-stacked frame (popped by PendSV_Handler: r4-r11) */
    for (int r = 0; r < 8; r++) {
        *(--sp) = 0;
    }

    s_tcb[tid].psp       = sp;
    s_tcb[tid].name      = name ? name : "task";
    s_tcb[tid].prio      = prio;
    s_tcb[tid].state     = PICO_TSK_READY;
    s_tcb[tid].wake_tick = 0;
    s_tcb[tid].wait_sem  = NULL;
    s_tcb[tid].wait_flg  = NULL;
    s_tcb[tid].wait_mask = 0;
    s_tcb[tid].switches  = 0;

    irq_restore(key);
    return tid;
}

uint32_t pico_ticks_ms(void)
{
    return s_ticks_ms;
}

uint32_t pico_idle_wfi_count(void)
{
    return s_idle_wfi;
}

void pico_tsk_yield(void)
{
    trigger_pendsv();
}

void pico_tsk_delay(uint32_t ms)
{
    uint32_t key;
    if (ms == 0) {
        trigger_pendsv();
        return;
    }
    key = irq_save();
    if (s_cur_tid > 0 && s_cur_tid < PICO_MAX_TASKS) {
        s_tcb[s_cur_tid].wake_tick = s_ticks_ms + ms;
        s_tcb[s_cur_tid].state     = PICO_TSK_DELAYED;
    }
    irq_restore(key);
    trigger_pendsv();
}

/* ── IPC: Semaphore & EventFlag ────────────────────────────────────────── */

void pico_sem_init(PICO_SEM *sem, int32_t init_count, int32_t max_count)
{
    if (!sem) return;
    sem->count     = init_count;
    sem->max_count = max_count > 0 ? max_count : 1;
}

int pico_sem_wait(PICO_SEM *sem, uint32_t timeout_ms)
{
    uint32_t deadline = s_ticks_ms + timeout_ms;
    if (!sem) return -1;
    for (;;) {
        uint32_t key = irq_save();
        if (sem->count > 0) {
            sem->count--;
            irq_restore(key);
            return 0;
        }
        if (timeout_ms != 0xFFFFFFFFUL && (int32_t)(s_ticks_ms - deadline) >= 0) {
            irq_restore(key);
            return -1;
        }
        if (s_cur_tid > 0) {
            s_tcb[s_cur_tid].wait_sem  = sem;
            s_tcb[s_cur_tid].wake_tick = (timeout_ms == 0xFFFFFFFFUL) ? 0u : deadline;
            s_tcb[s_cur_tid].state     = PICO_TSK_WAITSEM;
        }
        irq_restore(key);
        trigger_pendsv();
    }
}

void pico_sem_signal(PICO_SEM *sem)
{
    uint32_t key;
    int woke = 0;
    if (!sem) return;
    key = irq_save();
    if (sem->count < sem->max_count) {
        sem->count++;
    }
    for (int i = 1; i < PICO_MAX_TASKS; i++) {
        if (s_tcb[i].state == PICO_TSK_WAITSEM && s_tcb[i].wait_sem == sem) {
            s_tcb[i].wait_sem = NULL;
            s_tcb[i].state    = PICO_TSK_READY;
            woke = 1;
            break;
        }
    }
    irq_restore(key);
    if (woke) {
        trigger_pendsv();
    }
}

void pico_flg_init(PICO_FLG *flg, uint32_t init_bits)
{
    if (!flg) return;
    flg->bits = init_bits;
}

void pico_flg_set(PICO_FLG *flg, uint32_t pattern)
{
    uint32_t key;
    int woke = 0;
    if (!flg) return;
    key = irq_save();
    flg->bits |= pattern;
    for (int i = 1; i < PICO_MAX_TASKS; i++) {
        if (s_tcb[i].state == PICO_TSK_WAITFLG &&
            s_tcb[i].wait_flg == flg &&
            (flg->bits & s_tcb[i].wait_mask) != 0u) {
            s_tcb[i].wait_flg = NULL;
            s_tcb[i].state    = PICO_TSK_READY;
            woke = 1;
        }
    }
    irq_restore(key);
    if (woke) {
        trigger_pendsv();
    }
}

uint32_t pico_flg_wait(PICO_FLG *flg, uint32_t mask, int clear_on_exit, uint32_t timeout_ms)
{
    uint32_t deadline = s_ticks_ms + timeout_ms;
    if (!flg || mask == 0u) return 0u;
    for (;;) {
        uint32_t key = irq_save();
        uint32_t hit = flg->bits & mask;
        if (hit != 0u) {
            if (clear_on_exit) {
                flg->bits &= ~hit;
            }
            irq_restore(key);
            return hit;
        }
        if (timeout_ms != 0xFFFFFFFFUL && (int32_t)(s_ticks_ms - deadline) >= 0) {
            irq_restore(key);
            return 0u;
        }
        if (s_cur_tid > 0) {
            s_tcb[s_cur_tid].wait_flg  = flg;
            s_tcb[s_cur_tid].wait_mask = mask;
            s_tcb[s_cur_tid].wake_tick = (timeout_ms == 0xFFFFFFFFUL) ? 0u : deadline;
            s_tcb[s_cur_tid].state     = PICO_TSK_WAITFLG;
        }
        irq_restore(key);
        trigger_pendsv();
    }
}

/* ── Scheduler & SysTick ISR ───────────────────────────────────────────── */

void pico_systick_isr(void)
{
    uint32_t now = ++s_ticks_ms;
    int need_switch = 0;

    for (int i = 1; i < PICO_MAX_TASKS; i++) {
        PICO_TCB *t = &s_tcb[i];
        if (t->state == PICO_TSK_DELAYED && (int32_t)(now - t->wake_tick) >= 0) {
            t->state = PICO_TSK_READY;
            need_switch = 1;
        } else if ((t->state == PICO_TSK_WAITSEM || t->state == PICO_TSK_WAITFLG) &&
                   t->wake_tick != 0u && (int32_t)(now - t->wake_tick) >= 0) {
            t->wait_sem = NULL;
            t->wait_flg = NULL;
            t->state    = PICO_TSK_READY;
            need_switch = 1;
        }
    }

    /* Round-robin slice every 10 ms among equal-priority ready tasks */
    if (need_switch || (now % 10u) == 0u) {
        CM_SCB_ICSR = CM_ICSR_PENDSVSET;
    }
}

uint32_t *pico_sched_switch(uint32_t *cur_psp)
{
    int cur = s_cur_tid;
    int best = 0;
    uint8_t best_prio = 255;

    if (cur >= 0 && cur < PICO_MAX_TASKS) {
        s_tcb[cur].psp = cur_psp;
        if (s_tcb[cur].state == PICO_TSK_RUNNING) {
            s_tcb[cur].state = PICO_TSK_READY;
        }
    }

    /* Scan starting after `cur` for round-robin fairness at equal priority */
    for (int off = 1; off <= PICO_MAX_TASKS; off++) {
        int idx = (cur < 0 ? off - 1 : (cur + off) % PICO_MAX_TASKS);
        PICO_TCB *t = &s_tcb[idx];
        if (t->state == PICO_TSK_READY && t->prio <= best_prio) {
            if (t->prio < best_prio || best == 0) {
                best_prio = t->prio;
                best = idx;
            }
        }
    }

    s_cur_tid = best;
    s_tcb[best].state = PICO_TSK_RUNNING;
    s_tcb[best].switches++;
    s_total_sw++;
    return s_tcb[best].psp;
}

/* ── Diagnostics & HardFault ───────────────────────────────────────────── */

uint32_t pico_static_sram_used(void)
{
    uintptr_t data_sz = (uintptr_t)__data_end__ - (uintptr_t)__data_start__;
    uintptr_t bss_sz  = (uintptr_t)__bss_end__ - (uintptr_t)__bss_start__;
    return (uint32_t)(data_sz + bss_sz);
}

int pico_active_task_count(void)
{
    int n = 0;
    for (int i = 0; i < PICO_MAX_TASKS; i++) {
        if (s_tcb[i].state != PICO_TSK_UNUSED && s_tcb[i].state != PICO_TSK_DORMANT) {
            n++;
        }
    }
    return n;
}

void pico_hardfault_c(uint32_t *frame)
{
    uint32_t pc   = frame ? frame[6] : 0;
    uint32_t lr   = frame ? frame[5] : 0;
    uint32_t psr  = frame ? frame[7] : 0;
    uint32_t cfsr = 0;
#if defined(__ARM_ARCH_8M_MAIN__)
    cfsr = CM_SCB_CFSR;
#endif
    pico_hal_uart_puts("\n[PANIC] Cortex-M HardFault!\n  PC=");
    out_hex(uart_out_cb, NULL, pc);
    pico_hal_uart_puts("  LR=");
    out_hex(uart_out_cb, NULL, lr);
    pico_hal_uart_puts("  PSR=");
    out_hex(uart_out_cb, NULL, psr);
    pico_hal_uart_puts("  CFSR=");
    out_hex(uart_out_cb, NULL, cfsr);
    pico_hal_uart_puts("\n");
    pico_hal_panic_display("HARDFAULT", pc, lr, psr, cfsr);
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

/* ── Built-In Boot Self-Test (RTOS Preemption, Timer, IPC, WFI) ────────── */

static PICO_SEM s_test_sem;
static PICO_FLG s_test_flg;
static volatile uint32_t s_worker_a_runs = 0;
static volatile uint32_t s_worker_b_runs = 0;

static void selftest_worker_a(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        s_worker_a_runs++;
        pico_sem_signal(&s_test_sem);
        pico_tsk_delay(5);
    }
    pico_flg_set(&s_test_flg, 0x01u);
}

static void selftest_worker_b(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        if (pico_sem_wait(&s_test_sem, 200) == 0) {
            s_worker_b_runs++;
        }
    }
    pico_flg_set(&s_test_flg, 0x02u);
}

static void run_selftest(pico_out_fn out, void *ctx)
{
    uint32_t t0, t1, wfi0, flg_done = 0;

    s_worker_a_runs = 0;
    s_worker_b_runs = 0;
    pico_sem_init(&s_test_sem, 0, 4);
    pico_flg_init(&s_test_flg, 0);

    t0   = pico_ticks_ms();
    wfi0 = pico_idle_wfi_count();

    pico_tsk_create("t_prod", 10, selftest_worker_a, NULL);
    pico_tsk_create("t_cons", 10, selftest_worker_b, NULL);

    /* Wait for both worker bits (0x01 and 0x02) with bounded timeout */
    for (int iter = 0; iter < 50 && flg_done != 0x03u; iter++) {
        flg_done |= pico_flg_wait(&s_test_flg, 0x03u, 1, 20);
    }
    t1 = pico_ticks_ms();

    out("[TEST] SysTick 1kHz monotonic : ", ctx);
    out((t1 > t0) ? "PASS (" : "FAIL (", ctx);
    out_dec(out, ctx, t1 - t0);
    out(" ms)\n", ctx);

    out("[TEST] PendSV + Semaphore IPC : ", ctx);
    out((s_worker_a_runs == 3u && s_worker_b_runs == 3u) ? "PASS\n" : "FAIL\n", ctx);

    out("[TEST] EventFlag rendezvous   : ", ctx);
    out((flg_done == 0x03u) ? "PASS\n" : "FAIL\n", ctx);

    out("[TEST] WFI battery idle sleep : ", ctx);
    out((pico_idle_wfi_count() > wfi0) ? "PASS (" : "FAIL (", ctx);
    out_dec(out, ctx, pico_idle_wfi_count() - wfi0);
    out(" wakes)\n", ctx);
}

/* ── Shared Interactive Shell (`btron-pico#`) ──────────────────────────── */

static const char *state_str(PICO_TSK_STATE st)
{
    switch (st) {
    case PICO_TSK_READY:   return "RDY";
    case PICO_TSK_RUNNING: return "RUN";
    case PICO_TSK_DELAYED: return "DLY";
    case PICO_TSK_WAITSEM: return "SEM";
    case PICO_TSK_WAITFLG: return "FLG";
    case PICO_TSK_DORMANT: return "DRM";
    default:               return "---";
    }
}

void pico_shell_exec(const char *line, pico_out_fn out, void *ctx)
{
    if (!line || !out) return;
    while (*line == ' ') line++;
    if (*line == '\0') return;

    if (k_streq(line, "help") || k_streq(line, "?")) {
        out("Commands: help ver mem ps ticks test reboot bootsel\n", ctx);
    } else if (k_streq(line, "ver")) {
        out("B-System/BTRON3 3.20 (pico-cleanroom) Cortex-M RTOS\n", ctx);
        out("  Board : ", ctx); out(pico_hal_board_name(), ctx); out("\n", ctx);
        out("  CPU   : ", ctx); out(pico_hal_cpu_name(), ctx); out("\n", ctx);
    } else if (k_streq(line, "mem")) {
        uint32_t used  = pico_static_sram_used();
        uint32_t total = pico_hal_board_sram_bytes();
        out("SRAM static used : ", ctx); out_dec(out, ctx, used);
        out(" B / ", ctx); out_dec(out, ctx, total);
        out(" B (free ", ctx); out_dec(out, ctx, total > used ? total - used : 0);
        out(" B, heap=0)\n", ctx);
    } else if (k_streq(line, "ps")) {
        static const char *const c1_name[] = { "OFF", "RDY", "RUN", "ERR" };
        int st = pico_core1_state();
        if (st < 0 || st > 3) st = 0;
        out("ID PRI STATE SWITCH C NAME\n", ctx);
        for (int i = 0; i < PICO_MAX_TASKS; i++) {
            if (s_tcb[i].state == PICO_TSK_UNUSED) continue;
            out_num(out, ctx, (uint32_t)i, 2);
            out(" ", ctx);
            out_num(out, ctx, s_tcb[i].prio, 3);
            out(" ", ctx);
            out_field(out, ctx, state_str(s_tcb[i].state), 5, 0);
            out(" ", ctx);
            out_num(out, ctx, s_tcb[i].switches, 6);
            out(" 0 ", ctx);
            out(s_tcb[i].name, ctx);
            out("\n", ctx);
        }
        out("CORE STATE QUEUED REF NAME\n", ctx);
        out_num(out, ctx, 0, 4);
        out(" ", ctx);
        out_field(out, ctx, "RUN", 5, 0);
        out(" ", ctx);
        out_num(out, ctx, 0, 6);
        out(" ", ctx);
        out_num(out, ctx, 0, 3);
        out(" rtos\n", ctx);
        out_num(out, ctx, 1, 4);
        out(" ", ctx);
        out_field(out, ctx, c1_name[st], 5, 0);
        out(" ", ctx);
        out_num(out, ctx, pico_core1_queued(), 6);
        out(" ", ctx);
        out_num(out, ctx, pico_core1_refused(), 3);
        out(" picogl\n", ctx);
    } else if (k_streq(line, "ticks")) {
        out("Uptime: ", ctx); out_dec(out, ctx, pico_ticks_ms());
        out(" ms | switches: ", ctx); out_dec(out, ctx, s_total_sw);
        out(" | WFI idle: ", ctx); out_dec(out, ctx, pico_idle_wfi_count());
        out("\n", ctx);
    } else if (k_streq(line, "test")) {
        run_selftest(out, ctx);
    } else if (k_streq(line, "reboot")) {
        pico_hal_reboot(0);
    } else if (k_streq(line, "bootsel")) {
        pico_hal_reboot(1);
    } else if (k_starts_with(line, "echo ")) {
        out(line + 5, ctx);
        out("\n", ctx);
    } else {
        out("Unknown command: ", ctx);
        out(line, ctx);
        out(" (try 'help')\n", ctx);
    }
}

/* ── Idle Task (Battery-First WFI) & Console Task ──────────────────────── */

static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        s_idle_wfi++;
        __asm__ volatile ("wfi");
    }
}

__attribute__((weak)) void pico_ui_start(void)
{
    /* Pure non-UI kernel (make run-pico): no display task started. */
}

static void console_task(void *arg)
{
    char line[80];
    size_t len = 0;
    (void)arg;

    run_selftest(uart_out_cb, NULL);
    pico_ui_start();

    pico_hal_uart_puts("[BOOT] Stage 1: Pico RTOS Console Active\n");
    pico_hal_uart_puts("btron-pico# ");

    for (;;) {
        int ch = pico_hal_uart_getc_nonblock();
        if (ch < 0) {
            pico_tsk_delay(10);
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            pico_hal_uart_puts("\n");
            line[len] = '\0';
            pico_shell_exec(line, uart_out_cb, NULL);
            len = 0;
            pico_hal_uart_puts("btron-pico# ");
        } else if ((ch == '\b' || ch == 0x7F) && len > 0) {
            len--;
            pico_hal_uart_puts("\b \b");
        } else if (ch >= 0x20 && ch < 0x7F && len + 1 < sizeof(line)) {
            line[len++] = (char)ch;
            pico_hal_uart_putc((char)ch);
        }
    }
}

/* ── Kernel Entry (called from startup_pico.S Reset_Handler) ───────────── */

void pico_kernel_entry(void)
{
    uint32_t reload;

    pico_hal_early_init();

    pico_hal_uart_puts("\n");
    pico_hal_uart_puts("B-System/BTRON3 3.20 (pico-cleanroom) Cortex-M RTOS\n");
    pico_hal_uart_puts("Copyright 2026 Synrc Research Center. MIT License.\n");
    pico_hal_uart_puts("[BOOT] Board : ");
    pico_hal_uart_puts(pico_hal_board_name());
    pico_hal_uart_puts("\n[BOOT] CPU   : ");
    pico_hal_uart_puts(pico_hal_cpu_name());
    pico_hal_uart_puts("\n[MEM ] Static SRAM: ");
    out_dec(uart_out_cb, NULL, pico_static_sram_used());
    pico_hal_uart_puts(" B / ");
    out_dec(uart_out_cb, NULL, pico_hal_board_sram_bytes());
    pico_hal_uart_puts(" B (zero heap)\n");

    /* Task 0 = Battery-saving WFI Idle; Task 1 = Console / Boot supervisor */
    pico_tsk_create("idle",    255, idle_task,    NULL);
    pico_tsk_create("console",  20, console_task, NULL);

    /* PendSV = lowest priority (0xFF), SysTick = 0xC0 (ARMv6-M / ARMv8-M SHPR3) */
    CM_SCB_SHPR3 = (0xC0UL << 24) | (0xFFUL << 16);

    /* Configure 1 kHz SysTick */
    reload = (pico_hal_sysclk_hz() / 1000UL) - 1UL;
    CM_SYST_CSR = 0;
    CM_SYST_RVR = reload;
    CM_SYST_CVR = 0;
    CM_SYST_CSR = CM_SYST_CLKSOURCE | CM_SYST_TICKINT | CM_SYST_ENABLE;

    /* Clear PSP to 0 so first PendSV launches Task 1 without saving MSP */
    __asm__ volatile ("msr psp, %0" :: "r"(0UL) : "memory");
    __asm__ volatile ("cpsie i" ::: "memory");

    /* Trigger initial PendSV to enter Task 1 on PSP */
    trigger_pendsv();

    for (;;) {
        __asm__ volatile ("wfi");
    }
}
