/*
 * TopPhase5.bsv — Phase 5 Top-Level SoC: Audio Matrix & I/O.
 *
 * Integrates Phase 4 SoC + Audio (PSG, SCC, PCM) + I/O (PPI, Keyboard).
 *
 * I/O port routing:
 *   0xA0–0xA2  → PSG (AY-3-8910)
 *   0xA4–0xA5  → PCM DAC
 *   0xA8–0xAB  → PPI (8255)
 *   0x9800–0x9FFF (mem) → SCC (Konami wavetable)
 *
 * Audio tick policy (single-clock domain, phase-enables):
 *   PSG ticks at /8 of MSX_PSG_HZ; gated by a counter driven from tick_3m58.
 *   PCM ticks every 228 Z80 T-states; driven by T-state counter on tick_3m58.
 *   SCC ticks on every tick_3m58 (Z80 clock rate, same gating).
 *
 * Keyboard: PPI port C row drives keyboard row; Keyboard module returns
 *           column bits fed back into PPI port B each cycle.
 *
 * Introspection ports expose PSG, PPI, Keyboard, and PCM state.
 */

package TopPhase5;

import MSXClocks::*;
import Bus::*;
import MemoryArbiter::*;
import CPUCore::*;
import SlotManager::*;
import Mapper::*;
import V9958::*;
import PSG::*;
import SCC::*;
import PCM::*;
import PPI::*;
import Keyboard::*;
import GetPut::*;
import FIFOF::*;

