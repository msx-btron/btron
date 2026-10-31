/*
 * TopMSXTurboR.bsv — Complete MSX turbo R SoC Top-Level Module.
 *
 * Clean-room Bluespec SystemVerilog implementation isomorphic with
 * src/emulators/msx/.
 *
 * Integrates:
 *   - MSXClocks: 57.272728 MHz master PLL & zero-CDC synchronous phase enables
 *   - MemoryArbiter: 4-port physical memory crossbar:
 *       Client 0: CPU Core (R800 / Z80A)
 *       Client 1: VDP Scanout Rasterizer (Strict Priority)
 *       Client 2: VDP Command Engine / Blitter & CPU VRAM (Round-Robin)
 *       Client 3: Auxiliary / Coprocessor
 *   - CPUCore: Dual-mode Z80A (3.58MHz) / R800 (28.64MHz) execution engine
 *              with 16x16 hardware multiplier (MULUB, MULUQ)
 *   - T9769C: S1990 MSX turbo R Engine (Ports 0xE4/0xE5), E6 timer, DRAM mode
 *   - SlotManager: 4-page Primary (Port 0xA8) & Secondary (0xFFFF) slot translator
 *   - Mapper: 512KB–4MB Memory Mapper engine (Ports 0xFC..0xFF)
 *   - V9958: Yamaha V9958 Video Display Processor (Ports 0x98..0x9B / 0x88..0x8B)
 *            with RGB565 palette RAM, raster timing, Alex Wulms blitter, YJK color
 *   - PSG: AY-3-8910 Sound Generator (Ports 0xA0..0xA2)
 *   - SCC: Konami 5-channel wavetable synthesizer (Memory 0x9800..0x9FFF)
 *   - PCM: turboR 8-bit DAC/ADC (Ports 0xA4/0xA5) with 15.7 kHz sample clock
 *   - PPI: Intel 8255 (Ports 0xA8..0xAB)
 *   - Keyboard: 11x8 matrix scanner with active-low column output
 *
 * Interfaces:
 *   - Outbound physical memory bus (to external SRAM / SDRAM / BRAM)
 *   - Digital video output: 16-bit RGB565, HSYNC, VSYNC, BLANK, BEAM_X, BEAM_Y
 *   - Digital audio output: 16-bit mixed PCM sample stream
 *   - LED status indicators: CAPS LED, KANA LED, R800 Turbo LED
 *   - Keyboard & Joystick interactive controls
 */

package TopMSXTurboR;

import MSXClocks::*;
import Bus::*;
import MemoryArbiter::*;
import CPUCore::*;
import T9769C::*;
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

