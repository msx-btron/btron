/*
 * V9958.bsv — Yamaha V9958 Video Display Processor (MSX2+ / MSX turbo R).
 *
 * Implements:
 *   - Ports 0x98..0x9B (and aliases 0x88..0x8B):
 *       Port 0x98: VRAM Read / Write Data with read-ahead prefetch buffer
 *       Port 0x99: Control Register Write / VRAM Address Setup / Status Read
 *       Port 0x9A: RGB565 Palette RAM 2-Byte Sequence
 *       Port 0x9B: Indirect Register R#17 Access & CPU Transfer Stream
 *   - S#0..S#9 status flags (ST_FG, ST_5S, ST_C, ST_TR, ST_VR, ST_HR, ST_CE)
 *   - Automatic interrupt generation:
 *       Frame IRQ: ST_FG set at end of display when R#1 IE0 = 1
 *       Line IRQ: FH set at line (R#19 + R#23) % 256 when R#0 IE1 = 1
 *   - Alex Wulms Command Engine integration (Blitter)
 *   - Video raster timing & beam coordinate tracking (Raster)
 *   - YJK / YAE color space conversion (YJK)
 *   - Dual client bus interfaces:
 *       vdp_scan_req / vdp_scan_resp (tagVDPScan, Priority 1)
 *       vdp_cmd_req  / vdp_cmd_resp  (tagVDPCmd, Round-Robin)
 *
 * References:
 *   src/emulators/msx/v9938.c
 *   src/emulators/msx/vdp.c
 */

package V9958;

import FIFOF::*;
import GetPut::*;
import Vector::*;

import Bus::*;
import VDPRegs::*;
import Raster::*;
import Blitter::*;
import YJK::*;