interface TopPhase5;
    // Clock ticks
    (* always_ready *) method Bool tick_28m();
    (* always_ready *) method Bool tick_7m();
    (* always_ready *) method Bool tick_3m58();
    (* always_ready *) method Bool tick_e6();

    // Outbound physical memory bus
    (* always_ready *) method Bool      mem_req_valid();
    (* always_ready *) method BusAddr   mem_req_addr();
    (* always_ready *) method BusData   mem_req_data();
    (* always_ready *) method Bool      mem_req_write();
    (* always_ready *) method Bool      mem_req_is_io();
    (* always_ready *) method MasterTag mem_req_tag();
    method Action mem_req_deq();

    // Inbound memory response
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

    // VDP signals (pass-through from Phase 4)
    (* always_ready *) method Bool     vdp_irq();
    (* always_ready *) method Bit#(9)  vdp_beam_x();
    (* always_ready *) method Bit#(8)  vdp_beam_y();

    // Audio Introspection
    (* always_ready *) method Bit#(16) psg_sample();
    (* always_ready *) method Bit#(16) scc_sample();
    (* always_ready *) method Bit#(8)  pcm_sample();
    (* always_ready *) method Bit#(8)  psg_peek_reg(Bit#(4) idx);
    (* always_ready *) method Bit#(17) psg_peek_lfsr();

    // PPI / Keyboard Introspection
    (* always_ready *) method Bit#(8)  ppi_peek_pc();
    (* always_ready *) method Bit#(8)  ppi_read_a();
    method Bit#(8) kbd_peek_row(Bit#(4) row);

    // Keyboard injection (testbench-driven)
    method Action kbd_key_down(Bit#(4) row, Bit#(3) col);
    method Action kbd_key_up(Bit#(4) row, Bit#(3) col);

endinterface: TopPhase5

(* synthesize *)
module mkTopPhase5(TopPhase5);

    // ---- Submodules ----
    MSXClocks     clocks   <- mkMSXClocks;
    MemoryArbiter arbiter  <- mkMemoryArbiter;
    CPUCore       cpu      <- mkCPUCore;
    SlotManager   slot_mgr <- mkSlotManager;
    Mapper        mapper   <- mkMapper;
    V9958         vdp      <- mkV9958;
    PSG           psg      <- mkPSG;
    SCC           scc      <- mkSCC;
    PCM           pcm      <- mkPCM;
    PPI           ppi      <- mkPPI;
    Keyboard      kbd      <- mkKeyboard;

    // Outbound FIFO
    FIFOF#(BusReq) q_out <- mkFIFOF;

    // CPU response mux FIFO
    FIFOF#(BusResp) q_cpu_resp_mux <- mkFIFOF;

    // ---- PSG tick divider: /8 of PSG clock (PSG_HZ = Z80_HZ/2) ----
    // We gate PSG tick on tick_3m58 and divide by 4 more (PSG tick = 8 PSG clocks,
    // 1 PSG clock = 2 Z80 clocks; so PSG tick = 16 Z80 clocks counted at tick_3m58).
    // tick_3m58 fires at ~3.579545 MHz (Z80 rate).  PSG tick = every 16 tick_3m58.
    Reg#(Bit#(4)) r_psg_div <- mkReg(0);

    // ---- PCM T-state accumulator ----
    // tick_3m58 = 1 Z80 T-state.  PCM tick every 228 T-states.
    Reg#(Bit#(8)) r_pcm_acc <- mkReg(0);

    // ---- Clock domain advancement (identical to Phase 4) ----
    rule step_clocks;
        if (clocks.tick_28m)  cpu.tick_28m;
        if (clocks.tick_7m)   cpu.tick_7m;
        if (clocks.tick_3m58) begin
            cpu.tick_3m58;
            vdp.tick_3m58;
        end
        if (clocks.tick_e6)   cpu.tick_e6;
    endrule

    // ---- Audio tick generators (run on 3.58 MHz tick) ----
    rule step_audio (clocks.tick_3m58);
        scc.tick_scc;

        // PSG divider (/16)
        Bit#(4) nd = r_psg_div + 1;
        r_psg_div <= nd;
        if (nd == 0) psg.tick_psg;

        // PCM accumulator
        Bit#(8) na = r_pcm_acc + 1;
        if (na >= 228) begin
            r_pcm_acc <= na - 228;
            pcm.tick_pcm;
        end else begin
            r_pcm_acc <= na;
        end
    endrule

    // ---- Keyboard matrix scan (run on 3.58 MHz tick) ----
    rule sync_keyboard (clocks.tick_3m58);
        Bit#(4) row = ppi.kbd_row;
        ppi.set_kbd_data(kbd.kbd_row_bits(row));
    endrule

    rule sync_dram_mode;
        slot_mgr.set_dram_mode(cpu.is_r800_mode());
    endrule

    // ---- CPU bus request routing ----
    rule translate_and_route_cpu_req;
        let req <- cpu.bus_req.get();

        if (req.is_io) begin
            Bit#(8) port = req.addr[7:0];

            Bool is_vdp = (port >= 8'h98 && port <= 8'h9B) ||
                          (port >= 8'h88 && port <= 8'h8B);
            Bool is_psg = (port >= 8'hA0 && port <= 8'hA2);
            Bool is_pcm = (port == 8'hA4 || port == 8'hA5);
            Bool is_ppi = (port >= 8'hA8 && port <= 8'hAB);
            Bool is_map = (port >= 8'hFC && port <= 8'hFF);

            if (is_vdp) begin
                if (req.write) begin
                    vdp.io_write(port, req.data);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
                end else begin
                    let rdata <- vdp.io_read(port);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: rdata, ready: True });
                end
            end else if (is_psg) begin
                if (req.write) begin
                    psg.io_write(port, req.data);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
                end else begin
                    let rdata <- psg.io_read(port);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: rdata, ready: True });
                end
            end else if (is_pcm) begin
                if (req.write) begin
                    pcm.io_write(port, req.data);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
                end else begin
                    let rdata <- pcm.io_read(port);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: rdata, ready: True });
                end
            end else if (is_ppi) begin
                if (req.write) begin
                    // Port A also updates SlotManager
                    if ((port & 8'h03) == 0)
                        slot_mgr.write_ppi_a(req.data);
                    ppi.io_write(port, req.data);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
                end else begin
                    let rdata <- ppi.io_read(port);
                    q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: rdata, ready: True });
                end
            end else if (req.write && is_map) begin
                mapper.io_write(port, req.data);
                q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
            end else if (req.write && port == 8'hA8) begin
                slot_mgr.write_ppi_a(req.data);
                q_cpu_resp_mux.enq(BusResp { tag: tagCPU, data: 0, ready: True });
            end else begin
                arbiter.cpu_req.put(req);
            end
        end else begin
            Bit#(16) vaddr = req.addr[15:0];
            Bit#(8)  ppi_val = slot_mgr.read_ppi_a();
            Bit#(2)  page    = vaddr[15:14];
            Bit#(2)  prim    = case (page)
                2'd0: ppi_val[1:0];
                2'd1: ppi_val[3:2];
                2'd2: ppi_val[5:4];
                2'd3: ppi_val[7:6];
            endcase;

            BusAddr paddr;
            if (prim == 2'd3)
                paddr = mapper.translate_ram(vaddr);
            else
                paddr = slot_mgr.translate(vaddr);

            arbiter.cpu_req.put(BusReq {
                tag:   tagCPU,
                addr:  paddr,
                data:  req.data,
                write: req.write,
                is_io: False
            });
        end
    endrule

    rule pump_arb_to_cpu;
        let resp <- arbiter.cpu_resp.get();
        q_cpu_resp_mux.enq(resp);
    endrule

    rule pump_resp_to_cpu;
        let resp <- toGet(q_cpu_resp_mux).get();
        cpu.bus_resp.put(resp);
    endrule

    // VDP scan & cmd bus pipes
    rule pump_vdp_scan_req;
        let req <- vdp.vdp_scan_req.get();
        arbiter.vdp_scan_req.put(req);
    endrule

    rule pump_vdp_scan_resp;
        let resp <- arbiter.vdp_scan_resp.get();
        vdp.vdp_scan_resp.put(resp);
    endrule

    rule pump_vdp_cmd_req;
        let req <- vdp.vdp_cmd_req.get();
        arbiter.vdp_cmd_req.put(req);
    endrule

    rule pump_vdp_cmd_resp;
        let resp <- arbiter.vdp_cmd_resp.get();
        vdp.vdp_cmd_resp.put(resp);
    endrule

    rule pump_arb_out;
        let req <- arbiter.get_bus_req();
        q_out.enq(req);
    endrule

    // ---- Interface ----
    method Bool tick_28m  = clocks.tick_28m;
    method Bool tick_7m   = clocks.tick_7m;
    method Bool tick_3m58 = clocks.tick_3m58;
    method Bool tick_e6   = clocks.tick_e6;

    method Bool      mem_req_valid() = q_out.notEmpty;
    method BusAddr   mem_req_addr()  = q_out.notEmpty ? q_out.first.addr  : 0;
    method BusData   mem_req_data()  = q_out.notEmpty ? q_out.first.data  : 0;
    method Bool      mem_req_write() = q_out.notEmpty ? q_out.first.write : False;
    method Bool      mem_req_is_io() = q_out.notEmpty ? q_out.first.is_io : False;
    method MasterTag mem_req_tag()   = q_out.notEmpty ? q_out.first.tag   : 0;

    method Action mem_req_deq();
        if (q_out.notEmpty) q_out.deq();
    endmethod

    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);
        arbiter.put_bus_resp(BusResp { tag: tag, data: data, ready: ready });
    endmethod

    // CPU introspection
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

    // VDP
    method Bool    vdp_irq()    = vdp.irq();
    method Bit#(9) vdp_beam_x() = vdp.beam_x();
    method Bit#(8) vdp_beam_y() = vdp.beam_y();

    // Audio
    method Bit#(16) psg_sample()         = psg.pcm_sample;
    method Bit#(16) scc_sample()         = scc.pcm_sample;
    method Bit#(8)  pcm_sample()         = pcm.pcm_sample;
    method Bit#(8)  psg_peek_reg(Bit#(4) idx) = psg.peek_reg(idx);
    method Bit#(17) psg_peek_lfsr()      = psg.peek_lfsr;

    // PPI / Keyboard
    method Bit#(8) ppi_peek_pc() = ppi.peek_pc;
    method Bit#(8) ppi_read_a()  = ppi.read_ppi_a;
    method Bit#(8) kbd_peek_row(Bit#(4) row) = kbd.peek_row_bits(row);

    method Action kbd_key_down(Bit#(4) row, Bit#(3) col); kbd.kbd_down(row, col); endmethod
    method Action kbd_key_up  (Bit#(4) row, Bit#(3) col); kbd.kbd_up(row, col);   endmethod

endmodule: mkTopPhase5

endpackage: TopPhase5

