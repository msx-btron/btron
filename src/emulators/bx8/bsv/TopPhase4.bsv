/*
 * TopPhase4.bsv — Phase 4 Top-Level SoC: Integrated CPU, Slots, Memory & V9958 VDP.
 *
 * Integrates:
 *   - MSXClocks: 57.272728 MHz master PLL & zero-CDC tick generators
 *   - MemoryArbiter: 4-port memory/IO crossbar:
 *       Client 0: CPU Core
 *       Client 1: VDP Scanout Rasterizer (strict priority)
 *       Client 2: VDP Command Engine / Blitter & CPU VRAM (round-robin)
 *       Client 3: Geo3D Coprocessor
 *   - CPUCore: unified Z80A/R800 execution engine
 *   - SlotManager: 4-page Primary (Port #A8) & Secondary (0xFFFF) slot translator
 *   - Mapper: 512KB-4MB Memory Mapper engine (Ports #FC..#FF)
 *   - V9958: Yamaha V9958 Video Display Processor (Ports 0x98..0x9B / 0x88..0x8B)
 *
 * Verification:
 *   - V9958 registers R#0..R#46, status S#0..S#9, palette programming
 *   - VRAM read/write and screen mode address wrapping
 *   - Alex Wulms Command Engine blitter operations
 *   - Video raster timing, beam tracking, VBlank/Line IRQ
 *   - Pure C99 test harness with headless PNG frame dump & pixel-exact co-sim
 */

package TopPhase4;

import MSXClocks::*;
import Bus::*;
import MemoryArbiter::*;
import CPUCore::*;
import SlotManager::*;
import Mapper::*;
import V9958::*;
import GetPut::*;
import FIFOF::*;

interface TopPhase4;
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

    // VDP Video Signals
    (* always_ready *) method Bool     vdp_irq();
    (* always_ready *) method Bit#(9)  vdp_beam_x();
    (* always_ready *) method Bit#(8)  vdp_beam_y();
    (* always_ready *) method Bit#(9)  vdp_line();
    (* always_ready *) method Bool     vdp_hsync();
    (* always_ready *) method Bool     vdp_vsync();
    (* always_ready *) method Bool     vdp_blank();
    (* always_ready *) method Bit#(16) vdp_pixel();

    // VDP Introspection
    method Bit#(8)  vdp_peek_reg(Bit#(6) idx);
    method Bit#(8)  vdp_peek_stat(Bit#(4) idx);
    method Bit#(16) vdp_peek_palette(Bit#(4) idx);
    method Bit#(18) vdp_peek_vram_ptr();
    method Bit#(8)  vdp_peek_read_ahead();
endinterface: TopPhase4

