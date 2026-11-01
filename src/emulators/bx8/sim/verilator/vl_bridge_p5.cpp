/*
 * vl_bridge_p5.cpp — Minimal C++ Bridge Isolating Verilator for Phase 5.
 */

#include "vl_bridge_p5.h"
#include "VmkTopPhase5.h"

extern "C" {

void *vl_p5_create(void) {
    VmkTopPhase5 *top = new VmkTopPhase5();
    top->CLK = 0;
    top->RST_N = 1;
    top->EN_mem_req_deq = 0;
    top->EN_put_mem_resp = 0;
    top->eval();
    return (void *)top;
}

void vl_p5_destroy(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (top) { top->final(); delete top; }
}

void vl_p5_reset(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top) return;
    top->RST_N = 0;
    for (int i = 0; i < 5; i++) {
        top->CLK = 1; top->eval();
        top->CLK = 0; top->eval();
    }
    top->RST_N = 1;
    top->CLK = 0; top->eval();
}

void vl_p5_step(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top) return;
    top->CLK = 1; top->eval();
    top->CLK = 0; top->eval();
    top->EN_mem_req_deq  = 0;
    top->EN_put_mem_resp = 0;
}

bool vl_p5_has_mem_req(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? (bool)top->mem_req_valid : false;
}

bool vl_p5_get_mem_req(void *handle, vl_mem_req_t *req) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
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

bool vl_p5_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top || !top->RDY_put_mem_resp) return false;
    top->put_mem_resp_tag   = tag;
    top->put_mem_resp_data  = data;
    top->put_mem_resp_ready = ready ? 1 : 0;
    top->EN_put_mem_resp    = 1;
    return true;
}

void vl_p5_get_regs(void *handle, trace_regs_t *regs) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
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

bool vl_p5_inst_retired(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? (bool)top->cpu_inst_retired : false;
}

bool vl_p5_is_r800(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? (bool)top->cpu_is_r800 : false;
}

uint16_t vl_p5_psg_sample(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? top->psg_sample : 0;
}

uint16_t vl_p5_scc_sample(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? top->scc_sample : 0;
}

uint8_t vl_p5_pcm_sample(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? top->pcm_sample : 0x80;
}

uint8_t vl_p5_psg_peek_reg(void *handle, uint8_t idx) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top) return 0;
    top->psg_peek_reg_idx = idx & 0x0F;
    top->eval();
    return top->psg_peek_reg;
}

uint32_t vl_p5_psg_peek_lfsr(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? (uint32_t)top->psg_peek_lfsr : 0;
}

uint8_t vl_p5_ppi_peek_pc(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? top->ppi_peek_pc : 0;
}

uint8_t vl_p5_ppi_read_a(void *handle) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    return top ? top->ppi_read_a : 0;
}

uint8_t vl_p5_kbd_peek_row(void *handle, uint8_t row) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top) return 0xFF;
    top->kbd_peek_row_row = row & 0x0F;
    top->eval();
    return top->kbd_peek_row;
}

void vl_p5_kbd_key_down(void *handle, uint8_t row, uint8_t col) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top || !top->RDY_kbd_key_down) return;
    top->kbd_key_down_row = row & 0x0F;
    top->kbd_key_down_col = col & 0x07;
    top->EN_kbd_key_down = 1;
    top->CLK = 1; top->eval();
    top->CLK = 0; top->eval();
    top->EN_kbd_key_down = 0;
}

void vl_p5_kbd_key_up(void *handle, uint8_t row, uint8_t col) {
    VmkTopPhase5 *top = (VmkTopPhase5 *)handle;
    if (!top || !top->RDY_kbd_key_up) return;
    top->kbd_key_up_row = row & 0x0F;
    top->kbd_key_up_col = col & 0x07;
    top->EN_kbd_key_up = 1;
    top->CLK = 1; top->eval();
    top->CLK = 0; top->eval();
    top->EN_kbd_key_up = 0;
}

/* Unified memory model callbacks */
bool vl_has_mem_req(void *h) { return vl_p5_has_mem_req(h); }
bool vl_get_mem_req(void *h, vl_mem_req_t *r) { return vl_p5_get_mem_req(h, r); }
bool vl_put_mem_resp(void *h, uint8_t t, uint8_t d, bool r) { return vl_p5_put_mem_resp(h, t, d, r); }

} // extern "C"

