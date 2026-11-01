/*
 * VDPRegs.bsv — Yamaha V9938 / V9958 Video Display Processor Register File & Palette.
 *
 * Implements:
 *   - Control registers R#0..R#46 (display modes, base addresses, scroll, blitter)
 *   - Status registers S#0..S#9 (interrupt flags, beam retrace, command busy, ID)
 *   - 16-entry RGB565 Palette RAM (standard V9938 2-byte writes and default palette)
 *   - Base address calculators for pattern tables, color tables, and sprites
 *   - Register write bitfield masks matching MSX2+/turboR hardware specification
 *
 * References:
 *   src/emulators/msx/v9938.c
 *   src/emulators/msx/vdp.c
 */

package VDPRegs;

import Vector::*;

// ── VDP Status Flags ─────────────────────────────────────────────────────────
Bit#(8) stFG = 8'h80; // S#0 bit 7: End of active display / VBlank
Bit#(8) st5S = 8'h40; // S#0 bit 6: 5th (or 9th) sprite overflow flag
Bit#(8) stC  = 8'h20; // S#0 bit 5: Sprite collision flag

Bit#(8) stTR = 8'h80; // S#2 bit 7: CPU transfer ready
Bit#(8) stVR = 8'h40; // S#2 bit 6: Vertical retrace
Bit#(8) stHR = 8'h20; // S#2 bit 5: Horizontal retrace
Bit#(8) stBD = 8'h10; // S#2 bit 4: Border detect
Bit#(8) stCE = 8'h01; // S#2 bit 0: Command engine executing (busy)

// ── VDP Register Count ──────────────────────────────────────────────────────
typedef 47 NumVDPRegs;
typedef 10 NumVDPStats;
typedef 16 NumPaletteEntries;

// ── Default MSX Palette (RGB565) ────────────────────────────────────────────
// Standard TMS9918 / MSX palette entries encoded in RGB565
function Bit#(16) defaultPalette(Integer idx);
    case (idx)
        0:  return 16'h0000; // Transparent / Black
        1:  return 16'h0000; // Black
        2:  return 16'h2444; // Medium Green
        3:  return 16'h6DEB; // Light Green
        4:  return 16'h52BF; // Dark Blue
        5:  return 16'h7B9F; // Light Blue
        6:  return 16'hB285; // Dark Red
        7:  return 16'h463F; // Cyan
        8:  return 16'hD285; // Medium Red
        9:  return 16'hFB86; // Light Red
        10: return 16'hD5C4; // Dark Yellow
        11: return 16'hE66A; // Light Yellow
        12: return 16'h23C4; // Dark Green
        13: return 16'hB2B5; // Magenta
        14: return 16'hCCCC; // Gray
        15: return 16'hFFFF; // White
        default: return 16'h0000;
    endcase
endfunction

// Register write mask per V9958 spec
function Bit#(8) v9958RegMask(Bit#(6) regIdx);
    case (regIdx)
        6'd0:  return 8'h7E;
        6'd1:  return 8'h7F;
        6'd2:  return 8'h7F;
        6'd3:  return 8'hFF;
        6'd4:  return 8'h3F;
        6'd5:  return 8'hFF;
        6'd6:  return 8'h3F;
        6'd7:  return 8'hFF;
        6'd8:  return 8'hFB;
        6'd9:  return 8'hBF;
        6'd10: return 8'h07;
        6'd11: return 8'h03;
        6'd12: return 8'hFF;
        6'd13: return 8'hFF;
        6'd14: return 8'h0F;
        6'd15: return 8'h0F;
        6'd16: return 8'hFF; // V9968 EPAL uses R#16 as an 8-bit index
        6'd17: return 8'hBF;
        6'd18: return 8'hFF;
        6'd19: return 8'hFF;
        6'd20: return 8'h3F;
        6'd21: return 8'h3F;
        6'd22: return 8'h3F;
        6'd23: return 8'hFF;
        6'd25: return 8'h7F;
        6'd26: return 8'h3F;
        6'd27: return 8'h07;
        // R#32..R#46 (Command engine) are fully writable
        default: return 8'hFF;
    endcase
endfunction

