/*
 * v9968.c — V9968 Video Display Processor & System Controller
 *
 * Implements hardware extensions from Takayuki Hara's (HRA!) V9968 project:
 * https://github.com/hra1129/V9968_Cartridge
 *
 * - Port #4 (0x9C primary / 0x8C secondary): Lock/Unlock, Interrupts & Status
 * - System Controller (Ports 0x40..0x4F): Enabler, Device/Function select,
 *   Config ROM / Flash page buffers
 * - Extended Command Engine: Opcode 0x30 (LRMM - Logical Rotate Memory-to-Memory)
 *   with affine transformation, scaling, and window clipping
 */

#include "v9968.h"
#include "v9938.h"
#include <string.h>

v9968_t g_v9968;

void v9968_init(v9968_t *v68, v9938_t *vdp)
{
    if (!v68) return;
    memset(v68, 0, sizeof(*v68));
    v68->vdp = vdp;
    v68->lock_extregs = 1; /* Reset locked by default */
}

void v9968_reset(v9968_t *v68)
{
    if (!v68) return;
    v9938_t *vdp_save = v68->vdp;
    memset(v68, 0, sizeof(*v68));
    v68->vdp = vdp_save;
    v68->lock_extregs = 1;
    if (v68->vdp) {
        v68->vdp->regs[55] = 0xFF; /* WEX lo: 511 */
        v68->vdp->regs[56] = 0x01; /* WEX hi */
        v68->vdp->regs[57] = 0xFF; /* WEY lo: 2047 */
        v68->vdp->regs[58] = 0x07; /* WEY hi */
    }
}

/* ---- Port #4 (0x9C primary, 0x8C secondary) ----------------------------- */

uint8_t v9968_port4_read(v9968_t *v68)
{
    if (!v68) return 0xFFu;
    /* Bit 7: lock_extregs, Bits 6..3: 0, Bit 2: CMD_END_IRQ, Bit 1: LINE_IRQ, Bit 0: FRAME_IRQ */
    uint8_t val = (uint8_t)((v68->lock_extregs ? V9968_P4_LOCK_EXTREGS : 0u) |
                            (v68->cmd_end_irq ? V9968_P4_CMD_END_IRQ : 0u) |
                            (v68->line_irq ? V9968_P4_LINE_IRQ : 0u) |
                            (v68->frame_irq ? V9968_P4_FRAME_IRQ : 0u));
    return val;
}

void v9968_port4_write(v9968_t *v68, uint8_t val)
{
    if (!v68) return;
    /* Bit 7 sets lock_extregs (1 = lock, 0 = unlock) */
    v68->lock_extregs = (uint8_t)((val & 0x80u) ? 1 : 0);

    /* Bits 0..2 clear interrupt flags when 1 is written */
    if (val & V9968_P4_FRAME_IRQ)   v68->frame_irq = 0;
    if (val & V9968_P4_LINE_IRQ)    v68->line_irq = 0;
    if (val & V9968_P4_CMD_END_IRQ) v68->cmd_end_irq = 0;
}

/* ---- System Controller (Ports 0x40..0x4F) -------------------------------- */

uint8_t v9968_syscon_read(v9968_t *v68, uint8_t port)
{
    if (!v68 || !v68->syscon_enabled) return 0xFFu;

    uint8_t reg = (uint8_t)(port & 0x0Fu);
    switch (reg) {
    case 0x00: /* Port 0x40: Enabler read returns inverted 0x80 = 0x7F */
        return 0x7Fu;

    case 0x01: /* Port 0x41: Returns inverted device ID (0x01 -> 0xFE) */
        return (v68->syscon_device == 0x01u) ? 0xFEu : 0xFFu;

    case 0x02: /* Port 0x42: Function Select (0x01 -> 0xFE) */
        if (v68->syscon_device == 0x01u) {
            return (v68->syscon_func == 0x01u) ? 0xFEu : 0xFFu;
        }
        return 0xFFu;

    case 0x47 - 0x40: /* Port 0x47: Data */
        return 0x00u; /* Flash idle / SR1 */

    case 0x48 - 0x40: /* Port 0x48: Page buffer read with auto-increment */
        if (v68->syscon_device == 0x01u && v68->syscon_func == 0x01u) {
            uint8_t data = v68->syscon_buf[v68->syscon_buf_idx++];
            return data;
        }
        return 0xFFu;

    case 0x49 - 0x40: /* Port 0x49: Transfer length - 1 */
        return v68->syscon_len;

    case 0x4A - 0x40: /* Port 0x4A: Buffer index */
        return v68->syscon_buf_idx;

    case 0x4E - 0x40: /* Port 0x4E: Page command result (00 = success, 01 = boundary exceeded) */
        return v68->syscon_status;

    case 0x4F - 0x40: /* Port 0x4F: Page transfer identifier (A1h) */
        if (v68->syscon_device == 0x01u && v68->syscon_func == 0x01u) {
            return 0xA1u;
        }
        return 0xFFu;

    default:
        return 0xFFu;
    }
}

