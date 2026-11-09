/*
 * ps2_iopram.c — Cleanroom EE->IOP RAM window for the PlayStation 2 port
 *
 * Measurement first: the window is only useful if the EE can both read and
 * write it while the IOP is running, and if the bytes we claim stay ours.
 * Nothing here guesses: every field of the result struct is a value that came
 * back off the bus.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_iopram.h"
#include "ps2_irq.h"          /* btron_ee_ticks_per_us(), the image's one microsecond */

/* EE wall clock helpers, implemented in boot_ps2.s */
extern uint32_t ps2_count_read(void);

/* This driver's waits are in the microsecond the INPUT plane measures at boot
 * (src/drivers/ps2/ps2_irq.h).  The 147 it used to carry was the core clock's datasheet
 * speaking -- half of 294.912 MHz -- and this machine's Count turns out to tick at twice
 * that, so a reset budget written as 20 ms was being spent as 10 and every timeout here
 * was half as patient as it read. */

static int s_count_runs = -1;

static int iopram_count_running(void)
{
    if (s_count_runs < 0) {
        uint32_t a = ps2_count_read();
        for (volatile int i = 0; i < 8000; i++) { }
        s_count_runs = (ps2_count_read() != a) ? 1 : 0;
    }
    return s_count_runs;
}

static void iopram_wait_us(uint32_t us)
{
    if (!iopram_count_running()) {
        for (volatile uint32_t i = 0; i < us * 50u; i++) { __asm__ volatile("nop"); }
        return;
    }
    uint32_t start = ps2_count_read();
    uint32_t budget = us * btron_ee_ticks_per_us();
    for (uint32_t spins = 0; (uint32_t)(ps2_count_read() - start) < budget; spins++) {
        if (spins > 20000000u) break;
    }
}

volatile uint8_t *ps2_iopram_window(void)
{
    return (volatile uint8_t *)(uintptr_t)PS2_IOP_KSEG1;
}

volatile uint32_t *ps2_iopram_at(uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)(PS2_IOP_KSEG1 +
                                            (off & (PS2_IOP_RAM_BYTES - 1u)));
}

uint32_t ps2_iopram_read32(uint32_t off)
{
    return *ps2_iopram_at(off);
}

void ps2_iopram_write32(uint32_t off, uint32_t value)
{
    *ps2_iopram_at(off) = value;
}

#define SHADOW_WORDS (PS2_IOP_SHADOW_SIZE / 4u)
#define GUARD_WORDS  (SHADOW_WORDS - (PS2_IOP_GUARD_OFF / 4u))
/* Word i of the claimed block carries this tag plus its index, so a partial
 * overwrite names itself. */
#define CANARY_TAG   0xB70A0000u

static ps2_iopram_t s_iopram;
static uint32_t s_shadow_off = 0;
static uint32_t s_clobbered_at = 0xFFFFFFFFu;

static int block_is_zero(uint32_t off)
{
    volatile uint32_t *w = ps2_iopram_at(off);
    for (uint32_t i = 0; i < SHADOW_WORDS; i++) {
        if (w[i] != 0u) return 0;
    }
    return 1;
}

/* The IOP's exception-vector page holds jump instructions installed by its
 * kernel, so a window that decodes reads neither all-zero nor all-ones there.
 * That was the first guess at how to prove the window, and it is no good: one
 * unused vector slot reads zero, which zeroes the AND of the sample and fails a
 * window that is working perfectly.  Decoding is instead proven the only way
 * that cannot depend on what the IOP happens to have stored, by putting a word
 * through the window and reading it back. */
static int scratch_word_decodes(uint32_t off)
{
    volatile uint32_t *w = ps2_iopram_at(off);
    uint32_t was = *w;
    *w = ~was;
    int ok = (*w == ~was);
    *w = was;
    return ok;
}

static volatile uint32_t *guard_word(uint32_t i)
{
    return ps2_iopram_at(s_shadow_off + PS2_IOP_GUARD_OFF) + i;
}

/* What the fill pass leaves behind at guard word i. */
#define GUARD_CANARY(i) (CANARY_TAG + (PS2_IOP_GUARD_OFF / 4u) + (i))

