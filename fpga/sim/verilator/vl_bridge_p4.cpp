/*
 * vl_bridge_p4.cpp — Minimal C++ Bridge Isolating Verilator for Phase 4.
 */

#include "vl_bridge_p4.h"
#include "VmkTopPhase4.h"

extern "C" {

void *vl_p4_create(void) {
    VmkTopPhase4 *top = new VmkTopPhase4();
    top->CLK = 0;
    top->RST_N = 1;
    top->EN_mem_req_deq = 0;
    top->EN_put_mem_resp = 0;
    top->eval();
    return (void *)top;
}

void vl_p4_destroy(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (top) {
        top->final();
        delete top;
    }
}

void vl_p4_reset(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (!top) return;
    top->RST_N = 0;
    for (int i = 0; i < 5; i++) {
        top->CLK = 1; top->eval();
        top->CLK = 0; top->eval();
    }
    top->RST_N = 1;
    top->CLK = 0; top->eval();
}

void vl_p4_step(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (!top) return;
    top->CLK = 1;
    top->eval();
    top->CLK = 0;
    top->eval();
    top->EN_mem_req_deq  = 0;
    top->EN_put_mem_resp = 0;
}

bool vl_p4_has_mem_req(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->mem_req_valid : false;
}

bool vl_p4_get_mem_req(void *handle, vl_mem_req_t *req) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
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

bool vl_p4_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (!top || !top->RDY_put_mem_resp) return false;
    top->put_mem_resp_tag   = tag;
    top->put_mem_resp_data  = data;
    top->put_mem_resp_ready = ready ? 1 : 0;
    top->EN_put_mem_resp    = 1;
    return true;
}

void vl_p4_get_regs(void *handle, trace_regs_t *regs) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
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

bool vl_p4_inst_retired(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->cpu_inst_retired : false;
}

bool vl_p4_is_r800(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->cpu_is_r800 : false;
}

// VDP Video Signals
bool vl_p4_vdp_irq(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->vdp_irq : false;
}

uint16_t vl_p4_vdp_beam_x(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? top->vdp_beam_x : 0;
}

uint8_t vl_p4_vdp_beam_y(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? top->vdp_beam_y : 0;
}

uint16_t vl_p4_vdp_line(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? top->vdp_line : 0;
}

bool vl_p4_vdp_hsync(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->vdp_hsync : false;
}

bool vl_p4_vdp_vsync(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->vdp_vsync : false;
}

bool vl_p4_vdp_blank(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? (bool)top->vdp_blank : false;
}

uint16_t vl_p4_vdp_pixel(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? top->vdp_pixel : 0;
}

// VDP Introspection
uint8_t vl_p4_vdp_peek_reg(void *handle, uint8_t idx) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (!top) return 0xFF;
    top->vdp_peek_reg_idx = idx & 0x3F;
    top->eval();
    return top->vdp_peek_reg;
}

uint8_t vl_p4_vdp_peek_stat(void *handle, uint8_t idx) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (!top) return 0xFF;
    top->vdp_peek_stat_idx = idx & 0x0F;
    top->eval();
    return top->vdp_peek_stat;
}

uint16_t vl_p4_vdp_peek_palette(void *handle, uint8_t idx) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    if (!top) return 0;
    top->vdp_peek_palette_idx = idx & 0x0F;
    top->eval();
    return top->vdp_peek_palette;
}

uint32_t vl_p4_vdp_peek_vram_ptr(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? top->vdp_peek_vram_ptr : 0;
}

uint8_t vl_p4_vdp_peek_read_ahead(void *handle) {
    VmkTopPhase4 *top = (VmkTopPhase4 *)handle;
    return top ? top->vdp_peek_read_ahead : 0xFF;
}

// Unified memory model callbacks
bool vl_has_mem_req(void *h) { return vl_p4_has_mem_req(h); }
bool vl_get_mem_req(void *h, vl_mem_req_t *r) { return vl_p4_get_mem_req(h, r); }
bool vl_put_mem_resp(void *h, uint8_t t, uint8_t d, bool r) { return vl_p4_put_mem_resp(h, t, d, r); }

} // extern "C"
