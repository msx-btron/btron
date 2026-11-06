/*
 * include/arch/bcm283x/v3d_regs.h — VideoCore VI (V3D 4.2) Register Map & CLE Constants
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Physical Address (ARM CPU): 0xFEC00000
 * VideoCore Bus Address     : 0x7EC00000
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _ARCH_BCM283X_V3D_REGS_H_
#define _ARCH_BCM283X_V3D_REGS_H_

#include <stdint.h>

#define V3D_PHYS_BASE                   0xFEC00000UL
#define V3D_VC_BUS_BASE                 0x7EC00000UL

/* ── Hardware Identification & Status Registers ───────────────────────── */
#define V3D_IDENT0                      0x00000   /* Technology & version (0x04 for V3D 4.2) */
#define V3D_IDENT1                      0x00004   /* V3D revision, slices, QPUs per slice */
#define V3D_IDENT2                      0x00008   /* V3D specs & features */
#define V3D_IDENT3                      0x0000C   /* V3D specs & features */
#define V3D_SCRATCH0                    0x00010   /* Scratch register 0 */

#define V3D_L2CACTL                     0x00020   /* L2 Cache Control & Invalidate */
#define V3D_SLCACTL                     0x00024   /* System L2 Cache Control */

#define V3D_INTCTL                      0x00030   /* Interrupt Control */
#define V3D_INTSTS                      0x00034   /* Interrupt Status */
#define V3D_INTMSK                      0x00038   /* Interrupt Mask */

/* V3D_INTSTS Bits */
#define V3D_INTSTS_BINNER_DONE          (1u << 0)
#define V3D_INTSTS_RENDER_DONE          (1u << 1)

/* ── Control List Executor (CLE) Registers ────────────────────────────── */
/* Thread 0: Rendering Pipeline */
#define V3D_CLE_CT0CS                   0x00100   /* Control & Status (Thread 0) */
#define V3D_CLE_CT0EA                   0x00104   /* End Address (Writing triggers start) */
#define V3D_CLE_CT0CA                   0x00108   /* Current Address */
#define V3D_CLE_CT0EO                   0x0010C   /* End Address Output */

/* Thread 1: Binning Pipeline */
#define V3D_CLE_CT1CS                   0x00120   /* Control & Status (Thread 1) */
#define V3D_CLE_CT1EA                   0x00124   /* End Address (Writing triggers start) */
#define V3D_CLE_CT1CA                   0x00128   /* Current Address */
#define V3D_CLE_CT1EO                   0x0012C   /* End Address Output */

/* Primitive Tile Binning (PTB) Scratch Allocation */
#define V3D_PTB_BPCA                    0x00130   /* Base Pointer Address for Tile Allocation */
#define V3D_PTB_BPCS                    0x00134   /* Base Pointer Size for Tile Allocation */

/* Control List Status Bits */
#define V3D_CLE_CTnCS_BUSY              (1u << 5)
#define V3D_CLE_CTnCS_PAUSED            (1u << 4)
#define V3D_CLE_CTnCS_RESET             (1u << 15)

/* ── Control List Executor (CLE) Opcodes ──────────────────────────────── */
#define V3D42_HALT                      0x00
#define V3D42_NOP                       0x01
#define V3D42_FLUSH                     0x04
#define V3D42_FLUSH_ALL                 0x05
#define V3D42_START_TILE_BINNING        0x06
#define V3D42_INCREMENT_SEMAPHORE       0x07
#define V3D42_WAIT_SEMAPHORE            0x08
#define V3D42_BRANCH                    0x10
#define V3D42_BRANCH_TO_SUBLIST         0x11
#define V3D42_RETURN_FROM_SUBLIST       0x12
#define V3D42_STORE_MULTI_SAMPLE_END    0x18
#define V3D42_SCOREBOARD_WAIT           0x19
#define V3D42_SCOREBOARD_UNLOCK         0x1A

#define V3D42_TILE_BINNING_MODE_CFG     0x70
#define V3D42_TILE_RENDERING_MODE_CFG   0x71
#define V3D42_CLEAR_COLORS              0x72
#define V3D42_TILE_COORDINATES          0x73
#define V3D42_STORE_TILE_BUFFER_GENERAL 0x74

/* Mailbox Tags for V3D */
#define RPI_MBOX_TAG_GET_CLOCK_RATE     0x00030002
#define RPI_MBOX_TAG_SET_CLOCK_RATE     0x00038002
#define RPI_MBOX_TAG_SET_POWER_STATE    0x00028001
#define RPI_MBOX_CLOCK_V3D              0x00000005
#define RPI_MBOX_DEVICE_V3D             0x00000005

#endif /* _ARCH_BCM283X_V3D_REGS_H_ */

