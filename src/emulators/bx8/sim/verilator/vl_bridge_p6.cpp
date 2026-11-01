/*
 * vl_bridge_p6.cpp — Minimal C++ Bridge Isolating Verilator for Phase 6.
 */

#include "vl_bridge_p6.h"
#include "VmkTopMSXTurboR.h"

extern "C" {

void *vl_p6_create(void) {
    VmkTopMSXTurboR *top = new VmkTopMSXTurboR();
    top->CLK = 0;
    top->RST_N = 1;
    top->EN_mem_req_deq = 0;
    top->EN_put_mem_resp = 0;
    top->eval();
    return (void *)top;
}

void vl_p6_destroy(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (top) { top->final(); delete top; }
}

void vl_p6_reset(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top) return;
    top->RST_N = 0;
    for (int i = 0; i < 5; i++) {
        top->CLK = 1; top->eval();
        top->CLK = 0; top->eval();
    }
    top->RST_N = 1;
    top->CLK = 0; top->eval();
}

void vl_p6_step(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top) return;
    top->CLK = 1; top->eval();
    top->CLK = 0; top->eval();
    top->EN_mem_req_deq  = 0;
    top->EN_put_mem_resp = 0;
}

bool vl_p6_has_mem_req(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->mem_req_valid : false;
}

bool vl_p6_get_mem_req(void *handle, vl_mem_req_t *req) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top || !top->mem_req_valid || !top->RDY_mem_req_deq) return false;
    if (req) {
        req->addr  = top->mem_req_addr;
        req->data  = top->mem_req_data;
        req->write = (bool)top->mem_req_write;
        req->is_io = (bool)top->mem_req_is_io;
        req->tag   = top->mem_req_tag;
    }
    top->EN_mem_req_deq = 1;
    return true;
}

bool vl_p6_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top || !top->RDY_put_mem_resp) return false;
    top->put_mem_resp_tag   = tag;
    top->put_mem_resp_data  = data;
    top->put_mem_resp_ready = ready ? 1 : 0;
    top->EN_put_mem_resp    = 1;
    return true;
}

void vl_p6_get_regs(void *handle, trace_regs_t *regs) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top || !regs) return;
    regs->pc = top->cpu_pc;
    regs->a  = top->cpu_a;
    regs->f  = top->cpu_f;
    regs->b  = top->cpu_b;
    regs->c  = top->cpu_c;
    regs->d  = top->cpu_d;
    regs->e  = top->cpu_e;
    regs->h  = top->cpu_h;
    regs->l  = top->cpu_l;
    regs->ix = top->cpu_ix;
    regs->iy = top->cpu_iy;
    regs->sp = top->cpu_sp;
}

bool vl_p6_inst_retired(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->cpu_inst_retired : false;
}

bool vl_p6_is_r800(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->cpu_is_r800 : false;
}

uint16_t vl_p6_video_rgb565(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->video_rgb565 : 0;
}

bool vl_p6_video_hsync(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->video_hsync : false;
}

bool vl_p6_video_vsync(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->video_vsync : false;
}

bool vl_p6_video_blank(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->video_blank : false;
}

uint16_t vl_p6_video_beam_x(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->video_beam_x : 0;
}

uint8_t vl_p6_video_beam_y(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->video_beam_y : 0;
}

bool vl_p6_vdp_irq(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->vdp_irq : false;
}

uint16_t vl_p6_audio_pcm_sample(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->audio_pcm_sample : 0;
}

uint16_t vl_p6_psg_sample(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->psg_raw_sample : 0;
}

uint16_t vl_p6_scc_sample(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->scc_raw_sample : 0;
}

uint8_t vl_p6_pcm_sample(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->pcm_raw_sample : 0x80;
}

bool vl_p6_led_caps(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->led_caps : false;
}

bool vl_p6_led_kana(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->led_kana : false;
}

bool vl_p6_led_turbo(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (bool)top->led_turbo : false;
}

uint8_t vl_p6_ppi_peek_pc(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->ppi_peek_pc : 0;
}

uint8_t vl_p6_ppi_read_a(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? top->ppi_read_a : 0;
}

uint8_t vl_p6_psg_peek_reg(void *handle, uint8_t idx) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top) return 0;
    top->psg_peek_reg_idx = idx & 0x0F;
    top->eval();
    return top->psg_peek_reg;
}

uint8_t vl_p6_vdp_peek_reg(void *handle, uint8_t idx) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top) return 0xFF;
    top->vdp_peek_reg_idx = idx & 0x3F;
    top->eval();
    return top->vdp_peek_reg;
}

uint16_t vl_p6_vdp_peek_pal256(void *handle, uint8_t idx) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top) return 0;
    top->vdp_peek_pal256_idx = idx;
    top->eval();
    return top->vdp_peek_pal256;
}

uint32_t vl_p6_psg_peek_lfsr(void *handle) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    return top ? (uint32_t)top->psg_peek_lfsr : 0;
}

uint8_t vl_p6_kbd_peek_row(void *handle, uint8_t row) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top) return 0xFF;
    top->kbd_peek_row_row = row & 0x0F;
    top->eval();
    return top->kbd_peek_row;
}

void vl_p6_kbd_key_down(void *handle, uint8_t row, uint8_t col) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top || !top->RDY_kbd_key_down) return;
    top->kbd_key_down_row = row & 0x0F;
    top->kbd_key_down_col = col & 0x07;
    top->EN_kbd_key_down = 1;
    top->CLK = 1; top->eval();
    top->CLK = 0; top->eval();
    top->EN_kbd_key_down = 0;
}

void vl_p6_kbd_key_up(void *handle, uint8_t row, uint8_t col) {
    VmkTopMSXTurboR *top = (VmkTopMSXTurboR *)handle;
    if (!top || !top->RDY_kbd_key_up) return;
    top->kbd_key_up_row = row & 0x0F;
    top->kbd_key_up_col = col & 0x07;
    top->EN_kbd_key_up = 1;
    top->CLK = 1; top->eval();
    top->CLK = 0; top->eval();
    top->EN_kbd_key_up = 0;
}

/* Unified memory model callbacks */
bool vl_has_mem_req(void *h) { return vl_p6_has_mem_req(h); }
bool vl_get_mem_req(void *h, vl_mem_req_t *r) { return vl_p6_get_mem_req(h, r); }
bool vl_put_mem_resp(void *h, uint8_t t, uint8_t d, bool r) { return vl_p6_put_mem_resp(h, t, d, r); }

} // extern "C"

