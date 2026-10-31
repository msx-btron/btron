/*
 * vl_bridge_p2.h — Pure C99 ABI Bridge for Phase 2 CPU Core Co-Simulation.
 */

#ifndef _VL_BRIDGE_P2_H_
#define _VL_BRIDGE_P2_H_

#include <stdint.h>
#include <stdbool.h>
#include "trace_compare.h"
#include "vl_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

void *vl_p2_create(void);
void  vl_p2_destroy(void *handle);
void  vl_p2_reset(void *handle);
void  vl_p2_step(void *handle);

// Memory bus interaction (between CPU arbiter and C99 memory model)
bool  vl_p2_has_mem_req(void *handle);
bool  vl_p2_get_mem_req(void *handle, vl_mem_req_t *req);
bool  vl_p2_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready);

// CPU Introspection
void  vl_p2_get_regs(void *handle, trace_regs_t *regs);
bool  vl_p2_inst_retired(void *handle);
bool  vl_p2_is_r800(void *handle);
uint8_t vl_p2_get_state(void *handle);
uint8_t vl_p2_get_opcode(void *handle);

#ifdef __cplusplus
}
#endif

#endif /* _VL_BRIDGE_P2_H_ */