(* synthesize *)
module mkTopPhase4(TopPhase4);
    // Submodules
    MSXClocks     clocks   <- mkMSXClocks;
    MemoryArbiter arbiter  <- mkMemoryArbiter;
    CPUCore       cpu      <- mkCPUCore;
    SlotManager   slot_mgr <- mkSlotManager;
    Mapper        mapper   <- mkMapper;
    V9958         vdp      <- mkV9958;

    // Outbound request FIFO for Verilator inspection
    FIFOF#(BusReq) q_out <- mkFIFOF;

    // Unified CPU Response Mux FIFO
    FIFOF#(BusResp) q_cpu_resp_mux <- mkFIFOF;

    // Advance clock domains
    rule step_clocks;
        if (clocks.tick_28m)  cpu.tick_28m;
        if (clocks.tick_7m)   cpu.tick_7m;
        if (clocks.tick_3m58) begin
            cpu.tick_3m58;
            vdp.tick_3m58;
        end
        if (clocks.tick_e6)   cpu.tick_e6;
    endrule

    // Keep SlotManager DRAM shadow mode synced with CPU mode
    rule sync_dram_mode;
        slot_mgr.set_dram_mode(cpu.is_dram_mode());
    endrule

    // Intercept and translate CPU bus requests through SlotManager, Mapper & VDP
    rule translate_and_route_cpu_req;
        let req <- cpu.bus_req.get();

        if (req.is_io) begin
            Bit#(8) port = req.addr[7:0];
            Bool is_vdp_port = (port >= 8'h98 && port <= 8'h9B) || (port >= 8'h88 && port <= 8'h8B);

            if (is_vdp_port) begin
                // Route to V9958 VDP
                if (req.write) begin
                    vdp.io_write(port, req.data);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
                end else begin
                    let rdata <- vdp.io_read(port);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: rdata, ready: True });
                end
            end else if (req.write && port == 8'hA8) begin
                // PPI Port A: Primary Slot Select
                slot_mgr.write_ppi_a(req.data);
                q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
            end else if (req.write && port >= 8'hFC && port <= 8'hFF) begin
                // Memory Mapper Segment Select
                mapper.io_write(port, req.data);
                q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
            end else begin
                // Other I/O: forward to external arbiter
                arbiter.cpu_req.put(req);
            end
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
                paddr = mapper.translate_ram(vaddr);
            end else begin
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

    // Connect arbiter CPU responses back into the CPU response mux
    rule pump_arb_to_cpu;
        let resp <- arbiter.cpu_resp.get();
        q_cpu_resp_mux.enq(resp);
    endrule

    // Deliver unified responses to CPU
    rule pump_resp_to_cpu;
        let resp <- toGet(q_cpu_resp_mux).get();
        cpu.bus_resp.put(resp);
    endrule

    // Connect VDP Scanout client to MemoryArbiter client 1 (Strict Priority)
    rule pump_vdp_scan_req;
        let req <- vdp.vdp_scan_req.get();
        arbiter.vdp_scan_req.put(req);
    endrule

    rule pump_vdp_scan_resp;
        let resp <- arbiter.vdp_scan_resp.get();
        vdp.vdp_scan_resp.put(resp);
    endrule

    // Connect VDP Command Engine / Blitter client to MemoryArbiter client 2 (Round-Robin)
    rule pump_vdp_cmd_req;
        let req <- vdp.vdp_cmd_req.get();
        arbiter.vdp_cmd_req.put(req);
    endrule

    rule pump_vdp_cmd_resp;
        let resp <- arbiter.vdp_cmd_resp.get();
        vdp.vdp_cmd_resp.put(resp);
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

    method Action mem_req_deq();
        if (q_out.notEmpty) q_out.deq();
    endmethod

    // Inbound Memory Response
    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);
        arbiter.put_bus_resp(BusResp {
            tag:   tag,
            data:  data,
            ready: ready
        });
    endmethod

    // CPU Architectural Introspection
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

    // VDP Video Signals
    method Bool     vdp_irq()          = vdp.irq();
    method Bit#(9)  vdp_beam_x()       = vdp.beam_x();
    method Bit#(8)  vdp_beam_y()       = vdp.beam_y();
    method Bit#(9)  vdp_line()         = vdp.current_line();
    method Bool     vdp_hsync()        = vdp.hsync();
    method Bool     vdp_vsync()        = vdp.vsync();
    method Bool     vdp_blank()        = vdp.blank();
    method Bit#(16) vdp_pixel()        = vdp.pixel_rgb565();

    // VDP Introspection
    method Bit#(8)  vdp_peek_reg(Bit#(6) idx)     = vdp.peek_reg(idx);
    method Bit#(8)  vdp_peek_stat(Bit#(4) idx)    = vdp.peek_stat(idx);
    method Bit#(16) vdp_peek_palette(Bit#(4) idx) = vdp.peek_palette(idx);
    method Bit#(18) vdp_peek_vram_ptr()           = vdp.peek_vram_ptr();
    method Bit#(8)  vdp_peek_read_ahead()         = vdp.peek_read_ahead();
endmodule: mkTopPhase4

endpackage: TopPhase4