interface TopMSXTurboR;
    // Clock Ticks
    (* always_ready *) method Bool tick_28m();
    (* always_ready *) method Bool tick_7m();
    (* always_ready *) method Bool tick_3m58();
    (* always_ready *) method Bool tick_e6();

    // Outbound physical memory bus to external memory controller
    (* always_ready *) method Bool      mem_req_valid();
    (* always_ready *) method BusAddr   mem_req_addr();
    (* always_ready *) method BusData   mem_req_data();
    (* always_ready *) method Bool      mem_req_write();
    (* always_ready *) method Bool      mem_req_is_io();
    (* always_ready *) method MasterTag mem_req_tag();
    method Action mem_req_deq();

    // Inbound memory response from external memory controller
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

    // Video Output Signals
    (* always_ready *) method Bit#(16) video_rgb565();
    (* always_ready *) method Bool     video_hsync();
    (* always_ready *) method Bool     video_vsync();
    (* always_ready *) method Bool     video_blank();
    (* always_ready *) method Bit#(9)  video_beam_x();
    (* always_ready *) method Bit#(8)  video_beam_y();
    (* always_ready *) method Bool     vdp_irq();

    // Audio Output Signals (Mixed 16-bit PCM Stream)
    (* always_ready *) method Bit#(16) audio_pcm_sample();
    (* always_ready *) method Bit#(16) psg_raw_sample();
    (* always_ready *) method Bit#(16) scc_raw_sample();
    (* always_ready *) method Bit#(8)  pcm_raw_sample();

    // LED Status Indicators
    (* always_ready *) method Bool led_caps();
    (* always_ready *) method Bool led_kana();
    (* always_ready *) method Bool led_turbo();

    // Peripheral Introspection
    (* always_ready *) method Bit#(8)  ppi_peek_pc();
    (* always_ready *) method Bit#(8)  ppi_read_a();
    (* always_ready *) method Bit#(8)  psg_peek_reg(Bit#(4) idx);
    (* always_ready *) method Bit#(17) psg_peek_lfsr();
    method Bit#(8) kbd_peek_row(Bit#(4) row);

    // External Input Injection (Keyboard / Joystick)
    method Action kbd_key_down(Bit#(4) row, Bit#(3) col);
    method Action kbd_key_up  (Bit#(4) row, Bit#(3) col);
endinterface: TopMSXTurboR

(* synthesize *)
module mkTopMSXTurboR(TopMSXTurboR);

    // ── Submodules ──────────────────────────────────────────────────────────
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

    // Outbound Physical Memory Request FIFO
    FIFOF#(BusReq) q_out <- mkFIFOF;

    // Unified CPU Response Mux FIFO
    FIFOF#(BusResp) q_cpu_resp_mux <- mkFIFOF;

    // PSG Tick Divider: 16 Z80 T-states per /8 PSG generator tick
    Reg#(Bit#(4)) r_psg_div <- mkReg(0);

    // PCM Tick Accumulator: 228 Z80 T-states per 15.7 kHz PCM DAC tick
    Reg#(Bit#(8)) r_pcm_acc <- mkReg(0);

    // ── Clock & Tick Generators ─────────────────────────────────────────────
    // High-speed CPU & Video clocks
    rule step_clocks;
        if (clocks.tick_28m)  cpu.tick_28m;
        if (clocks.tick_7m)   cpu.tick_7m;
        if (clocks.tick_3m58) begin
            cpu.tick_3m58;
            vdp.tick_3m58;
        end
        if (clocks.tick_e6)   cpu.tick_e6;
    endrule

    // Audio tick generators (run synchronously on 3.58 MHz tick enable)
    rule step_audio (clocks.tick_3m58);
        scc.tick_scc;

        // PSG divider (/16)
        Bit#(4) nd = r_psg_div + 1;
        r_psg_div <= nd;
        if (nd == 0) psg.tick_psg;

        // PCM accumulator (228 T-states per 15.7 kHz sample)
        Bit#(8) na = r_pcm_acc + 1;
        if (na >= 228) begin
            r_pcm_acc <= na - 228;
            pcm.tick_pcm;
        end else begin
            r_pcm_acc <= na;
        end
    endrule

    // Keyboard matrix scanning (run synchronously on 3.58 MHz tick enable)
    rule sync_keyboard (clocks.tick_3m58);
        Bit#(4) row = ppi.kbd_row;
        ppi.set_kbd_data(kbd.kbd_row_bits(row));
    endrule

    // Synchronize DRAM shadow mode with CPU mode (R800 vs Z80A)
    rule sync_dram_mode;
        slot_mgr.set_dram_mode(cpu.is_r800_mode());
    endrule

    // ── CPU Bus Request Decoding & Routing ──────────────────────────────────
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
                    // Port A also writes Primary Slot latch in SlotManager
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
                // Forward unmapped I/O to external bus
                arbiter.cpu_req.put(req);
            end
        end else begin
            // Memory address translation: 16-bit virtual -> 24-bit physical
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

    // Forward external responses back to CPU response mux
    rule pump_arb_to_cpu;
        let resp <- arbiter.cpu_resp.get();
        q_cpu_resp_mux.enq(resp);
    endrule

    // Deliver responses to CPU
    rule pump_resp_to_cpu;
        let resp <- toGet(q_cpu_resp_mux).get();
        cpu.bus_resp.put(resp);
    endrule

    // ── VDP Memory Client Pipelines ─────────────────────────────────────────
    // Priority 1: VDP Scanout Rasterizer
    rule pump_vdp_scan_req;
        let req <- vdp.vdp_scan_req.get();
        arbiter.vdp_scan_req.put(req);
    endrule

    rule pump_vdp_scan_resp;
        let resp <- arbiter.vdp_scan_resp.get();
        vdp.vdp_scan_resp.put(resp);
    endrule

    // Priority 2: VDP Command Engine / Blitter
    rule pump_vdp_cmd_req;
        let req <- vdp.vdp_cmd_req.get();
        arbiter.vdp_cmd_req.put(req);
    endrule

    rule pump_vdp_cmd_resp;
        let resp <- arbiter.vdp_cmd_resp.get();
        vdp.vdp_cmd_resp.put(resp);
    endrule

    // Forward arbiter's outbound physical memory bus request
    rule pump_arb_out;
        let req <- arbiter.get_bus_req();
        q_out.enq(req);
    endrule

    // ── Digital Audio Mixer ─────────────────────────────────────────────────
    // PSG: unipolar 16-bit (0..32640)
    // SCC: signed 16-bit (-32768..32767)
    // PCM: unsigned 8-bit (0x80 = 0) -> level: ((sample - 128) << 6) -> 14-bit signed
    function Bit#(16) mixAudio();
        Bit#(16) psg_val = psg.pcm_sample;
        Int#(16) scc_val = unpack(scc.pcm_sample);
        Bit#(8)  pcm_val = pcm.pcm_sample;

        Int#(16) pcm_signed = (unpack(zeroExtend(pcm_val)) - 16'd128) << 6;
        Int#(16) psg_signed = unpack({1'b0, psg_val[15:1]}); // scale unipolar 16-bit to positive signed 16-bit

        Int#(32) sum = signExtend(psg_signed) + signExtend(scc_val) + signExtend(pcm_signed);

        if (sum > 32767)       sum = 32767;
        else if (sum < -32768) sum = -32768;
        return pack(truncate(sum));
    endfunction

    // ── Interface Methods ───────────────────────────────────────────────────
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

    // Video Output Signals
    method Bit#(16) video_rgb565() = vdp.pixel_rgb565();
    method Bool     video_hsync()  = vdp.hsync();
    method Bool     video_vsync()  = vdp.vsync();
    method Bool     video_blank()  = vdp.blank();
    method Bit#(9)  video_beam_x() = vdp.beam_x();
    method Bit#(8)  video_beam_y() = vdp.beam_y();
    method Bool     vdp_irq()      = vdp.irq();

    // Audio Output Signals
    method Bit#(16) audio_pcm_sample() = mixAudio();
    method Bit#(16) psg_raw_sample()   = psg.pcm_sample;
    method Bit#(16) scc_raw_sample()   = scc.pcm_sample;
    method Bit#(8)  pcm_raw_sample()   = pcm.pcm_sample;

    // LED Indicators (PPI port C bits & CPU mode)
    method Bool led_caps()  = (ppi.peek_pc[6] == 1'b0); // Active low on MSX
    method Bool led_kana()  = (ppi.peek_pc[7] == 1'b0);
    method Bool led_turbo() = cpu.is_r800_mode();

    // Peripheral Introspection
    method Bit#(8)  ppi_peek_pc()                 = ppi.peek_pc;
    method Bit#(8)  ppi_read_a()                  = ppi.read_ppi_a;
    method Bit#(8)  psg_peek_reg(Bit#(4) idx)     = psg.peek_reg(idx);
    method Bit#(17) psg_peek_lfsr()               = psg.peek_lfsr;
    method Bit#(8)  kbd_peek_row(Bit#(4) row)     = kbd.peek_row_bits(row);

    // Interactive Input Injection
    method Action kbd_key_down(Bit#(4) row, Bit#(3) col); kbd.kbd_down(row, col); endmethod
    method Action kbd_key_up  (Bit#(4) row, Bit#(3) col); kbd.kbd_up(row, col);   endmethod

endmodule: mkTopMSXTurboR

endpackage: TopMSXTurboR
