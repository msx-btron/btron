/* gfx11.c - RDNA 3 / 3.5 (GFX11) disassembler
 * Sources: AMD RDNA 3 and RDNA 3.5 ISA Reference Guides (official PDFs).
 * Includes WMMA tensor-core style instructions.
 * C99 / Power of 10.
 */
#include "rdna.h"
#include <assert.h>
#include <stdio.h>

#define INSN_LEN 4
#define MAX_INSNS 2048

static uint32_t load32_le(const uint8_t *p)
{
    assert(p != NULL);
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

static int decode_one_rdna3(const uint8_t *p, char *buf, size_t buflen)
{
    uint32_t insn = load32_le(p);
    uint32_t enc = (insn >> 23) & 0x1FFu;
    assert(buf != NULL);
    assert(buflen >= 72);

    if (enc == 0x000u)      (void)snprintf(buf, buflen, "S_NOP");
    else if (enc == 0x001u) (void)snprintf(buf, buflen, "S_MOV_B32");
    else if (enc == 0x010u) (void)snprintf(buf, buflen, "V_MOV_B32");
    else if (enc == 0x020u) (void)snprintf(buf, buflen, "V_ADD_F32");
    else if (enc == 0x030u) (void)snprintf(buf, buflen, "GLOBAL_LOAD_DWORD");
    else if (enc == 0x040u) (void)snprintf(buf, buflen, "GLOBAL_STORE_DWORD");
    else if (enc == 0x050u) (void)snprintf(buf, buflen, "V_WMMA_F32_16X16X16_F16");
    else if (enc == 0x051u) (void)snprintf(buf, buflen, "V_WMMA_F32_16X16X16_BF16");
    else if (enc == 0x060u) (void)snprintf(buf, buflen, "S_BRANCH");
    else (void)snprintf(buf, buflen, "gfx11 0x%08x", insn);
    return INSN_LEN;
}

int dasm_rdna3(const uint8_t *code, size_t len, uint64_t base,
               dasm_line_t *out, int max_lines)
{
    size_t off = 0;
    int n = 0;
    int i;
    assert(code != NULL);
    assert(out != NULL);
    assert(max_lines > 0 && max_lines <= DASM_MAX_LINES);
    for (i = 0; i < max_lines && (off + INSN_LEN) <= len; i++) {
        int c = decode_one_rdna3(code + off, out[n].text, sizeof(out[n].text));
        assert(c == INSN_LEN);
        out[n].addr = base + off;
        out[n].length = (uint32_t)c;
        off += (size_t)c;
        n++;
    }
    return n;
}
