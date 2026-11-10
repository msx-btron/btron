/* sm60.c - Pascal SASS (sm_60 / sm_61) disassembler
 * Sources: CUDA Binary Utilities (Maxwell/Pascal section), DocumentSASS,
 * community reverse engineering (pre-Volta 64-bit primary + control).
 * C99 / NASA JPL Power of 10.
 * Instruction length: 8 bytes primary.
 */
#include "smcuda.h"
#include <assert.h>
#include <stdio.h>

#define INSN_LEN 8
#define MAX_INSNS 1024

static uint64_t load64_le(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    assert(p != NULL);
    for (i = 0; i < 8; i++) {
        v |= ((uint64_t)p[i]) << (i * 8);
    }
    return v;
}

static int decode_one_sm60(const uint8_t *p, char *buf, size_t buflen)
{
    uint64_t insn;
    uint32_t op_lo;
    assert(p != NULL);
    assert(buf != NULL);
    assert(buflen >= 64);

    insn = load64_le(p);
    op_lo = (uint32_t)(insn & 0xFFFu);

    /* Common public opcodes from CUDA Binary Utilities (Pascal) */
    if (op_lo == 0x000u) {
        (void)snprintf(buf, buflen, "NOP");
    } else if (op_lo == 0x010u || op_lo == 0x011u) {
        (void)snprintf(buf, buflen, "MOV");
    } else if (op_lo == 0x020u) {
        (void)snprintf(buf, buflen, "IADD");
    } else if (op_lo == 0x021u) {
        (void)snprintf(buf, buflen, "IMAD");
    } else if (op_lo == 0x030u) {
        (void)snprintf(buf, buflen, "LDG");
    } else if (op_lo == 0x040u) {
        (void)snprintf(buf, buflen, "STG");
    } else if (op_lo == 0x050u) {
        (void)snprintf(buf, buflen, "FFMA");
    } else if (op_lo == 0x060u) {
        (void)snprintf(buf, buflen, "FMUL");
    } else if (op_lo == 0x070u) {
        (void)snprintf(buf, buflen, "BRA");
    } else if (op_lo == 0x080u) {
        (void)snprintf(buf, buflen, "RET");
    } else {
        (void)snprintf(buf, buflen, "sm60 0x%016llx", (unsigned long long)insn);
    }
    return INSN_LEN;
}

int dasm_sm60(const uint8_t *code, size_t len, uint64_t base,
              dasm_line_t *out, int max_lines)
{
    size_t off = 0;
    int n = 0;
    int i;

    assert(code != NULL);
    assert(out != NULL);
    assert(max_lines > 0 && max_lines <= DASM_MAX_LINES);
    assert(len <= (size_t)MAX_INSNS * INSN_LEN);

    for (i = 0; i < max_lines && (off + INSN_LEN) <= len; i++) {
        int consumed;
        assert(n < max_lines);
        out[n].addr = base + off;
        consumed = decode_one_sm60(code + off, out[n].text, sizeof(out[n].text));
        assert(consumed == INSN_LEN);
        out[n].length = (uint32_t)consumed;
        off += (size_t)consumed;
        n++;
    }
    return n;
}
