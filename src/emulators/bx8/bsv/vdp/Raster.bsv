/*
 * Raster.bsv — Yamaha V9938 / V9958 Video Raster Timing Generator.
 *
 * Implements:
 *   - 228 CPU cycle / 3648 master cycle horizontal scanline timer
 *   - 262-line (NTSC) / 313-line (PAL) vertical frame counters
 *   - 192 / 212 active display line switching (R#9 LN)
 *   - Frame interrupt strobe (FG / VBlank) at display boundary
 *   - Programmable line interrupt comparator (R#19 + R#23)
 *   - Real-time beam coordinates (beam_x, beam_y, HR, VR)
 *
 * References:
 *   src/emulators/msx/v9938.c
 *   src/emulators/msx/vdp.c
 */

package Raster;

interface Raster;
    // Clock advance enable (invoked on tick_3m58)
    method Action tick();

    // Configuration from VDP registers
    method Action set_config(Bool lines212, Bool palMode, Bit#(8) lineIrq, Bit#(8) vOffset);

    // Raster Position
    (* always_ready *) method Bit#(9) current_line();
    (* always_ready *) method Bit#(8) current_t_line();
    (* always_ready *) method Bit#(9) beam_x();
    (* always_ready *) method Bit#(8) beam_y();

    // Status Flags
    (* always_ready *) method Bool is_active_display();
    (* always_ready *) method Bool is_hr(); // Horizontal Retrace (t_line >= 168)
    (* always_ready *) method Bool is_vr(); // Vertical Retrace (line >= disp_end)
    (* always_ready *) method Bool vblank_edge(); // Single-cycle strobe at end of active display
    (* always_ready *) method Bool line_int_edge(); // Single-cycle strobe at interrupt line

    // Physical Video Sync Signals
    (* always_ready *) method Bool hsync();
    (* always_ready *) method Bool vsync();
    (* always_ready *) method Bool blank();
endinterface: Raster

(* synthesize *)
module mkRaster(Raster);
    Reg#(Bit#(8)) t_line      <- mkReg(0);
    Reg#(Bit#(9)) line        <- mkReg(0);

    Reg#(Bool)    reg_ln212   <- mkReg(False);
    Reg#(Bool)    reg_pal     <- mkReg(False);
    Reg#(Bit#(8)) reg_irqline <- mkReg(0);
    Reg#(Bit#(8)) reg_voffset <- mkReg(0);

    Reg#(Bool)    pulse_vblank   <- mkReg(False);
    Reg#(Bool)    pulse_line_int <- mkReg(False);

    // Timing constants
    Bit#(9) max_lines = reg_pal ? 9'd313 : 9'd262;
    Bit#(9) disp_end  = reg_ln212 ? 9'd212 : 9'd192;

    // Line interrupt comparison line: (R#19 + R#23) % 256
    Bit#(9) eff_irq_line = {1'b0, (reg_irqline + reg_voffset)};

    method Action set_config(Bool lines212, Bool palMode, Bit#(8) lineIrq, Bit#(8) vOffset);
        reg_ln212   <= lines212;
        reg_pal     <= palMode;
        reg_irqline <= lineIrq;
        reg_voffset <= vOffset;
    endmethod

    method Action tick();
        Bit#(9) next_line = (line + 1 >= max_lines) ? 9'd0 : line + 1;
        Bool is_end_of_line = (t_line >= 8'd227);

        pulse_vblank   <= is_end_of_line && (next_line == disp_end);
        pulse_line_int <= is_end_of_line && (next_line == eff_irq_line);

        if (is_end_of_line) begin
            t_line <= 0;
            line   <= next_line;
        end else begin
            t_line <= t_line + 1;
        end
    endmethod

    method Bit#(9) current_line()   = line;
    method Bit#(8) current_t_line() = t_line;

    // Active display area: line < disp_end and t_line in active horizontal interval
    // Standard MSX active pixel display corresponds to t_line < 168 (256 pixels)
    method Bool is_active_display() = (line < disp_end) && (t_line < 8'd168);

    method Bit#(9) beam_x();
        // 256 pixels mapped across the 168 active cycles
        // Each cycle ~ 1.5 pixels (or direct t_line mapping)
        if (t_line < 8'd168) begin
            Bit#(16) px = (zeroExtend(t_line) * 16'd256) / 16'd168;
            return truncate(px);
        end else begin
            return 9'd256;
        end
    endmethod

    method Bit#(8) beam_y() = truncate(line);

    // HR is active during horizontal blanking (t_line >= 168)
    method Bool is_hr() = (t_line >= 8'd168);

    // VR is active during vertical blanking (line >= disp_end)
    method Bool is_vr() = (line >= disp_end);

    method Bool vblank_edge()   = pulse_vblank;
    method Bool line_int_edge() = pulse_line_int;

    // Video sync pulses
    // HSync: t_line between 180 and 205
    method Bool hsync() = !(t_line >= 8'd180 && t_line < 8'd205);
    // VSync: line in bottom blanking interval (e.g. 240..243 in NTSC)
    method Bool vsync() = !(line >= (max_lines - 9'd20) && line < (max_lines - 9'd17));
    method Bool blank() = (line >= disp_end) || (t_line >= 8'd168);
endmodule: mkRaster

endpackage: Raster
