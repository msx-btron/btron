/*
 * V9958.bsv — Yamaha V9958 Video Display Processor (MSX2+ / MSX turbo R).
 *
 * Implements:
 *   - Ports 0x98..0x9C (and aliases 0x88..0x8C):
 *       Port 0x98: VRAM Read / Write Data with read-ahead prefetch buffer
 *       Port 0x99: Control Register Write / VRAM Address Setup / Status Read
 *       Port 0x9A: RGB565 Palette RAM 2-Byte Sequence
 *       Port 0x9B: Indirect Register R#17 Access & CPU Transfer Stream
 *       Port 0x9C: V9968 port 4. Frame, line, and command-end flags,
 *                  write-1-to-clear. Bit 7 locks R#20/R#21 until a write of 00h.
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

import ConfigReg::*;
import FIFOF::*;
import GetPut::*;
import RegFile::*;
import RWire::*;
import Vector::*;

import Bus::*;
import VDPRegs::*;
import Raster::*;
import Blitter::*;
import YJK::*;

// One scanline of screen 1 or screen 2 is three bursts of 32 bytes:
// the name row, then the pattern bytes those names select, then the
// color bytes. Screen 0 is 40 names and the 40 pattern rows they
// select. Those pattern bytes land in line_buf[128..167]. Screen 5
// is a flat 128-byte bitmap burst. Screen 8 is a flat 256-byte burst,
// one byte per pixel.
typedef enum {
    ScanIdle,
    ScanS5,
    ScanS8,
    ScanG1Name, ScanG1Pat, ScanG1Col,
    ScanG2Name, ScanG2Pat, ScanG2Col,
    ScanT0Name, ScanT0Pat
} ScanKind deriving (Bits, Eq);

// V9938 screen 8 byte: bits 7:5 green, 4:2 red, 1:0 blue.
function Bit#(16) rgb332_to_rgb565(Bit#(8) c);
    Bit#(16) g8 = zeroExtend(c[7:5]) * 16'd255 / 16'd7;
    Bit#(16) r8 = zeroExtend(c[4:2]) * 16'd255 / 16'd7;
    Bit#(16) b8 = zeroExtend(c[1:0]) * 16'd255 / 16'd3;
    return {r8[7:3], g8[7:2], b8[7:3]};
endfunction

interface V9958;
    // Clock tick enables
    method Action tick_3m58();

    // CPU I/O Port Interface (Ports 0x98..0x9C, 0x88..0x8C)
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
    method Action                io_write(Bit#(8) port, Bit#(8) data);

    // Interrupt Line (Active High)
    (* always_ready *) method Bool irq();
    // One cycle at the end of the active display. The CPU uses it to
    // accept the next frame interrupt while FH is still set.
    (* always_ready *) method Bool vblank_edge();

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
    method Bit#(16) peek_pal256(Bit#(8) idx);
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
    Reg#(Bit#(8))  pal_g         <- mkReg(0);
    Reg#(Bit#(2))  pal_sub       <- mkReg(0);
    Reg#(Bool)     pal_pending   <- mkReg(False);
    // V9968 extended palette. Screen 8 indexes it when R#20 bit 4 is set.
    RegFile#(Bit#(8), Bit#(16)) pal256 <- mkRegFileFull;

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

    // V9968 port 4. Bits 2:0 are frame, line, and command-end, write-1-to-clear.
    // Bit 7 starts set. A write of 00h clears it and lets R#20/R#21 through.
    // Neon polls bit 0 from its IM 2 handler and acks it with OUT (9Ch), 1.
    RWire#(Bit#(8)) p4_wr      <- mkRWire;
    Reg#(Bit#(3))   p4_flags   <- mkConfigReg(0);
    Reg#(Bool)      p4_lock    <- mkConfigReg(True);
    Reg#(Bool)      ce_was     <- mkReg(False);
    // One-cycle copies. The flag rule has to run after do_io_write so it
    // sees the ack. Reading the raster or the blitter in that same rule
    // closes a schedule cycle and Bluespec then drops every VDP write.
    Reg#(Bool)      frame_edge <- mkConfigReg(False);
    Reg#(Bool)      line_edge  <- mkConfigReg(False);
    Reg#(Bool)      ce_edge    <- mkConfigReg(False);

    // Physical VRAM Base Address
    Bit#(24) vramBase = 24'h300000;

    // Screen 5 scanline. Filled from VRAM at the start of each line.
    // One buffer is enough: the fetch finishes before the beam crosses it.
    Reg#(Vector#(256, Bit#(8))) line_buf <- mkReg(replicate(0));
    // Screen 1 / 2: 32 name bytes, then the pattern and color they select.
    Reg#(Vector#(32, Bit#(8))) name_buf <- mkReg(replicate(0));
    Reg#(Vector#(32, Bit#(8))) pat_buf  <- mkReg(replicate(0));
    Reg#(Vector#(32, Bit#(8))) col_buf  <- mkReg(replicate(0));
    Reg#(Bit#(8))  seen_t     <- mkReg(8'hFF);
    Reg#(Bool)     scanning   <- mkReg(False);
    Reg#(Bool)     scan_wait  <- mkReg(False);
    Reg#(Bit#(8))  scan_i     <- mkReg(0);
    Reg#(Bit#(18)) scan_addr  <- mkReg(0);
    Reg#(ScanKind) scan_kind  <- mkReg(ScanIdle);
    Reg#(Bit#(8))  tile_y     <- mkReg(0);

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
    endrule

    // Handle Line Interrupt Event
    rule handle_line_int (raster.line_int_edge());
        regs.set_stat_bits(4'd1, 8'h01); // FH flag
    endrule

    rule note_port4_frame;
        frame_edge <= raster.vblank_edge();
    endrule

    rule note_port4_line;
        line_edge <= raster.line_int_edge();
    endrule

    // Falling CE, sampled before do_io_write touches the blitter.
    rule note_ce_fall;
        Bool busy = blitter.is_busy();
        ce_edge <= ce_was && !busy;
        ce_was <= busy;
    endrule

    // Port 4 flags follow S#0 FG, S#1 FH, and S#2 CE, one cycle later.
    // The ack is write-1-to-clear of the byte do_io_write just stored.
    rule update_port4;
        Bit#(3) f = p4_flags;
        if (frame_edge)
            f = f | 3'b001;
        if (line_edge)
            f = f | 3'b010;
        if (ce_edge)
            f = f | 3'b100;
        if (p4_wr.wget() matches tagged Valid .d) begin
            f = f & ~d[2:0];
            if (d == 8'h00)
                p4_lock <= False;
        end
        p4_flags <= f;
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
    //
    // Screen 2 (mode 0x04, M3) and screen 1 (mode 0x00) share the 32-cell
    // line. Addresses follow v9938.c: screen 2 pattern base is only R#4[2],
    // and its color base is only R#3[7], so the VDPRegs table helpers (which
    // are the bitmap / MSX2 forms) are not used here.
    function Bit#(18) tile_scan_addr(ScanKind kind);
        Bit#(8) r2 = regs.read_reg(6'd2);
        Bit#(8) r3 = regs.read_reg(6'd3);
        Bit#(8) r4 = regs.read_reg(6'd4);
        Bit#(5) tile_i = scan_i[4:0];
        Bit#(8) name = name_buf[tile_i];
        Bit#(3) row = tile_y[2:0];
        Bit#(2) chunk = tile_y[7:6];
        Bit#(18) nt = {1'b0, r2[6:0], 10'd0};
        Bit#(10) name_off = {tile_y[7:3], tile_i};
        Bit#(13) g2_gen = {chunk & r4[1:0], name, row};
        Bit#(14) g2_pt = {r4[2], 13'd0};
        Bit#(14) g2_ct = {r3[7], 13'd0};
        Bit#(13) g2_mask = {r3[6:0], 6'b111111};
        Bit#(13) g2_col = {chunk, name, row} & g2_mask;
        Bit#(17) g1_pt = {r4[5:0], 11'd0};
        Bit#(11) g1_poff = {name, row};
        Bit#(14) g1_ct = {r3, 6'd0};
        // Screen 0 (v9938.c): name base is R#2[6:0] << 10, 40 columns.
        // Pattern base is R#4[5:0] << 11. The glyph was stored in
        // line_buf by ScanT0Name. No vertical scroll on this mode.
        Bit#(13) t0_band = zeroExtend(tile_y[7:3]);
        Bit#(13) t0_col  = zeroExtend(scan_i[5:0]);
        Bit#(13) t0_off  = t0_band * 13'd40 + t0_col;
        Bit#(18) t0_nt   = {1'b0, r2[6:0], 10'd0} + zeroExtend(t0_off);
        Bit#(8)  t0_glyph = line_buf[scan_i];
        Bit#(11) t0_idx  = {t0_glyph, tile_y[2:0]};
        Bit#(18) t0_pt   = (zeroExtend(r4[5:0]) << 11) + zeroExtend(t0_idx);
        return case (kind)
            ScanG1Name: nt + zeroExtend(name_off);
            ScanG2Name: nt + zeroExtend(name_off);
            ScanG2Pat:  zeroExtend(g2_pt) + zeroExtend(g2_gen);
            ScanG2Col:  zeroExtend(g2_ct) + zeroExtend(g2_col);
            ScanG1Pat:  zeroExtend(g1_pt) + zeroExtend(g1_poff);
            ScanG1Col:  zeroExtend(g1_ct) + zeroExtend(name[7:3]);
            ScanT0Name: t0_nt;
            ScanT0Pat:  t0_pt;
            default:    18'd0;
        endcase;
    endfunction

    rule start_scanline (raster.current_t_line() == 0 && seen_t != 0 && !scan_wait);
        seen_t <= 0;
        Bit#(5) mode = regs.display_mode();
        Bit#(8) sy = truncate(raster.current_line()) + regs.vscroll();
        if (regs.display_enabled() && mode == 5'b01100) begin
            // Screen 5 (G4): four 32 KiB pages. R#2[6:5] selects the page.
            Bit#(2) page = regs.read_reg(6'd2)[6:5];
            scanning  <= True;
            scan_wait <= False;
            scan_i    <= 0;
            scan_kind <= ScanS5;
            scan_addr <= {1'b0, page, 15'd0} + zeroExtend(sy) * 18'd128;
        end else if (regs.display_enabled() && mode == 5'b11100) begin
            // Screen 8 (G7): 256 bytes/line, RGB GGGRRRBB. R#2 bit 5 is A16.
            Bit#(1) page = regs.read_reg(6'd2)[5];
            scanning  <= True;
            scan_wait <= False;
            scan_i    <= 0;
            scan_kind <= ScanS8;
            scan_addr <= {page, 17'd0} + zeroExtend(sy) * 18'd256;
        end else if (regs.display_enabled() && mode == 5'b10100) begin
            // Screen 7 (G6): 256 bytes/line, 4bpp. R#2 bit 5 is A16, same as screen 8.
            Bit#(1) page = regs.read_reg(6'd2)[5];
            scanning  <= True;
            scan_wait <= False;
            scan_i    <= 0;
            scan_kind <= ScanS8;
            scan_addr <= {1'b0, page, 16'd0} + zeroExtend(sy) * 18'd256;
        end else if (regs.display_enabled() && (mode == 5'b00100 || mode == 5'b00000)) begin
            scanning  <= True;
            scan_wait <= False;
            scan_i    <= 0;
            scan_kind <= (mode == 5'b00100) ? ScanG2Name : ScanG1Name;
            tile_y    <= sy;
        end else if (regs.display_enabled() && mode == 5'b00001) begin
            // Screen 0. v9938.c does not add R#23 on this mode.
            scanning  <= True;
            scan_wait <= False;
            scan_i    <= 0;
            scan_kind <= ScanT0Name;
            tile_y    <= truncate(raster.current_line());
        end else begin
            scanning  <= False;
            scan_kind <= ScanIdle;
        end
    endrule

    rule note_scan_t (raster.current_t_line() != 0);
        seen_t <= raster.current_t_line();
    endrule

    rule issue_scan (scanning && !scan_wait);
        Bit#(18) addr = (scan_kind == ScanS5 || scan_kind == ScanS8) ? scan_addr : tile_scan_addr(scan_kind);
        q_scan_req.enq(BusReq {
            tag:   tagVDPScan,
            addr:  vramBase + zeroExtend(addr),
            data:  0,
            write: False,
            is_io: False
        });
        scan_wait <= True;
    endrule

    rule take_scan (scan_wait);
        let resp <- toGet(q_scan_resp).get();
        scan_wait <= False;
        if (scan_kind == ScanS5 || scan_kind == ScanS8) begin
            let next_line = line_buf;
            next_line[scan_i] = resp.data;
            line_buf <= next_line;
            scan_addr <= scan_addr + 1;
            Bit#(8) last = (scan_kind == ScanS8) ? 8'd255 : 8'd127;
            if (scan_i == last) begin
                scanning <= False;
                scan_kind <= ScanIdle;
            end else begin
                scan_i <= scan_i + 1;
            end
        end else if (scan_kind == ScanG1Name || scan_kind == ScanG2Name) begin
            let nb = name_buf;
            nb[scan_i[4:0]] = resp.data;
            name_buf <= nb;
            if (scan_i == 8'd31) begin
                scan_i <= 0;
                scan_kind <= (scan_kind == ScanG2Name) ? ScanG2Pat : ScanG1Pat;
            end else begin
                scan_i <= scan_i + 1;
            end
        end else if (scan_kind == ScanG1Pat || scan_kind == ScanG2Pat) begin
            let pb = pat_buf;
            pb[scan_i[4:0]] = resp.data;
            pat_buf <= pb;
            if (scan_i == 8'd31) begin
                scan_i <= 0;
                scan_kind <= (scan_kind == ScanG2Pat) ? ScanG2Col : ScanG1Col;
            end else begin
                scan_i <= scan_i + 1;
            end
        end else if (scan_kind == ScanT0Name || scan_kind == ScanT0Pat) begin
            // Names occupy line_buf[0..39]. Pattern rows occupy [128..167].
            let nb = line_buf;
            if (scan_kind == ScanT0Name)
                nb[scan_i] = resp.data;
            else
                nb[8'd128 + scan_i] = resp.data;
            line_buf <= nb;
            if (scan_i == 8'd39) begin
                scan_i <= 0;
                if (scan_kind == ScanT0Name)
                    scan_kind <= ScanT0Pat;
                else begin
                    scanning <= False;
                    scan_kind <= ScanIdle;
                end
            end else begin
                scan_i <= scan_i + 1;
            end
        end else begin
            let cb = col_buf;
            cb[scan_i[4:0]] = resp.data;
            col_buf <= cb;
            if (scan_i == 8'd31) begin
                scanning <= False;
                scan_kind <= ScanIdle;
            end else begin
                scan_i <= scan_i + 1;
            end
        end
    endrule

    // Update S#7 with POINT color when blitter completes a POINT command
    rule sync_point_color (regs.read_stat(4'd2)[0] == 1'b0); // when not busy
        regs.write_stat(4'd7, blitter.point_color());
    endrule

    // Unified register write handler (called by Port 0x99 and Port 0x9B)
    function Action handleRegWrite(Bit#(6) r, Bit#(8) val);
        action
            // Generation C ignores R#20 and R#21 while port 4 bit 7 is set.
            // Neon writes 00h to port 9Ch before the register table.
            if (!(p4_lock && (r == 6'd20 || r == 6'd21)))
                regs.write_reg(r, val);

            // R#16 reset palette write latch
            if (r == 6'd16) begin
                pal_pending <= False;
                pal_sub <= 0;
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
                            regs.display_mode(),
                            regs.read_reg(6'd21)[0] == 1'b0
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

        Bit#(3) idx = port[2:0];

        case (idx)
            3'd0: begin
                // Port 0x98: VRAM Data Write
                ctrl_pending <= False;
                if (write_mode) begin
                    // R#21 bit 0 = 0 keeps A17. Otherwise the V9958 wraps at 128 KiB.
                    Bit#(18) a = getCpuVramAddr();
                    if (regs.read_reg(6'd21)[0] == 1'b1)
                        a = a & 18'h1FFFF;
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

            3'd1: begin
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

            3'd2: begin
                // Port 0x9A. R#20 bit 4 selects the V9968 256-color palette
                // (three bytes, R then G then B, each 0..31). Otherwise the
                // V9938 16-color palette is a two-byte RGB333 pair.
                if (regs.read_reg(6'd20)[4] == 1'b1) begin
                    Bit#(8) pidx = regs.read_reg(6'd16);
                    if (pal_sub == 0) begin
                        pal_latch <= data;
                        pal_sub <= 1;
                    end else if (pal_sub == 1) begin
                        pal_g <= data;
                        pal_sub <= 2;
                    end else begin
                        Bit#(5) r5 = pal_latch[4:0];
                        Bit#(5) g5 = pal_g[4:0];
                        Bit#(5) b5 = data[4:0];
                        Bit#(16) c565 = rgb555ToRgb565(r5, g5, b5);
                        pal256.upd(pidx, c565);
                        if (pidx < 16)
                            regs.write_palette(truncate(pidx), c565);
                        regs.write_reg(6'd16, pidx + 1);
                        pal_sub <= 0;
                    end
                end else if (pal_pending) begin
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

            3'd3: begin
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

            3'd4: begin
                // Port 0x9C / 0x8C. Write-1-to-clear of the three flags.
                // The value 00h is also the generation-C unlock.
                p4_wr.wset(data);
            end

            default: noAction;
        endcase
    endrule

    // ── Clock Advance ───────────────────────────────────────────────────────

    method Action tick_3m58();
        tick_req <= True;
    endmethod

    // ── CPU I/O Port Interface ──────────────────────────────────────────────

    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
        Bit#(3) idx = port[2:0];
        ctrl_pending <= False;

        if (idx == 3'd0) begin
            // Port 0x98: VRAM Data Read
            Bit#(8) res = read_ahead;
            if (!write_mode) begin
                // In hardware, reading returns prefetched byte and queues next prefetch
                stepCpuPtr();
            end
            return res;
        end else if (idx == 3'd1) begin
            // Port 0x99: Status Register Read
            Bit#(4) sreg = regs.read_reg(6'd15)[3:0];
            Bit#(8) statVal = regs.read_stat(sreg);

            if (sreg == 4'd0) begin
                // Reading S#0 clears ST_FG. The pin drops because irq()
                // follows the flag, not a latch that outlives the read.
                regs.clear_stat_bits(4'd0, stFG);
            end else if (sreg == 4'd1) begin
                // Reading S#1 clears FH (line interrupt).
                // Bits 5:1 are the ID. R#21 bit 0 = 0 reports V9968 (3).
                Bit#(8) id = (regs.read_reg(6'd21)[0] == 1'b0) ? 8'h06 : 8'h04;
                statVal = (statVal & 8'hC1) | id;
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
        end else if (idx == 3'd4) begin
            // Port 0x9C / 0x8C. Bit 0 is the frame flag Neon tests.
            Bit#(8) p4 = zeroExtend(p4_flags);
            if (p4_lock)
                p4 = p4 | 8'h80;
            return p4;
        end else begin
            return 8'hFF;
        end
    endmethod

    method Action io_write(Bit#(8) port, Bit#(8) data);
        q_io_write.enq(tuple2(port, data));
    endmethod

    // ── External Interfaces ─────────────────────────────────────────────────

    // The pin is the enable and the flag. Turning IE0 off drops the
    // request even when FH is still set. A sticky latch here retriggers
    // keyint forever once R#15 selects a status other than S#0.
    method Bool irq();
        Bool frame_irq = regs.ie0_enabled() && ((regs.read_stat(4'd0) & stFG) != 0);
        Bool line_irq  = regs.ie1_enabled() && ((regs.read_stat(4'd1) & 8'h01) != 0);
        return frame_irq || line_irq;
    endmethod

    method Bool vblank_edge() = raster.vblank_edge();

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
            // Nibble 0 is a real color on a bitmap plane.
            Bit#(8) x   = truncate(raster.beam_x());
            Bit#(8) bx  = {1'b0, x[7:1]};
            Bit#(8) b   = line_buf[bx];
            Bit#(4) pix = (x[0] == 1'b0) ? b[7:4] : b[3:0];
            return regs.read_palette(pix);
        end else if (regs.display_mode() == 5'b00100 || regs.display_mode() == 5'b00000) begin
            // Screen 2 and screen 1: pattern bit selects the color nibble.
            // A zero nibble is transparent and shows the border.
            Bit#(8) x = truncate(raster.beam_x());
            Bit#(8) pat = pat_buf[x[7:3]];
            Bit#(8) cb  = col_buf[x[7:3]];
            Bool on = pat[3'd7 - x[2:0]] == 1'b1;
            Bit#(4) c = on ? cb[7:4] : cb[3:0];
            if (c == 4'd0)
                return regs.read_palette(regs.border_color()[3:0]);
            else
                return regs.read_palette(c);
        end else if (regs.display_mode() == 5'b11100) begin
            // Screen 8. R#20 bit 4 is the V9968 palette; otherwise GGGRRRBB.
            Bit#(8) x = truncate(raster.beam_x());
            Bit#(8) c = line_buf[x];
            if (regs.read_reg(6'd20)[4] == 1'b1)
                return pal256.sub(c);
            else
                return rgb332_to_rgb565(c);
        end else if (regs.display_mode() == 5'b10100) begin
            // Screen 7 scaled to 256: the high nibble, unless it equals
            // R#7[3:0], in which case the low nibble. Same rule as v9938.c.
            Bit#(8) x  = truncate(raster.beam_x());
            Bit#(8) b  = line_buf[x];
            Bit#(4) bg = regs.read_reg(6'd7)[3:0];
            Bit#(4) p0 = b[7:4];
            Bit#(4) pix = (p0 != bg) ? p0 : b[3:0];
            return regs.read_palette(pix);
        end else if (regs.display_mode() == 5'b00001) begin
            // Screen 0. 40 glyphs of 6 pixels, inset by 8. Ink is R#7[7:4].
            // A clear bit is the border, which is R#7[3:0]. v9938.c.
            Bit#(8) x = truncate(raster.beam_x());
            Bit#(4) bg = regs.border_color()[3:0];
            Bit#(4) fg = regs.read_reg(6'd7)[7:4];
            if (x < 8 || x >= 248) begin
                return regs.read_palette(bg);
            end else begin
                Bit#(8) tx = x - 8;
                Bit#(8) col = tx / 6;
                Bit#(3) bitn = truncate(tx % 6);
                Bit#(8) pat = line_buf[8'd128 + col];
                Bool on = pat[3'd7 - bitn] == 1'b1;
                return regs.read_palette(on ? fg : bg);
            end
        end else begin
            // Screen 3, 6 and anything else: one flat color, R#7[7:4].
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
    method Bit#(16) peek_pal256(Bit#(8) idx)   = pal256.sub(idx);
    method Bit#(18) peek_vram_ptr()            = getCpuVramAddr();
    method Bit#(8)  peek_read_ahead()          = read_ahead;
endmodule: mkV9958

endpackage: V9958
