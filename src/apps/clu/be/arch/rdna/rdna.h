#ifndef GPU_DASM_H
#define GPU_DASM_H

#include <stdint.h>
#include <stddef.h>

#define DASM_MAX_LINE   192
#define DASM_MAX_LINES  512

typedef struct {
    uint64_t addr;
    uint32_t length;
    char     text[DASM_MAX_LINE];
} dasm_line_t;

#endif
