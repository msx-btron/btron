/*
 * TopPhase3.bsv — Phase 3 Top-Level SoC: Integrated CPU, Slots & Memory Mapper.
 *
 * Integrates:
 *   - MSXClocks: 57.272728 MHz master PLL & tick generators
 *   - MemoryArbiter: 4-port memory/IO crossbar
 *   - CPUCore: unified Z80A/R800 execution engine
 *   - SlotManager: 4-page Primary (Port #A8) & Secondary (0xFFFF) slot translator
 *   - Mapper: 512KB-4MB Memory Mapper engine (Ports #FC..#FF)
 *
 * Boot verification:
 *   - Runs headless C-BIOS / ROM boot sequence with full address translation.
 */

package TopPhase3;

import MSXClocks::*;
import Bus::*;
import MemoryArbiter::*;
import CPUCore::*;
import SlotManager::*;
import Mapper::*;
import GetPut::*;
import FIFOF::*;

interface TopPhase3;
    // Clock Ticks
    (* always_ready *) method Bool tick_28m();
    (* always_ready *) method Bool tick_7m();
    (* always_ready *) method Bool tick_3m58();
    (* always_ready *) method Bool tick_e6();

    // Outbound physical memory bus to external memory model
    (* always_ready *) method Bool      mem_req_valid();
    (* always_ready *) method BusAddr   mem_req_addr();
    (* always_ready *) method BusData   mem_req_data();
    (* always_ready *) method Bool      mem_req_write();
    (* always_ready *) method Bool      mem_req_is_io();
    (* always_ready *) method MasterTag mem_req_tag();
    method Action mem_req_deq();

    // Inbound memory response from external memory model
    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);

    // CPU Architectural Introspection
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
    (* always_ready *) method Bool     cpu_inst_retired();
    (* always_ready *) method Bool     cpu_is_r800();

    // Slot & Mapper Introspection
    (* always_ready *) method Bit#(8)  slot_ppi_a();
endinterface: TopPhase3

(* synthesize *)
module mkTopPhase3(TopPhase3);
    MSXClocks     clocks      <- mkMSXClocks;
    MemoryArbiter arbiter     <- mkMemoryArbiter;
    CPUCore       cpu         <- mkCPUCore;
    SlotManager   slot_mgr    <- mkSlotManager;
    Mapper        mapper      <- mkMapper;

    // Outbound request staging queue
    FIFOF#(BusReq) q_out      <- mkFIFOF;

    // Forward clock tick enables into CPUCore
    rule pump_clocks;
        if (clocks.tick_28m)  cpu.tick_28m;
        if (clocks.tick_7m)   cpu.tick_7m;
        if (clocks.tick_3m58) cpu.tick_3m58;
        if (clocks.tick_e6)   cpu.tick_e6;
    endrule

    // Keep SlotManager DRAM shadow mode synced with CPU mode
    rule sync_dram_mode;
        // In turboR, DRAM mode is active when not in ROM mode
        slot_mgr.set_dram_mode(cpu.is_r800_mode());
    endrule

    // Intercept and translate CPU bus requests through SlotManager & Mapper
    rule translate_and_route_cpu_req;
        let req <- cpu.bus_req.get();

        if (req.is_io) begin
            // Intercept I/O port writes
            Bit#(8) port = req.addr[7:0];
            if (req.write) begin
                if (port == 8'hA8) begin
                    slot_mgr.write_ppi_a(req.data);
                end else if (port >= 8'hFC && port <= 8'hFF) begin
                    mapper.io_write(port, req.data);
                end
            end
            // Forward I/O request to arbiter
            arbiter.cpu_req.put(req);
        end else begin
            // Memory address translation: 16-bit virtual -> 24-bit physical
            Bit#(16) vaddr = req.addr[15:0];
            BusAddr  paddr = 0;

            // Check if page targets Slot 3 (Internal RAM with Memory Mapper)
            Bit#(8) ppi_val = slot_mgr.read_ppi_a();
            Bit#(2) page    = vaddr[15:14];
            Bit#(2) prim    = case (page)
                2'd0: ppi_val[1:0];
                2'd1: ppi_val[3:2];
                2'd2: ppi_val[5:4];
                2'd3: ppi_val[7:6];
            endcase;

            if (prim == 2'd3) begin
                // Slot 3: Internal RAM mapped through 16KB segment registers
                paddr = mapper.translate_ram(vaddr);
            end else begin
                // Slot 0, 1, 2: Standard BIOS / Cartridge slots
                paddr = slot_mgr.translate(vaddr);
            end

            arbiter.cpu_req.put(BusReq {
                tag:   tagCPU,
                addr:  paddr,
                data:  req.data,
                write: req.write,
                is_io: False
            });
        end
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

    // Outbound Memory Request
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

    // Slot Introspection
    method Bit#(8)  slot_ppi_a()       = slot_mgr.read_ppi_a();
endmodule: mkTopPhase3

endpackage: TopPhase3