interface V9958;
    // Clock tick enables
    method Action tick_3m58();

    // CPU I/O Port Interface (Ports 0x98..0x9B, 0x88..0x8B)
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
    method Action                io_write(Bit#(8) port, Bit#(8) data);

    // Interrupt Line (Active High)
    (* always_ready *) method Bool irq();

    // Video Raster Output Signals
    (* always_ready *) method Bit#(9)  beam_x();
    (* always_ready *) method Bit#(8)  beam_y();
    (* always_ready *) method Bit#(9)  current_line();
    (* always_ready *) method Bool     hsync();
    (* always_ready *) method Bool     vsync();
    (* always_ready *) method Bool     blank();
    (* always_ready *) method Bit#(16) pixel_rgb565();

    // Memory Arbiter Client 1: Scanout Rasterizer (Strict Priority)
    interface Get#(BusReq)  vdp_scan_req;
    interface Put#(BusResp) vdp_scan_resp;

    // Memory Arbiter Client 2: Command Engine & CPU VRAM (Round-Robin)
    interface Get#(BusReq)  vdp_cmd_req;
    interface Put#(BusResp) vdp_cmd_resp;

    // Introspection Methods for Testbench & Co-Simulation
    method Bit#(8)  peek_reg(Bit#(6) idx);
    method Bit#(8)  peek_stat(Bit#(4) idx);
    method Bit#(16) peek_palette(Bit#(4) idx);
    method Bit#(18) peek_vram_ptr();
    method Bit#(8)  peek_read_ahead();
endinterface: V9958

(* synthesize *)
module mkV9958(V9958);
    // Submodules
    VDPRegs regs    <- mkVDPRegs;
    Raster  raster  <- mkRaster;
    Blitter blitter <- mkBlitter;

    // VRAM CPU Pointer & Latch State
    Reg#(Bit#(14)) vram_ptr      <- mkReg(0);
    Reg#(Bit#(8))  read_ahead    <- mkReg(8'hFF);
    Reg#(Bool)     write_mode    <- mkReg(True);
    Reg#(Bit#(8))  ctrl_latch    <- mkReg(0);
    Reg#(Bool)     ctrl_pending  <- mkReg(False);

    // Palette Programming Latches
    Reg#(Bit#(8))  pal_latch     <- mkReg(0);
    Reg#(Bool)     pal_pending   <- mkReg(False);

    // Interrupt Latch
    Reg#(Bool)     irq_pending   <- mkReg(False);

    // Blitter Parameter Shadow Registers (R#32..R#46)
    Reg#(Bit#(10)) blit_sx <- mkReg(0);
    Reg#(Bit#(10)) blit_sy <- mkReg(0);
    Reg#(Bit#(10)) blit_dx <- mkReg(0);
    Reg#(Bit#(10)) blit_dy <- mkReg(0);
    Reg#(Bit#(10)) blit_nx <- mkReg(0);
    Reg#(Bit#(10)) blit_ny <- mkReg(0);
    Reg#(Bit#(8))  blit_col<- mkReg(0);
    Reg#(Bit#(8))  blit_arg<- mkReg(0);

    // Arbiter Client Queues
    FIFOF#(BusReq)  q_scan_req  <- mkFIFOF;
    FIFOF#(BusResp) q_scan_resp <- mkFIFOF;
    FIFOF#(BusReq)  q_cmd_mux   <- mkFIFOF;
    FIFOF#(BusResp) q_cmd_resp  <- mkFIFOF;
    // Lockstep with q_cmd_mux. True = CPU VRAM access, False = blitter.
    // Responses are in order on this one arbiter client, and a CPU write
    // must not be delivered to the blitter: its response FIFO is only two
    // deep and is not drained while idle, which stalls every later OUT ($98).
    FIFOF#(Bool)    q_cmd_src   <- mkFIFOF;

    // CPU VRAM Write Queue (muxed with blitter into q_cmd_mux)
    FIFOF#(BusReq)  q_cpu_vram  <- mkFIFOF;

    // CPU I/O Write Queue
    FIFOF#(Tuple2#(Bit#(8), Bit#(8))) q_io_write <- mkFIFOF1;

    // Tick request register
    Reg#(Bool) tick_req <- mkReg(False);

    // Physical VRAM Base Address
    Bit#(24) vramBase = 24'h300000;

    // Screen 5 scanline. Filled from VRAM at the start of each line.
    // One buffer is enough: the fetch finishes before the beam crosses it.
    Reg#(Vector#(128, Bit#(8))) line_buf <- mkReg(replicate(0));
    Reg#(Bit#(8))  seen_t     <- mkReg(8'hFF);
    Reg#(Bool)     scanning   <- mkReg(False);
    Reg#(Bool)     scan_wait  <- mkReg(False);
    Reg#(Bit#(8))  scan_i     <- mkReg(0);
    Reg#(Bit#(18)) scan_addr  <- mkReg(0);

    // Full 18-bit CPU VRAM address: (R#14[3:0] << 14) | vram_ptr
    function Bit#(18) getCpuVramAddr();
        return {regs.vram_page_cpu(), vram_ptr};
    endfunction

    // CPU pointer auto-increment with screen mode wrapping
    function Action stepCpuPtr();
        action
            Bit#(5) m = regs.display_mode();
            Bit#(14) next_ptr = vram_ptr + 1;
            vram_ptr <= next_ptr;
            // Bitmap modes (Screen 5-8: M4 or M5 bit set) carry into R#14 on 16KB wrap
            if (next_ptr == 0 && ((m & 5'h18) != 0)) begin
                Bit#(4) next_page = regs.vram_page_cpu() + 1;
                regs.write_reg(6'd14, {4'b0, next_page});
            end
        endaction
    endfunction

    // ── Synchronization Rules ───────────────────────────────────────────────

    // Advance Raster on Tick Request
    rule do_tick (tick_req);
        tick_req <= False;
        raster.tick();
    endrule

    // Keep Raster configuration synced with VDP registers
    rule sync_raster_config;
        raster.set_config(
            regs.lines_212(),
            regs.pal_timing(),
            regs.irq_line(),
            regs.vscroll()
        );
    endrule

    // Handle VBlank (End of Active Display) Interrupt Event
    rule handle_vblank (raster.vblank_edge());
        regs.set_stat_bits(4'd0, stFG);
        regs.set_stat_bits(4'd2, stVR);
        if (regs.ie0_enabled()) begin
            irq_pending <= True;
        end
    endrule

    // Handle Line Interrupt Event
    rule handle_line_int (raster.line_int_edge());
        regs.set_stat_bits(4'd1, 8'h01); // FH flag
        if (regs.ie1_enabled()) begin
            irq_pending <= True;
        end
    endrule

    // Mux CPU VRAM accesses and Blitter requests into arbiter client 2.
    // The source tag is pushed in the same rule as the request so a response
    // can be steered back to the client that issued it.
    rule route_blitter_req;
        let req <- blitter.vram_req.get();
        q_cmd_mux.enq(req);
        q_cmd_src.enq(False);
    endrule

    rule route_cmd_resp;
        let from_cpu = q_cmd_src.first;
        let resp <- toGet(q_cmd_resp).get();
        q_cmd_src.deq;
        if (!from_cpu)
            blitter.vram_resp.put(resp);
    endrule

    // Screen 5 (mode 0x0C): 128 bytes per line, high nibble is the even pixel.
    // Page comes from R#2[6:5]. The fetch runs once per scanline and stays
    // ahead of the beam, which steps 168 times across 256 pixels.
    rule start_scanline (raster.current_t_line() == 0 && seen_t != 0 && !scan_wait);
        seen_t <= 0;
        if (regs.display_mode() == 5'b01100 && regs.display_enabled()) begin
            Bit#(2)  page = regs.read_reg(6'd2)[6:5];
            Bit#(8)  sy   = truncate(raster.current_line()) + regs.vscroll();
            scanning  <= True;
            scan_wait <= False;
            scan_i    <= 0;
            scan_addr <= {page, 16'd0} + zeroExtend(sy) * 18'd128;
        end else begin
            scanning <= False;
        end
    endrule

    rule note_scan_t (raster.current_t_line() != 0);
        seen_t <= raster.current_t_line();
    endrule

    rule issue_scan (scanning && !scan_wait);
        q_scan_req.enq(BusReq {
            tag:   tagVDPScan,
            addr:  vramBase + zeroExtend(scan_addr),
            data:  0,
            write: False,
            is_io: False
        });
        scan_wait <= True;
    endrule

    rule take_scan (scan_wait);
        let resp <- toGet(q_scan_resp).get();
        let next_line = line_buf;
        next_line[scan_i[6:0]] = resp.data;
        line_buf <= next_line;
        scan_addr <= scan_addr + 1;
        scan_i    <= scan_i + 1;
        scan_wait <= False;
        if (scan_i == 8'd127) scanning <= False;
    endrule

    // Update S#7 with POINT color when blitter completes a POINT command
    rule sync_point_color (regs.read_stat(4'd2)[0] == 1'b0); // when not busy
        regs.write_stat(4'd7, blitter.point_color());
    endrule

    // Unified register write handler (called by Port 0x99 and Port 0x9B)
    function Action handleRegWrite(Bit#(6) r, Bit#(8) val);
        action
            regs.write_reg(r, val);

            // If R#1 IE0 enabled while ST_FG is set, raise IRQ immediately
            if (r == 6'd1 && (val[5] == 1'b1) && ((regs.read_stat(4'd0) & stFG) != 0)) begin
                irq_pending <= True;
            end

            // R#16 reset palette write latch
            if (r == 6'd16) begin
                pal_pending <= False;
            end

            // Blitter indirect register tracking (R#32..R#46)
            if (r >= 6'd32 && r <= 6'd46) begin
                case (r - 6'd32)
                    6'd0:  blit_sx <= {blit_sx[9:8], val};
                    6'd1:  blit_sx <= {1'b0, val[0], blit_sx[7:0]};
                    6'd2:  blit_sy <= {blit_sy[9:8], val};
                    6'd3:  blit_sy <= {val[1:0], blit_sy[7:0]};
                    6'd4:  blit_dx <= {blit_dx[9:8], val};
                    6'd5:  blit_dx <= {1'b0, val[0], blit_dx[7:0]};
                    6'd6:  blit_dy <= {blit_dy[9:8], val};
                    6'd7:  blit_dy <= {val[1:0], blit_dy[7:0]};
                    6'd8:  blit_nx <= {blit_nx[9:8], val};
                    6'd9:  blit_nx <= {val[1:0], blit_nx[7:0]};
                    6'd10: blit_ny <= {blit_ny[9:8], val};
                    6'd11: blit_ny <= {val[1:0], blit_ny[7:0]};
                    6'd12: begin
                        blit_col <= val;
                        blitter.cpu_transfer_write(val);
                    end
                    6'd13: blit_arg <= val;
                    6'd14: begin
                        blitter.set_params(
                            blit_sx, blit_sy,
                            blit_dx, blit_dy,
                            blit_nx, blit_ny,
                            blit_col, blit_arg, val,
                            regs.display_mode()
                        );
                    end
                endcase
            end
        endaction
    endfunction

    // Process I/O Writes from CPU
    rule do_io_write (q_io_write.notEmpty);
        match {.port, .data} = q_io_write.first;
        q_io_write.deq;

        Bit#(2) idx = port[1:0];

        case (idx)
            2'd0: begin
                // Port 0x98: VRAM Data Write
                ctrl_pending <= False;
                if (write_mode) begin
                    Bit#(18) a = getCpuVramAddr();
                    q_cmd_mux.enq(BusReq {
                        tag:   tagVDPCmd,
                        addr:  vramBase + zeroExtend(a),
                        data:  data,
                        write: True,
                        is_io: False
                    });
                    q_cmd_src.enq(True);
                    stepCpuPtr();
                end
            end

            2'd1: begin
                // Port 0x99: Control / Register Write / VRAM Address Setup
                if (ctrl_pending) begin
                    ctrl_pending <= False;
                    if (data[7] == 1'b1) begin
                        handleRegWrite(data[5:0], ctrl_latch);
                    end else begin
                        // VRAM address set: {data[5:0], ctrl_latch}
                        write_mode <= (data[6] == 1'b1);
                        vram_ptr   <= {data[5:0], ctrl_latch};
                    end
                end else begin
                    ctrl_latch   <= data;
                    ctrl_pending <= True;
                end
            end

            2'd2: begin
                // Port 0x9A: Palette Write (2-byte sequence)
                if (pal_pending) begin
                    pal_pending <= False;
                    Bit#(4) pidx = regs.read_reg(6'd16)[3:0];
                    Bit#(3) r3   = pal_latch[6:4];
                    Bit#(3) b3   = pal_latch[2:0];
                    Bit#(3) g3   = data[2:0];

                    // Scale 3-bit (0..7) to 5-bit (0..31) in 16-bit precision to avoid overflow
                    Bit#(16) r16 = (zeroExtend(r3) * 16'd31) / 16'd7;
                    Bit#(16) g16 = (zeroExtend(g3) * 16'd31) / 16'd7;
                    Bit#(16) b16 = (zeroExtend(b3) * 16'd31) / 16'd7;

                    Bit#(16) c565 = rgb555ToRgb565(truncate(r16), truncate(g16), truncate(b16));
                    regs.write_palette(pidx, c565);
                    regs.write_reg(6'd16, {4'b0, (pidx + 1)});
                end else begin
                    pal_latch   <= data;
                    pal_pending <= True;
                end
            end

            2'd3: begin
                // Port 0x9B: Indirect Register Write via R#17
                Bit#(8) r17 = regs.read_reg(6'd17);
                Bit#(6) targetReg = r17[5:0];

                if (targetReg == 6'd17) begin
                    regs.write_reg(6'd17, data);
                end else begin
                    handleRegWrite(targetReg, data);
                    if (r17[7] == 1'b0) begin
                        regs.write_reg(6'd17, {2'b0, targetReg + 1});
                    end
                end
            end
        endcase
    endrule

    // ── Clock Advance ───────────────────────────────────────────────────────

    method Action tick_3m58();
        tick_req <= True;
    endmethod

    // ── CPU I/O Port Interface ──────────────────────────────────────────────

    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
        Bit#(2) idx = port[1:0];
        ctrl_pending <= False;

        if (idx == 2'd0) begin
            // Port 0x98: VRAM Data Read
            Bit#(8) res = read_ahead;
            if (!write_mode) begin
                // In hardware, reading returns prefetched byte and queues next prefetch
                stepCpuPtr();
            end
            return res;
        end else if (idx == 2'd1) begin
            // Port 0x99: Status Register Read
            Bit#(4) sreg = regs.read_reg(6'd15)[3:0];
            Bit#(8) statVal = regs.read_stat(sreg);

            if (sreg == 4'd0) begin
                // Reading S#0 clears ST_FG and frame interrupt
                regs.clear_stat_bits(4'd0, stFG);
                irq_pending <= False;
            end else if (sreg == 4'd1) begin
                // Reading S#1 clears FH (line interrupt)
                regs.clear_stat_bits(4'd1, 8'h01);
            end else if (sreg == 4'd2) begin
                // S#2 dynamic status flags: TR, VR, HR, BD, 1, 1, EO, CE
                Bit#(8) dynS2 = 8'h0C;
                if (blitter.is_tr())   dynS2 = dynS2 | stTR;
                if (raster.is_vr())    dynS2 = dynS2 | stVR;
                if (raster.is_hr())    dynS2 = dynS2 | stHR;
                if (blitter.is_busy()) dynS2 = dynS2 | stCE;
                statVal = dynS2;
            end
            return statVal;
        end else begin
            return 8'hFF;
        end
    endmethod

    method Action io_write(Bit#(8) port, Bit#(8) data);
        q_io_write.enq(tuple2(port, data));
    endmethod

    // ── External Interfaces ─────────────────────────────────────────────────

    method Bool irq() = irq_pending;

    method Bit#(9)  beam_x()       = raster.beam_x();
    method Bit#(8)  beam_y()       = raster.beam_y();
    method Bit#(9)  current_line() = raster.current_line();
    method Bool     hsync()        = raster.hsync();
    method Bool     vsync()        = raster.vsync();
    method Bool     blank()        = raster.blank();

    // Default active pixel: borders use border_color, active display uses palette
    method Bit#(16) pixel_rgb565();
        if (raster.blank()) begin
            return 16'h0000;
        end else if (!regs.display_enabled()) begin
            return regs.read_palette(regs.border_color()[3:0]);
        end else if (regs.display_mode() == 5'b01100) begin
            // Screen 5: two 4-bit pixels per byte. Even x is the high nibble.
            Bit#(8) x   = truncate(raster.beam_x());
            Bit#(8) b   = line_buf[x[7:1]];
            Bit#(4) pix = (x[0] == 1'b0) ? b[7:4] : b[3:0];
            return regs.read_palette(pix);
        end else begin
            return regs.read_palette(regs.read_reg(6'd7)[7:4]);
        end
    endmethod

    interface Get vdp_scan_req  = toGet(q_scan_req);
    interface Put vdp_scan_resp = toPut(q_scan_resp);

    interface Get vdp_cmd_req   = toGet(q_cmd_mux);
    interface Put vdp_cmd_resp  = toPut(q_cmd_resp);

    // Introspection Methods
    method Bit#(8)  peek_reg(Bit#(6) idx)      = regs.read_reg(idx);
    method Bit#(8)  peek_stat(Bit#(4) idx)     = regs.read_stat(idx);
    method Bit#(16) peek_palette(Bit#(4) idx)  = regs.read_palette(idx);
    method Bit#(18) peek_vram_ptr()            = getCpuVramAddr();
    method Bit#(8)  peek_read_ahead()          = read_ahead;
endmodule: mkV9958

endpackage: V9958
