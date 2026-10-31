/*
 * vl_bridge_p4.h — Pure C99 ABI Bridge for Phase 4 V9958 VDP Co-Simulation.
 */

#ifndef _VL_BRIDGE_P4_H_
#define _VL_BRIDGE_P4_H_

#include <stdint.h>
#include <stdbool.h>
#include "trace_compare.h"
#include "vl_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

void *vl_p4_create(void);
void  vl_p4_destroy(void *handle);
void  vl_p4_reset(void *handle);
void  vl_p4_step(void *handle);

// Memory bus interaction
bool  vl_p4_has_mem_req(void *handle);
bool  vl_p4_get_mem_req(void *handle, vl_mem_req_t *req);
bool  vl_p4_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready);

// CPU Introspection
void  vl_p4_get_regs(void *handle, trace_regs_t *regs);
bool  vl_p4_inst_retired(void *handle);
bool  vl_p4_is_r800(void *handle);

// VDP Video Signals
bool     vl_p4_vdp_irq(void *handle);
uint16_t vl_p4_vdp_beam_x(void *handle);
uint8_t  vl_p4_vdp_beam_y(void *handle);
uint16_t vl_p4_vdp_line(void *handle);
bool     vl_p4_vdp_hsync(void *handle);
bool     vl_p4_vdp_vsync(void *handle);
bool     vl_p4_vdp_blank(void *handle);
uint16_t vl_p4_vdp_pixel(void *handle);

// VDP Introspection
uint8_t  vl_p4_vdp_peek_reg(void *handle, uint8_t idx);
uint8_t  vl_p4_vdp_peek_stat(void *handle, uint8_t idx);
uint16_t vl_p4_vdp_peek_palette(void *handle, uint8_t idx);
uint32_t vl_p4_vdp_peek_vram_ptr(void *handle);
uint8_t  vl_p4_vdp_peek_read_ahead(void *handle);

#ifdef __cplusplus
}
#endif

#endif /* _VL_BRIDGE_P4_H_ */

