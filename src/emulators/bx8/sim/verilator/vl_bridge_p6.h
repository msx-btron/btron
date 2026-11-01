/*
 * vl_bridge_p6.h — Pure C99 ABI Bridge for Phase 6 Full System MSX turbo R SoC.
 */

#ifndef _VL_BRIDGE_P6_H_
#define _VL_BRIDGE_P6_H_

#include <stdint.h>
#include <stdbool.h>
#include "trace_compare.h"
#include "vl_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

void *vl_p6_create(void);
void  vl_p6_destroy(void *handle);
void  vl_p6_reset(void *handle);
void  vl_p6_step(void *handle);

/* Physical Memory Bus Interface */
bool  vl_p6_has_mem_req(void *handle);
bool  vl_p6_get_mem_req(void *handle, vl_mem_req_t *req);
bool  vl_p6_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready);

/* CPU Architectural State Introspection */
void  vl_p6_get_regs(void *handle, trace_regs_t *regs);
bool  vl_p6_inst_retired(void *handle);
bool  vl_p6_is_r800(void *handle);

/* Video Signals */
uint16_t vl_p6_video_rgb565(void *handle);
bool     vl_p6_video_hsync(void *handle);
bool     vl_p6_video_vsync(void *handle);
bool     vl_p6_video_blank(void *handle);
uint16_t vl_p6_video_beam_x(void *handle);
uint8_t  vl_p6_video_beam_y(void *handle);
bool     vl_p6_vdp_irq(void *handle);

/* Audio Signals */
uint16_t vl_p6_audio_pcm_sample(void *handle);
uint16_t vl_p6_psg_sample(void *handle);
uint16_t vl_p6_scc_sample(void *handle);
uint8_t  vl_p6_pcm_sample(void *handle);

/* LED Indicators */
bool vl_p6_led_caps(void *handle);
bool vl_p6_led_kana(void *handle);
bool vl_p6_led_turbo(void *handle);

/* Peripheral Introspection */
uint8_t  vl_p6_ppi_peek_pc(void *handle);
uint8_t  vl_p6_ppi_read_a(void *handle);
uint8_t  vl_p6_psg_peek_reg(void *handle, uint8_t idx);
uint8_t  vl_p6_vdp_peek_reg(void *handle, uint8_t idx);
uint16_t vl_p6_vdp_peek_palette(void *handle, uint8_t idx);
uint16_t vl_p6_vdp_peek_pal256(void *handle, uint8_t idx);
uint32_t vl_p6_psg_peek_lfsr(void *handle);
uint8_t  vl_p6_kbd_peek_row(void *handle, uint8_t row);
uint16_t vl_p6_e6_ticks(void *handle);

/* Keyboard Injection */
void vl_p6_kbd_key_down(void *handle, uint8_t row, uint8_t col);
void vl_p6_kbd_key_up  (void *handle, uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

#endif /* _VL_BRIDGE_P6_H_ */

