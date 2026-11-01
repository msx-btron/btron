/*
 * vl_bridge_p3.h — Pure C99 ABI Bridge for Phase 3 Slots & Mapper Co-Simulation.
 */

#ifndef _VL_BRIDGE_P3_H_
#define _VL_BRIDGE_P3_H_

#include <stdint.h>
#include <stdbool.h>
#include "trace_compare.h"
#include "vl_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

void *vl_p3_create(void);
void  vl_p3_destroy(void *handle);
void  vl_p3_reset(void *handle);
void  vl_p3_step(void *handle);

// Memory bus interaction (between CPU crossbar and C99 memory model)
bool  vl_p3_has_mem_req(void *handle);
bool  vl_p3_get_mem_req(void *handle, vl_mem_req_t *req);
bool  vl_p3_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready);

// CPU Introspection
void  vl_p3_get_regs(void *handle, trace_regs_t *regs);
bool  vl_p3_inst_retired(void *handle);
bool  vl_p3_is_r800(void *handle);
uint8_t vl_p3_slot_ppi_a(void *handle);

#ifdef __cplusplus
}
#endif

#endif /* _VL_BRIDGE_P3_H_ */

