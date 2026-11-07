/*
 * include/arch/bcm283x/v3d_qpu.h — VideoCore VI (V3D 4.2) QPU Shader Engine
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Implements pre-assembled QPU microcode for:
 * 1. Flat & Gouraud Shaded Color Triangles (v3d_shader_basic_color)
 * 2. Modulated 2D Texture Sampler with TMU0 (v3d_shader_texture_modulate)
 * 3. Directional Lighting & Normal Transform (v3d_shader_lighting)
 *
 * Conforms to NASA JPL Rule 1 (bounded loops) and Rule 3 (no dynamic allocations).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _ARCH_BCM283X_V3D_QPU_H_
#define _ARCH_BCM283X_V3D_QPU_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    V3D_SHADER_BASIC_COLOR       = 0,
    V3D_SHADER_TEXTURE_MODULATE  = 1,
    V3D_SHADER_LIGHTING          = 2,
    V3D_SHADER_COUNT             = 3
} v3d_shader_type_t;

typedef struct {
    const uint32_t *bytecode;
    size_t          size_bytes;
    uint32_t        num_varyings;
    uint32_t        flags;
} v3d_shader_program_t;

/* Initialize pre-assembled QPU microcode shader programs in .nocache memory */
int v3d_qpu_init(void);

/* Query whether QPU shader engine is active */
bool v3d_qpu_is_ready(void);

/* Get physical address of compiled shader bytecode in .nocache RAM */
uint32_t v3d_qpu_get_shader_phys(v3d_shader_type_t type);

/* Get shader program descriptor */
const v3d_shader_program_t *v3d_qpu_get_program(v3d_shader_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_BCM283X_V3D_QPU_H_ */

