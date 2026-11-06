/*
 * include/arch/bcm283x/v3d.h — VideoCore VI (V3D 4.2) Driver Interface
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _ARCH_BCM283X_V3D_H_
#define _ARCH_BCM283X_V3D_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ident0;
    uint32_t ident1;
    uint32_t ident2;
    uint32_t ident3;
    uint32_t clock_hz;
    uint32_t qpu_count;
    bool     initialized;
    bool     hardware_present;
} v3d_info_t;

/* Initialize VideoCore VI V3D 4.2 engine (Mailbox power/clock + register probe) */
int v3d_init(void);

/* Query whether hardware V3D acceleration is active */
bool v3d_is_available(void);

/* Get V3D hardware telemetry and state */
const v3d_info_t *v3d_get_info(void);

/* Clear hardware frame with ARGB color */
int v3d_clear(uint32_t color, uint32_t width, uint32_t height);

/* Emit a test triangle using V3D 4.2 CLE (Tile Binning + Render) */
int v3d_test_triangle(uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_BCM283X_V3D_H_ */