static int shadow_fill_and_check(uint32_t off)
{
    volatile uint32_t *w = ps2_iopram_at(off);
    int ok = 1;
    for (uint32_t i = 0; i < SHADOW_WORDS; i++) w[i] = CANARY_TAG + i;
    __asm__ volatile("" : : : "memory");
    for (uint32_t i = 0; i < SHADOW_WORDS; i++) {
        if (w[i] != CANARY_TAG + i) ok = 0;
    }
    return ok;
}

int ps2_iopram_claim(ps2_iopram_t *out)
{
    ps2_iopram_t p;
    for (unsigned i = 0; i < sizeof(p); i++) ((uint8_t *)&p)[i] = 0;

    p.first_read = ps2_iopram_read32(0x80u);   /* IOP vector page, for the log */

#if PS2_IOP_SHADOW_OFF != 0u
    p.off = PS2_IOP_SHADOW_OFF;
#else
    /* Walk down from just below the CDVD bounce (0x1F0000).  The IOP
     * kernel stacks sit at the very top; FILEIO's 16 KiB xfer and the
     * CDVD sector pad live in 0x1F0000..0x1F7FFF.  A USB HCCA there
     * dies on the first PAK read and the pointer freezes. */
    p.off = 0;
    for (uint32_t cand = 0x1E8000u; cand >= 0x100000u; cand -= PS2_IOP_SHADOW_SIZE) {
        p.granules++;
        if (cand < 0x1F8000u && cand + PS2_IOP_SHADOW_SIZE > 0x1F0000u)
            continue;
        if (block_is_zero(cand)) { p.off = cand; break; }
        if (cand < PS2_IOP_SHADOW_SIZE) break;
    }
#endif

    s_shadow_off = p.off;
    if (p.off != 0) {
        /* Fill and settle-check only once one word has come back changed: a
         * window that does not decode should not be walked over at all. */
        p.readable = scratch_word_decodes(p.off);
        p.canary   = *guard_word(0);
        if (p.readable) {
            p.write_ok = shadow_fill_and_check(p.off);
            iopram_wait_us(20000);
            p.after_settle = *guard_word(0);
            p.survived = (p.after_settle == GUARD_CANARY(0));
        }
    }

    if (!(p.readable && p.write_ok && p.survived)) s_shadow_off = 0;
    s_iopram = p;
    s_clobbered_at = 0xFFFFFFFFu;
    if (out) *out = p;
    return (s_shadow_off != 0);
}

const ps2_iopram_t *ps2_iopram_last(void) { return &s_iopram; }

uint32_t ps2_iopram_shadow_off(void) { return s_shadow_off; }

/* Cheap spot check, called from the USB poll loop: a shadow the IOP took back
 * mid-session has to be reported rather than walked as descriptors. */
int ps2_iopram_shadow_intact(void)
{
    if (!s_shadow_off) return 0;
    for (uint32_t i = 0; i < GUARD_WORDS; i += 8u) {
        if (*guard_word(i) != GUARD_CANARY(i)) {
            if (s_clobbered_at == 0xFFFFFFFFu) s_clobbered_at = i;
            return 0;
        }
    }
    return 1;
}

/* Byte offset inside the claimed block of the first guard word that came back
 * wrong, or ~0 if the block never lied. */
uint32_t ps2_iopram_clobbered_at(void) { return s_clobbered_at; }

extern void ps2_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void ps2_iopram_log(const ps2_iopram_t *r)
{
    if (!r) return;
    ps2_kprintf("[SBUS] iop-ram@0x%08x decode=%u vector@0x80=0x%08x granules=%u\n",
                (unsigned int)PS2_IOP_KSEG1, (unsigned int)r->readable,
                (unsigned int)r->first_read, (unsigned int)r->granules);
#if BTRON_HID_TRACE
    ps2_kprintf("[SBUS] shadow %u B @0x%06x: canary 0x%08x->0x%08x write=%u settle=%u\n",
                (unsigned int)PS2_IOP_SHADOW_SIZE, (unsigned int)r->off,
                (unsigned int)r->canary, (unsigned int)r->after_settle,
                (unsigned int)r->write_ok, (unsigned int)r->survived);
#endif
}