void v9968_syscon_write(v9968_t *v68, uint8_t port, uint8_t val)
{
    if (!v68) return;

    uint8_t reg = (uint8_t)(port & 0x0Fu);
    if (reg == 0x00) { /* Port 0x40: Enabler */
        if (val == 0x80u) {
            v68->syscon_enabled = 1;
        } else {
            v68->syscon_enabled = 0;
        }
        return;
    }

    if (!v68->syscon_enabled) return;

    switch (reg) {
    case 0x01: /* Port 0x41: Device Select */
        v68->syscon_device = val;
        break;

    case 0x02: /* Port 0x42: Function Select */
        v68->syscon_func = val;
        break;

    case 0x43 - 0x40: /* Port 0x43: Address Low */
        v68->syscon_addr[0] = val;
        break;
    case 0x44 - 0x40: /* Port 0x44: Address Mid */
        v68->syscon_addr[1] = val;
        break;
    case 0x45 - 0x40: /* Port 0x45: Address High */
        v68->syscon_addr[2] = val;
        break;

    case 0x46 - 0x40: /* Port 0x46: Command */
        v68->syscon_cmd = val;
        v68->syscon_status = 0x00u;
        break;

    case 0x48 - 0x40: /* Port 0x48: Page buffer write with auto-increment */
        if (v68->syscon_device == 0x01u && v68->syscon_func == 0x01u) {
            v68->syscon_buf[v68->syscon_buf_idx++] = val;
        }
        break;

    case 0x49 - 0x40: /* Port 0x49: Transfer length - 1 */
        v68->syscon_len = val;
        break;

    case 0x4A - 0x40: /* Port 0x4A: Buffer index */
        v68->syscon_buf_idx = val;
        break;

    default:
        break;
    }
}

/* ---- Extended Command Execution (LRMM) ---------------------------------- */

void v9968_exec_lrmm(v9938_t *v)
{
    if (!v) return;

    uint8_t mode = v9938_mode(v);
    uint8_t op = (uint8_t)(v->cmd & 0x0Fu);

    int dix = (v->arg & 0x04u) ? -1 : 1;
    int diy = (v->arg & 0x08u) ? -1 : 1;
    int xhr = (v->arg & 0x40u) != 0;

    int nx = v->nx ? (int)v->nx : 256;
    int ny = v->ny ? (int)v->ny : 1024;

    /* Affine step vectors from R#47..R#50 (signed 8.8 fixed point) */
    int16_t vx = (int16_t)((uint16_t)v->regs[47] | ((uint16_t)v->regs[48] << 8));
    int16_t vy = (int16_t)((uint16_t)v->regs[49] | ((uint16_t)v->regs[50] << 8));

    /* Window boundary coordinates from R#51..R#58 */
    int16_t wsx = (int16_t)((uint16_t)v->regs[51] | ((uint16_t)(v->regs[52] & 0x01u) << 8));
    int16_t wsy = (int16_t)((uint16_t)v->regs[53] | ((uint16_t)(v->regs[54] & 0x07u) << 8));
    int16_t wex = (int16_t)((uint16_t)v->regs[55] | ((uint16_t)(v->regs[56] & 0x01u) << 8));
    int16_t wey = (int16_t)((uint16_t)v->regs[57] | ((uint16_t)(v->regs[58] & 0x07u) << 8));

    int32_t sx_val = (int32_t)(int16_t)((v->sx & 0x0800u) ? (v->sx | 0xF000u) : (v->sx & 0x0FFFu));
    int32_t sy_val = (int32_t)(int16_t)((v->sy & 0x1000u) ? (v->sy | 0xE000u) : (v->sy & 0x1FFFu));

    int32_t row_sx = sx_val << 8;
    int32_t row_sy = sy_val << (xhr ? 9 : 8);

    for (int yi = 0; yi < ny; yi++) {
        int dy = (int)v->dy + yi * diy;
        int32_t cur_sx = row_sx;
        int32_t cur_sy = row_sy;

        for (int xi = 0; xi < nx; xi++) {
            int dx = (int)v->dx + xi * dix;

            int sx_int = (int)(cur_sx / 256);
            int sy_int = (int)(cur_sy / (xhr ? 512 : 256));

            uint8_t c;
            /* Window clipping check */
            if (sx_int < wsx || sx_int > wex || sy_int < wsy || sy_int > wey) {
                c = v->col; /* Outside window uses foreground/fill color */
            } else {
                c = v9938_point_pixel(v, sx_int, sy_int, mode);
            }

            v9938_pset_pixel(v, dx, dy, c, op, mode);

            cur_sx += vx;
            cur_sy += vy;
        }

        /* End of line: advance row starting position */
        if (xhr) {
            row_sx -= ((int32_t)vy * 2);
            row_sy += ((int32_t)vx * 2);
        } else {
            row_sx -= vy;
            row_sy += vx;
        }
    }
}