interface VDPRegs;
    // Register read/write
    method Bit#(8) read_reg(Bit#(6) idx);
    method Action  write_reg(Bit#(6) idx, Bit#(8) val);

    // Status read/write
    method Bit#(8) read_stat(Bit#(4) idx);
    method Action  set_stat_bits(Bit#(4) idx, Bit#(8) mask);
    method Action  clear_stat_bits(Bit#(4) idx, Bit#(8) mask);
    method Action  write_stat(Bit#(4) idx, Bit#(8) val);

    // Palette read/write
    method Bit#(16) read_palette(Bit#(4) idx);
    method Action   write_palette(Bit#(4) idx, Bit#(16) rgb565);

    // Display mode & geometry queries
    (* always_ready *) method Bit#(5)  display_mode(); // M5..M1
    (* always_ready *) method Bool     display_enabled(); // R1 DISP
    (* always_ready *) method Bool     lines_212();    // R9 LN (0: 192, 1: 212)
    (* always_ready *) method Bool     pal_timing();   // R9 S/N (0: NTSC, 1: PAL)
    (* always_ready *) method Bool     ie0_enabled();  // R1 IE0 (Frame IRQ)
    (* always_ready *) method Bool     ie1_enabled();  // R0 IE1 (Line IRQ)
    (* always_ready *) method Bit#(8)  irq_line();     // R19 Interrupt line
    (* always_ready *) method Bit#(8)  vscroll();      // R23 Vertical display offset
    (* always_ready *) method Bit#(8)  border_color(); // R7 EC (border / backdrop color)

    // Base address calculations (in 24-bit VRAM space)
    (* always_ready *) method Bit#(18) name_table_base();
    (* always_ready *) method Bit#(18) color_table_base();
    (* always_ready *) method Bit#(18) pattern_table_base();
    (* always_ready *) method Bit#(18) sprite_attrib_base();
    (* always_ready *) method Bit#(18) sprite_pattern_base();

    // CPU VRAM base address pointer (R#14)
    (* always_ready *) method Bit#(4)  vram_page_cpu();
endinterface: VDPRegs

module mkVDPRegs(VDPRegs);
    Vector#(NumVDPRegs, Reg#(Bit#(8)))  regs  <- replicateM(mkReg(0));
    Vector#(NumVDPStats, Reg#(Bit#(8))) stats <- replicateM(mkReg(0));
    Vector#(NumPaletteEntries, Reg#(Bit#(16))) palette <- replicateM(mkReg(0));

    // Reset default registers and palette
    Reg#(Bool) initialized <- mkReg(False);
    rule do_init (!initialized);
        initialized <= True;
        // V9958 ID: S#1 bits 5..1 = 00010 (V9958 ID)
        stats[1] <= 8'h04;
        // S#2 bits 3 & 2 are always 1 on V9938/V9958
        stats[2] <= 8'h0C | stTR; // ready for transfer
        for (Integer i = 0; i < 16; i = i + 1) begin
            palette[i] <= defaultPalette(i);
        end
    endrule

    // Mode bits: M5, M4, M3 in R#0[3:1], M2, M1 in R#1[3:4]
    // Return M5..M1 as a 5-bit integer
    function Bit#(5) get_mode();
        Bit#(8) r0 = regs[0];
        Bit#(8) r1 = regs[1];
        Bit#(3) m5_m3 = r0[3:1];
        Bit#(1) m2    = r1[3];
        Bit#(1) m1    = r1[4];
        return {m5_m3, m2, m1};
    endfunction

    method Bit#(8) read_reg(Bit#(6) idx);
        if (idx < fromInteger(valueOf(NumVDPRegs)))
            return regs[idx];
        else
            return 8'hFF;
    endmethod

    method Action write_reg(Bit#(6) idx, Bit#(8) val);
        if (idx < fromInteger(valueOf(NumVDPRegs))) begin
            Bit#(8) mask = v9958RegMask(idx);
            regs[idx] <= (val & mask);
        end
    endmethod

    method Bit#(8) read_stat(Bit#(4) idx);
        if (idx < fromInteger(valueOf(NumVDPStats)))
            return stats[idx];
        else
            return 8'hFF;
    endmethod

    method Action set_stat_bits(Bit#(4) idx, Bit#(8) mask);
        if (idx < fromInteger(valueOf(NumVDPStats)))
            stats[idx] <= stats[idx] | mask;
    endmethod

    method Action clear_stat_bits(Bit#(4) idx, Bit#(8) mask);
        if (idx < fromInteger(valueOf(NumVDPStats)))
            stats[idx] <= stats[idx] & ~mask;
    endmethod

    method Action write_stat(Bit#(4) idx, Bit#(8) val);
        if (idx < fromInteger(valueOf(NumVDPStats)))
            stats[idx] <= val;
    endmethod

    method Bit#(16) read_palette(Bit#(4) idx) = palette[idx];

    method Action write_palette(Bit#(4) idx, Bit#(16) rgb565);
        palette[idx] <= rgb565;
    endmethod

    method Bit#(5) display_mode()   = get_mode();
    method Bool    display_enabled() = (regs[1][6] == 1'b1);
    method Bool    lines_212()       = (regs[9][7] == 1'b1);
    method Bool    pal_timing()      = (regs[9][1] == 1'b1);
    method Bool    ie0_enabled()     = (regs[1][5] == 1'b1);
    method Bool    ie1_enabled()     = (regs[0][0] == 1'b1);
    method Bit#(8) irq_line()        = regs[19];
    method Bit#(8) vscroll()         = regs[23];
    method Bit#(8) border_color()    = regs[7] & 8'h0F;

    // Pattern Name Table: R#2 bits 6..0 << 10
    method Bit#(18) name_table_base();
        return {1'b0, regs[2][6:0], 10'b0};
    endmethod

    // Color Table: (R#10[2:0] << 14) | (R#3 << 6)
    method Bit#(18) color_table_base();
        return {1'b0, regs[10][2:0], regs[3], 6'b0};
    endmethod

    // Pattern Generator Table: R#4 bits 5..0 << 11
    method Bit#(18) pattern_table_base();
        return {1'b0, regs[4][5:0], 11'b0};
    endmethod

    // Sprite Attribute Table: (R#11[1:0] << 15) | (R#5 << 7)
    method Bit#(18) sprite_attrib_base();
        return {1'b0, regs[11][1:0], regs[5], 7'b0};
    endmethod

    // Sprite Pattern Generator: R#6 bits 5..0 << 11
    method Bit#(18) sprite_pattern_base();
        return {1'b0, regs[6][5:0], 11'b0};
    endmethod

    // CPU VRAM base address pointer (R#14 bits 3..0)
    method Bit#(4) vram_page_cpu() = regs[14][3:0];
endmodule: mkVDPRegs

endpackage: VDPRegs
