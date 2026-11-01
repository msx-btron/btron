/*
 * vl_bridge_p5.h — Pure C99 ABI Bridge for Phase 5 Audio & I/O Co-Simulation.
 */

#ifndef _VL_BRIDGE_P5_H_
#define _VL_BRIDGE_P5_H_

#include <stdint.h>
#include <stdbool.h>
#include "trace_compare.h"
#include "vl_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

void *vl_p5_create(void);
void  vl_p5_destroy(void *handle);
void  vl_p5_reset(void *handle);
void  vl_p5_step(void *handle);

/* Memory bus */
bool  vl_p5_has_mem_req(void *handle);
bool  vl_p5_get_mem_req(void *handle, vl_mem_req_t *req);
bool  vl_p5_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready);

/* CPU introspection */
void  vl_p5_get_regs(void *handle, trace_regs_t *regs);
bool  vl_p5_inst_retired(void *handle);
bool  vl_p5_is_r800(void *handle);

/* Audio introspection */
uint16_t vl_p5_psg_sample(void *handle);
uint16_t vl_p5_scc_sample(void *handle);
uint8_t  vl_p5_pcm_sample(void *handle);
uint8_t  vl_p5_psg_peek_reg(void *handle, uint8_t idx);
uint32_t vl_p5_psg_peek_lfsr(void *handle);

/* PPI / Keyboard introspection */
uint8_t  vl_p5_ppi_peek_pc(void *handle);
uint8_t  vl_p5_ppi_read_a(void *handle);
uint8_t  vl_p5_kbd_peek_row(void *handle, uint8_t row);

/* Keyboard injection */
void vl_p5_kbd_key_down(void *handle, uint8_t row, uint8_t col);
void vl_p5_kbd_key_up  (void *handle, uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

#endif /* _VL_BRIDGE_P5_H_ */

