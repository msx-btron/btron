/* sm86.c - Ampere SASS (sm_86) disassembler
 * Sources: CUDA Binary Utilities (Ampere), DocumentSASS, community SASS dumps.
 * Fixed 16-byte instructions. C99 / Power of 10.
 */
#include "smcuda.h"
#include <assert.h>
#include <stdio.h>

#define INSN_LEN 16
#define MAX_INSNS 512

static uint64_t load64_le(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    assert(p != NULL);
    for (i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}

static int decode_one_sm86(const uint8_t *p, char *buf, size_t buflen)
{
    uint64_t hi = load64_le(p + 8);
    uint32_t op = (uint32_t)(hi & 0xFFu);
    assert(p != NULL);
    assert(buf != NULL);
    assert(buflen >= 64);

    if (op == 0x00u)      (void)snprintf(buf, buflen, "NOP");
    else if (op == 0x10u) (void)snprintf(buf, buflen, "MOV");
    else if (op == 0x20u) (void)snprintf(buf, buflen, "IADD3");
    else if (op == 0x21u) (void)snprintf(buf, buflen, "IMAD");
    else if (op == 0x30u) (void)snprintf(buf, buflen, "LDG");
    else if (op == 0x40u) (void)snprintf(buf, buflen, "STG");
    else if (op == 0x50u) (void)snprintf(buf, buflen, "HMMA");
    else if (op == 0x51u) (void)snprintf(buf, buflen, "IMMA");
    else if (op == 0x52u) (void)snprintf(buf, buflen, "DMMA");
    else if (op == 0x60u) (void)snprintf(buf, buflen, "FFMA");
    else if (op == 0x70u) (void)snprintf(buf, buflen, "LDGSTS");
    else if (op == 0x80u) (void)snprintf(buf, buflen, "BRA");
    else (void)snprintf(buf, buflen, "sm86 0x%016llx", (unsigned long long)hi);
    return INSN_LEN;
}

int dasm_sm86(const uint8_t *code, size_t len, uint64_t base,
              dasm_line_t *out, int max_lines)
{
    size_t off = 0;
    int n = 0;
    int i;
    assert(code != NULL);
    assert(out != NULL);
    assert(max_lines > 0 && max_lines <= DASM_MAX_LINES);
    for (i = 0; i < max_lines && (off + INSN_LEN) <= len; i++) {
        int c = decode_one_sm86(code + off, out[n].text, sizeof(out[n].text));
        assert(c == INSN_LEN);
        out[n].addr = base + off;
        out[n].length = (uint32_t)c;
        off += (size_t)c;
        n++;
    }
    return n;
}
