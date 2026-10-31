/*
 * vl_bridge_p3.cpp — Minimal C++ Bridge Isolating Verilator for Phase 3.
 */

#include "vl_bridge_p3.h"
#include "VmkTopPhase3.h"

extern "C" {

void *vl_p3_create(void) {
    VmkTopPhase3 *top = new VmkTopPhase3();
    top->CLK = 0;
    top->RST_N = 1;
    top->EN_mem_req_deq = 0;
    top->EN_put_mem_resp = 0;
    top->eval();
    return (void *)top;
}

void vl_p3_destroy(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    if (top) {
        top->final();
        delete top;
    }
}

void vl_p3_reset(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    if (!top) return;
    top->RST_N = 0;
    for (int i = 0; i < 5; i++) {
        top->CLK = 1; top->eval();
        top->CLK = 0; top->eval();
    }
    top->RST_N = 1;
    top->CLK = 0; top->eval();
}

void vl_p3_step(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    if (!top) return;
    top->CLK = 1;
    top->eval();
    top->CLK = 0;
    top->eval();
    top->EN_mem_req_deq  = 0;
    top->EN_put_mem_resp = 0;
}

bool vl_p3_has_mem_req(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    return top ? (bool)top->mem_req_valid : false;
}

bool vl_p3_get_mem_req(void *handle, vl_mem_req_t *req) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    if (!top || !top->mem_req_valid || !top->RDY_mem_req_deq) return false;
    if (req) {
        req->addr  = top->mem_req_addr;
        req->data  = top->mem_req_data;
        req->write = (bool)top->mem_req_write;
        req->is_io = (bool)top->mem_req_is_io;
        req->tag   = (uint8_t)top->mem_req_tag;
    }
    top->EN_mem_req_deq = 1;
    return true;
}

bool vl_p3_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    if (!top || !top->RDY_put_mem_resp) return false;
    top->put_mem_resp_tag   = tag & 0x0Fu;
    top->put_mem_resp_data  = data;
    top->put_mem_resp_ready = ready ? 1 : 0;
    top->EN_put_mem_resp    = 1;
    return true;
}

void vl_p3_get_regs(void *handle, trace_regs_t *regs) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    if (!top || !regs) return;
    regs->pc = (uint16_t)top->cpu_pc;
    regs->a  = (uint8_t)top->cpu_a;
    regs->f  = (uint8_t)top->cpu_f;
    regs->b  = (uint8_t)top->cpu_b;
    regs->c  = (uint8_t)top->cpu_c;
    regs->d  = (uint8_t)top->cpu_d;
    regs->e  = (uint8_t)top->cpu_e;
    regs->h  = (uint8_t)top->cpu_h;
    regs->l  = (uint8_t)top->cpu_l;
    regs->ix = (uint16_t)top->cpu_ix;
    regs->iy = (uint16_t)top->cpu_iy;
    regs->sp = (uint16_t)top->cpu_sp;
}

bool vl_p3_inst_retired(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    return top ? (bool)top->cpu_inst_retired : false;
}

bool vl_p3_is_r800(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    return top ? (bool)top->cpu_is_r800 : false;
}

uint8_t vl_p3_slot_ppi_a(void *handle) {
    VmkTopPhase3 *top = (VmkTopPhase3 *)handle;
    return top ? (uint8_t)top->slot_ppi_a : 0;
}

// Unified memory model callbacks
bool vl_has_mem_req(void *handle) {
    return vl_p3_has_mem_req(handle);
}

bool vl_get_mem_req(void *handle, vl_mem_req_t *req) {
    return vl_p3_get_mem_req(handle, req);
}

bool vl_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready) {
    return vl_p3_put_mem_resp(handle, tag, data, ready);
}

} // extern "C"

