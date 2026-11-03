#ifndef V9968_H
#define V9968_H

/*
 * V9968 Video Display Processor & System Controller Emulation
 *
 * Based on Takayuki Hara's (HRA!) V9968 MSX-VIDEO specification:
 * https://github.com/hra1129/V9968_Cartridge
 *
 * Implements:
 * - V9968 Port #4 (0x9C primary, 0x8C secondary) extended status/interrupts
 * - Extended Register R#20 (Mode 1: EPAL, FAST, S16, FIL, CEIE, etc.)
 * - Extended Register R#21 (Mode 2: V58 compatibility mode)
 * - Extended Command Registers R#47..R#58 (Affine transformation vectors & window)
 * - LRMM (Logical Rotate Memory to Memory) command execution
 * - System Controller (Ports 0x40..0x4F) enabler & configuration interface
 */

#include <stdint.h>
#include "v9938.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Port #4 bit definitions */
#define V9968_P4_LOCK_EXTREGS    0x80u  /* Bit 7: 1 = locked, 0 = unlocked */
#define V9968_P4_CMD_END_IRQ     0x04u  /* Bit 2: Command end interrupt */
#define V9968_P4_LINE_IRQ        0x02u  /* Bit 1: Line interrupt */
#define V9968_P4_FRAME_IRQ       0x01u  /* Bit 0: Frame interrupt */

/* R#20 Mode Register 1 bit definitions */
#define V9968_R20_HS             0x01u  /* High-speed command execution */
#define V9968_R20_SVNS           0x02u  /* Sprite vertical non-R#23 */
#define V9968_R20_ILNS           0x04u  /* Line IRQ non-R#23 */
#define V9968_R20_SP3            0x08u  /* Sprite Mode 3 */
#define V9968_R20_EPAL           0x10u  /* Extended 256-color palette */
#define V9968_R20_FIL            0x20u  /* Flat interlace mode */
#define V9968_R20_CEIE           0x40u  /* Command end interrupt enable */
#define V9968_R20_S16            0x80u  /* Sprite 16 mode */

/* R#21 Mode Register 2 bit definitions */
#define V9968_R21_V58            0x01u  /* 1 = V9958 compatible mode, 0 = V9968 native mode */

typedef struct v9968_s {
    v9938_t *vdp;

    /* Port #4 State */
    uint8_t lock_extregs;               /* 1 = locked (default), 0 = unlocked */
    uint8_t frame_irq;
    uint8_t line_irq;
    uint8_t cmd_end_irq;

    /* System Controller (Ports 0x40..0x4F) */
    uint8_t syscon_enabled;             /* 1 when Port 0x40 written with 0x80 */
    uint8_t syscon_device;              /* Selected device ID (Port 0x41) */
    uint8_t syscon_func;                /* Selected function ID (Port 0x42) */
    uint8_t syscon_addr[3];             /* Flash / Config ROM address (43h..45h) */
    uint8_t syscon_cmd;                 /* Command (46h) */
    uint8_t syscon_buf[256];            /* 256-byte page buffer (48h) */
    uint8_t syscon_buf_idx;             /* Buffer index (4Ah) */
    uint8_t syscon_len;                 /* Transfer length - 1 (49h) */
    uint8_t syscon_status;              /* Page transfer status (4Eh: 00=ok, 01=exceeded) */
} v9968_t;

/* Global instance */
extern v9968_t g_v9968;

void     v9968_init(v9968_t *v68, v9938_t *vdp);
void     v9968_reset(v9968_t *v68);

/* Port #4 access (0x9C primary, 0x8C secondary) */
uint8_t  v9968_port4_read(v9968_t *v68);
void     v9968_port4_write(v9968_t *v68, uint8_t val);

/* System Controller access (0x40..0x4F) */
uint8_t  v9968_syscon_read(v9968_t *v68, uint8_t port);
void     v9968_syscon_write(v9968_t *v68, uint8_t port, uint8_t val);

/* Extended Command Execution (LRMM) */
void     v9968_exec_lrmm(v9938_t *v);

#ifdef __cplusplus
}
#endif

#endif /* V9968_H */
