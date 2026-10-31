/*
 * vl_bridge.cpp — Minimal C++ Bridge (< 90 lines) isolating Verilator.
 *
 * Implements the pure C99 ABI declared in vl_bridge.h.
 */

#include "vl_bridge.h"
#include "VmkTopPhase1.h"

extern "C" {

void *vl_create(void) {
    VmkTopPhase1 *top = new VmkTopPhase1();
    top->CLK = 0;
    top->RST_N = 1;
    top->EN_cpu_req = 0;
    top->EN_vdp_scan_req = 0;
    top->EN_vdp_cmd_req = 0;
    top->EN_geo3d_req = 0;
    top->EN_mem_req_deq = 0;
    top->EN_put_mem_resp = 0;
    top->EN_cpu_resp_deq = 0;
    top->eval();
    return (void *)top;
}

void vl_destroy(void *handle) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (top) {
        top->final();
        delete top;
    }
}

void vl_reset(void *handle) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top) return;
    top->RST_N = 0;
    for (int i = 0; i < 5; i++) {
        top->CLK = 1; top->eval();
        top->CLK = 0; top->eval();
    }
    top->RST_N = 1;
    /* Leave CLK=0 so the next vl_step() starts with posedge (correct capture) */
    top->CLK = 0; top->eval();
}

void vl_step(void *handle) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top) return;
    /*
     * Correct BSC/bluespec handshake timing:
     *   1. Posedge — registers capture whatever EN+data are set RIGHT NOW.
     *   2. Negedge  — combinatorial outputs settle (FIFO EMPTY_N, RDY, valid signals).
     *   3. Clear one-shot ENs so they don't re-fire on the NEXT posedge.
     *
     * Callers must set EN + data inputs BEFORE calling vl_step().
     * vl_step() captures them at posedge, then clears them.
     */
    top->CLK = 1;
    top->eval();
    top->CLK = 0;
    top->eval();
    /* Clear one-shot ENs after negedge — combinatorial outputs have settled */
    top->EN_cpu_req      = 0;
    top->EN_vdp_scan_req = 0;
    top->EN_vdp_cmd_req  = 0;
    top->EN_geo3d_req    = 0;
    top->EN_mem_req_deq  = 0;
    top->EN_put_mem_resp = 0;
    top->EN_cpu_resp_deq = 0;
}

void vl_get_clocks(void *handle, vl_clocks_t *clks) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !clks) return;
    clks->tick_28m   = (bool)top->tick_28m;
    clks->tick_7m    = (bool)top->tick_7m;
    clks->tick_3m58  = (bool)top->tick_3m58;
    clks->tick_e6    = (bool)top->tick_e6;
    clks->tick_pcm   = (bool)top->tick_pcm;
    clks->tick_audio = (bool)top->tick_audio;
}

bool vl_cpu_req(void *handle, uint32_t addr, uint8_t data, bool write, bool is_io) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !top->RDY_cpu_req) return false;
    top->cpu_req_addr = addr & 0xFFFFFFu;
    top->cpu_req_data = data;
    top->cpu_req_write = write ? 1 : 0;
    top->cpu_req_is_io = is_io ? 1 : 0;
    top->EN_cpu_req = 1;
    return true;
}

bool vl_vdp_scan_req(void *handle, uint32_t addr) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !top->RDY_vdp_scan_req) return false;
    top->vdp_scan_req_addr = addr & 0xFFFFFFu;
    top->EN_vdp_scan_req = 1;
    return true;
}

bool vl_vdp_cmd_req(void *handle, uint32_t addr, uint8_t data, bool write) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !top->RDY_vdp_cmd_req) return false;
    top->vdp_cmd_req_addr = addr & 0xFFFFFFu;
    top->vdp_cmd_req_data = data;
    top->vdp_cmd_req_write = write ? 1 : 0;
    top->EN_vdp_cmd_req = 1;
    return true;
}

bool vl_geo3d_req(void *handle, uint32_t addr, uint8_t data, bool write) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !top->RDY_geo3d_req) return false;
    top->geo3d_req_addr = addr & 0xFFFFFFu;
    top->geo3d_req_data = data;
    top->geo3d_req_write = write ? 1 : 0;
    top->EN_geo3d_req = 1;
    return true;
}

bool vl_has_mem_req(void *handle) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    return top ? (bool)top->mem_req_valid : false;
}

bool vl_get_mem_req(void *handle, vl_mem_req_t *req) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
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

bool vl_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !top->RDY_put_mem_resp) return false;
    top->put_mem_resp_tag = tag & 0x0Fu;
    top->put_mem_resp_data = data;
    top->put_mem_resp_ready = ready ? 1 : 0;
    top->EN_put_mem_resp = 1;
    return true;
}

bool vl_has_cpu_resp(void *handle) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    return top ? (bool)top->cpu_resp_valid : false;
}

bool vl_get_cpu_resp(void *handle, uint8_t *data) {
    VmkTopPhase1 *top = (VmkTopPhase1 *)handle;
    if (!top || !top->cpu_resp_valid || !top->RDY_cpu_resp_deq) return false;
    if (data) *data = top->cpu_resp_data;
    top->EN_cpu_resp_deq = 1;
    return true;
}

} // extern "C"

