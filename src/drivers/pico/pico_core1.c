/*
 * pico_core1.c — Wake core 1. The span jobs travel on a Nexus SPSC ring.
 *
 * A cold PicoCalc leaves core 1 in the bootrom. The wake is the bootrom
 * sequence 0, 0, 1, VTOR, SP, entry, each word echoed. If core 1 has already
 * entered Reset_Handler, that park takes one thumb pointer instead.
 *
 * Core 0 posts triangles with snd and does not wait inside the ring. A full
 * ring refuses and counts the refusal. The completion check is reap, at the
 * next yield (a full ring, or the frame being read), not on every triangle.
 * The integer span does not use the hardware divider. If the wake fails,
 * core 0 paints the same span.
 */
#include <stdint.h>
#include "../../desktop/graphics/picogl/picogl.h"
#include "pico_regs.h"

#define SIO_CPUID     REG32(SIO_BASE + 0x000u)
#define SIO_FIFO_ST   REG32(SIO_BASE + 0x050u)
#define SIO_FIFO_WR   REG32(SIO_BASE + 0x054u)
#define SIO_FIFO_RD   REG32(SIO_BASE + 0x058u)
#define FIFO_ST_VLD   1u
#define FIFO_ST_RDY   2u
#define LAUNCH_US     100000u
#define ECHO_US       2000u
#define REAP_US       200000u

extern uint8_t __vectors_start__[];
extern uint8_t __core1_stack_top__[];

static volatile int s_ready;
static int s_giveup;
static int s_tries;
static int s_reap_fail;

static void sev(void) { __asm__ volatile("sev" ::: "memory"); }
static void wfe(void) { __asm__ volatile("wfe" ::: "memory"); }

static void fifo_drain(void)
{
    int n;
    for (n = 0; n < 8; n++) {
        if ((SIO_FIFO_ST & FIFO_ST_VLD) == 0u) break;
        (void)SIO_FIFO_RD;
    }
}

static int timed_out(uint32_t t0, uint32_t us)
{
    return (TIMER0_TIMERAWL - t0) > us;
}

/* First instruction is cpsid. A C prologue must not run before it, or a
 * pending SysTick on this VTOR would enter the core-0 scheduler. */
__attribute__((noinline, used, noreturn))
void pico_core1_body(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    s_ready = 1;
    sev();
    for (;;) {
        picogl_span_drain();
        if (picogl_span_finished() == picogl_span_posted())
            wfe();
    }
}

__attribute__((naked, used, noreturn))
void pico_core1_main(void)
{
    __asm__ volatile(
        "cpsid i\n"
        "bl pico_core1_body\n"
        "1:\n"
        "wfe\n"
        "b 1b\n"
    );
}

void pico_core1_sev(void)
{
    sev();
}

void pico_core1_reap(void)
{
    uint32_t t0;
    if (!s_ready)
        return;
    t0 = TIMER0_TIMERAWL;
    while (picogl_span_finished() != picogl_span_posted()) {
        if (timed_out(t0, REAP_US)) {
            s_reap_fail++;
            if (s_reap_fail >= 3) {
                s_ready = 0;
                s_giveup = 1;
            }
            return;
        }
        sev();
        wfe();
    }
    s_reap_fail = 0;
}

int pico_core1_run_flush(void)
{
    if (SIO_CPUID != 0u || !s_ready)
        return 0;
    pico_core1_reap();
    return s_ready ? 1 : 0;
}

int pico_core1_state(void)
{
    if (s_ready)
        return picogl_span_queued() > 0 ? 2 : 1;
    if (s_giveup)
        return 3;
    return 0;
}

unsigned pico_core1_queued(void)
{
    return (unsigned)picogl_span_queued();
}

unsigned pico_core1_refused(void)
{
    return picogl_span_refused();
}

static int fifo_push(uint32_t v, uint32_t t0, uint32_t us)
{
    while ((SIO_FIFO_ST & FIFO_ST_RDY) == 0u) {
        if (timed_out(t0, us)) return 0;
    }
    SIO_FIFO_WR = v;
    sev();
    return 1;
}

static int fifo_pop(uint32_t *out, uint32_t t0, uint32_t us)
{
    while ((SIO_FIFO_ST & FIFO_ST_VLD) == 0u) {
        if (timed_out(t0, us)) return 0;
        wfe();
    }
    *out = SIO_FIFO_RD;
    return 1;
}

void pico_core1_launch(void)
{
    uint32_t cmds[6];
    uint32_t t0, seq, resp;
    uintptr_t entry;

    if (SIO_CPUID != 0u || s_ready || s_giveup)
        return;
    if (s_tries >= 3) {
        s_giveup = 1;
        return;
    }
    s_tries++;
    entry = (uintptr_t)pico_core1_main;
    cmds[0] = 0u;
    cmds[1] = 0u;
    cmds[2] = 1u;
    cmds[3] = (uint32_t)(uintptr_t)__vectors_start__;
    cmds[4] = (uint32_t)(uintptr_t)__core1_stack_top__;
    cmds[5] = (uint32_t)entry | 1u;

    t0 = TIMER0_TIMERAWL;
    seq = 0u;
    while (seq < 6u) {
        uint32_t cmd = cmds[seq];
        uint32_t budget = (seq == 0u) ? ECHO_US : LAUNCH_US;
        if (timed_out(t0, budget))
            break;
        if (cmd == 0u) {
            fifo_drain();
            sev();
        }
        if (!fifo_push(cmd, t0, budget))
            break;
        if (!fifo_pop(&resp, t0, budget))
            break;
        seq = (resp == cmd) ? seq + 1u : 0u;
    }

    if (seq < 6u && !s_ready) {
        /* No echo. Core 1 may be in the user FIFO park, which takes one
         * thumb pointer and ignores the bootrom words. */
        t0 = TIMER0_TIMERAWL;
        fifo_drain();
        (void)fifo_push((uint32_t)entry | 1u, t0, LAUNCH_US);
    }

    t0 = TIMER0_TIMERAWL;
    while (!s_ready) {
        if (timed_out(t0, LAUNCH_US))
            break;
        wfe();
    }
    if (!s_ready && s_tries >= 3)
        s_giveup = 1;
}
