/*
 * trace_compare.c — Pure C99 Lock-Step Trace Comparator Implementation.
 */

#include "trace_compare.h"
#include <stdio.h>

void trace_compare_init(trace_stats_t *st)
{
    if (!st) return;
    st->total_instructions = 0;
    st->total_divergences = 0;
}

bool trace_compare_step(trace_stats_t *st, const trace_regs_t *hw, const trace_regs_t *ref, const char *prefix)
{
    bool match = true;
    if (!st || !hw || !ref) return false;
    st->total_instructions++;

    if (hw->pc != ref->pc || hw->a != ref->a || hw->f != ref->f ||
        hw->b  != ref->b  || hw->c != ref->c || hw->d != ref->d ||
        hw->e  != ref->e  || hw->h != ref->h || hw->l != ref->l ||
        hw->sp != ref->sp || hw->ix != ref->ix || hw->iy != ref->iy) {
        match = false;
        st->total_divergences++;

        fprintf(stderr, "\n[FATAL DIVERGENCE] %s at Instruction #%llu:\n",
                prefix ? prefix : "Trace", (unsigned long long)st->total_instructions);
        fprintf(stderr, "  HW : PC=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X\n",
                hw->pc, hw->a, hw->f, hw->b, hw->c, hw->d, hw->e, hw->h, hw->l, hw->ix, hw->iy, hw->sp);
        fprintf(stderr, "  REF: PC=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X\n",
                ref->pc, ref->a, ref->f, ref->b, ref->c, ref->d, ref->e, ref->h, ref->l, ref->ix, ref->iy, ref->sp);
    }

    return match;
}

