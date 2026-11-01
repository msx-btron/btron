/*
 * trace_compare.h — Pure C99 Lock-Step Trace Comparator.
 *
 * Compares architectural CPU registers and memory bus transactions between
 * the BSV Verilated hardware and the in-tree C99 reference emulator.
 */

#ifndef _TRACE_COMPARE_H_
#define _TRACE_COMPARE_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t pc;
    uint8_t  a, f, b, c, d, e, h, l;
    uint16_t ix, iy, sp;
} trace_regs_t;

typedef struct {
    uint64_t total_instructions;
    uint64_t total_divergences;
} trace_stats_t;

void trace_compare_init(trace_stats_t *st);
bool trace_compare_step(trace_stats_t *st, const trace_regs_t *hw, const trace_regs_t *ref, const char *prefix);

#endif /* _TRACE_COMPARE_H_ */

