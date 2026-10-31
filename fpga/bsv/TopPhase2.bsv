/*
 * TopPhase2.bsv — Phase 2 Top-Level SoC: Clocks, Memory Arbiter & CPU Core.
 *
 * Integrates:
 *   - MSXClocks: single master PLL & tick pulse generator
 *   - MemoryArbiter: 4-port memory/IO arbiter
 *   - CPUCore: unified Z80A/R800 execution engine with S1990 & multiplier
 *
 * Exposes:
 *   - Outbound physical bus to C99 flat memory model
 *   - Inbound memory response from C99 memory model
 *   - CPU register introspection & instruction retired strobe for lock-step co-sim
 */

package TopPhase2;

import MSXClocks::*;
import Bus::*;
import MemoryArbiter::*;
import CPUCore::*;
import GetPut::*;
import FIFOF::*;

interface TopPhase2;
    // Clock Ticks
    (* always_ready *) method Bool tick_28m();
    (* always_ready *) method Bool tick_7m();
    (* always_ready *) method Bool tick_3m58();
    (* always_ready *) method Bool tick_e6();

    // Outbound physical memory bus (to C99 memory model)
    (* always_ready *) method Bool      mem_req_valid();
    (* always_ready *) method BusAddr   mem_req_addr();
    (* always_ready *) method BusData   mem_req_data();
    (* always_ready *) method Bool      mem_req_write();
    (* always_ready *) method Bool      mem_req_is_io();
    (* always_ready *) method MasterTag mem_req_tag();
    method Action mem_req_deq();

    // Inbound memory response from C99 memory model
    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);

    // CPU Architectural Introspection for Verilator Co-Sim
    (* always_ready *) method Bit#(16) cpu_pc();
    (* always_ready *) method Bit#(8)  cpu_a();
    (* always_ready *) method Bit#(8)  cpu_f();
    (* always_ready *) method Bit#(8)  cpu_b();
    (* always_ready *) method Bit#(8)  cpu_c();
    (* always_ready *) method Bit#(8)  cpu_d();
    (* always_ready *) method Bit#(8)  cpu_e();
    (* always_ready *) method Bit#(8)  cpu_h();
    (* always_ready *) method Bit#(8)  cpu_l();
    (* always_ready *) method Bit#(16) cpu_ix();
    (* always_ready *) method Bit#(16) cpu_iy();
    (* always_ready *) method Bit#(16) cpu_sp();
    (* always_ready *) method Bool    cpu_inst_retired();
    (* always_ready *) method Bool    cpu_is_r800();
    (* always_ready *) method Bit#(5) cpu_state();
    (* always_ready *) method Bit#(8) cpu_opcode();
endinterface: TopPhase2

(* synthesize *)
module mkTopPhase2(TopPhase2);
    MSXClocks     clocks  <- mkMSXClocks;
    MemoryArbiter arbiter <- mkMemoryArbiter;
    CPUCore       cpu     <- mkCPUCore;

    // Staging FIFO for the outbound bus requests (filled by arbiter)
    FIFOF#(BusReq) q_out <- mkFIFOF;

    // Forward clock tick enables into CPUCore
    rule pump_clocks;
        if (clocks.tick_28m)  cpu.tick_28m;
        if (clocks.tick_7m)   cpu.tick_7m;
        if (clocks.tick_3m58) cpu.tick_3m58;
        if (clocks.tick_e6)   cpu.tick_e6;
    endrule

    // Connect CPU memory requests into arbiter
    rule pump_cpu_to_arb;
        let req <- cpu.bus_req.get();
        arbiter.cpu_req.put(req);
    endrule

    // Connect arbiter CPU responses back to CPU
    rule pump_arb_to_cpu;
        let resp <- arbiter.cpu_resp.get();
        cpu.bus_resp.put(resp);
    endrule

    // Forward arbiter's outbound physical memory request into our staging FIFO
    rule pump_arb_out;
        let req <- arbiter.get_bus_req();
        q_out.enq(req);
    endrule

    // Clock ticks
    method Bool tick_28m   = clocks.tick_28m;
    method Bool tick_7m    = clocks.tick_7m;
    method Bool tick_3m58  = clocks.tick_3m58;
    method Bool tick_e6    = clocks.tick_e6;

    // Outbound Memory Request — directly from staging FIFO head
    method Bool      mem_req_valid() = q_out.notEmpty;
    method BusAddr   mem_req_addr()  = q_out.notEmpty ? q_out.first.addr  : 0;
    method BusData   mem_req_data()  = q_out.notEmpty ? q_out.first.data  : 0;
    method Bool      mem_req_write() = q_out.notEmpty ? q_out.first.write : False;
    method Bool      mem_req_is_io() = q_out.notEmpty ? q_out.first.is_io : False;
    method MasterTag mem_req_tag()   = q_out.notEmpty ? q_out.first.tag   : 0;
    method Action mem_req_deq() if (q_out.notEmpty);
        q_out.deq;
    endmethod

    // Inbound Memory Response from C99 Memory Model
    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);
        arbiter.put_bus_resp(BusResp { tag: tag, data: data, ready: ready });
    endmethod

    // CPU Architectural Register Introspection
    method Bit#(16) cpu_pc()           = cpu.get_regs().pc;
    method Bit#(8)  cpu_a()            = cpu.get_regs().a;
    method Bit#(8)  cpu_f()            = cpu.get_regs().f;
    method Bit#(8)  cpu_b()            = cpu.get_regs().b;
    method Bit#(8)  cpu_c()            = cpu.get_regs().c;
    method Bit#(8)  cpu_d()            = cpu.get_regs().d;
    method Bit#(8)  cpu_e()            = cpu.get_regs().e;
    method Bit#(8)  cpu_h()            = cpu.get_regs().h;
    method Bit#(8)  cpu_l()            = cpu.get_regs().l;
    method Bit#(16) cpu_ix()           = cpu.get_regs().ix;
    method Bit#(16) cpu_iy()           = cpu.get_regs().iy;
    method Bit#(16) cpu_sp()           = cpu.get_regs().sp;
    method Bool     cpu_inst_retired() = cpu.inst_retired();
    method Bool     cpu_is_r800()      = cpu.is_r800_mode();
    method Bit#(5)  cpu_state()        = cpu.get_state();
    method Bit#(8)  cpu_opcode()       = cpu.get_opcode();
endmodule: mkTopPhase2

endpackage: TopPhase2
