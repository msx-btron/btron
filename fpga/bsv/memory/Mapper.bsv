/*
 * Mapper.bsv — MSX 512KB - 4MB Memory Mapper Engine.
 *
 * Implements:
 *   1. 4 Segment registers mapped to I/O ports #FC, #FD, #FE, #FF.
 *   2. Physical 24-bit address translation for Slot 3 RAM:
 *        addr = baseRAM + ({segment, cpu_addr[13:0]} & mask).
 *   3. Default power-on reset segment initialization:
 *        Page 0: 3, Page 1: 2, Page 2: 1, Page 3: 0.
 *
 * References:
 *   src/emulators/msx/slots.c
 *   src/emulators/msx/t9769c.c
 */

package Mapper;

import Bus::*;

interface Mapper;
    // I/O Port #FC..#FF interface
    method Bit#(8) io_read(Bit#(8) port);
    method Action  io_write(Bit#(8) port, Bit#(8) val);

    // RAM address translation for 16KB pages (cpu_addr[15:14])
    method BusAddr translate_ram(Bit#(16) cpu_addr);
endinterface: Mapper

(* synthesize *)
module mkMapper(Mapper);
    // Segment registers for each 16KB page
    Reg#(Bit#(8)) seg0 <- mkReg(8'd3); // Page 0: default segment 3
    Reg#(Bit#(8)) seg1 <- mkReg(8'd2); // Page 1: default segment 2
    Reg#(Bit#(8)) seg2 <- mkReg(8'd1); // Page 2: default segment 1
    Reg#(Bit#(8)) seg3 <- mkReg(8'd0); // Page 3: default segment 0

    // 512 KB mask: 32 segments of 16KB (bits 4:0 active)
    Bit#(8) mapper_mask = 8'h1F;

    method Bit#(8) io_read(Bit#(8) port);
        case (port[1:0])
            2'd0: return seg0 | ~mapper_mask;
            2'd1: return seg1 | ~mapper_mask;
            2'd2: return seg2 | ~mapper_mask;
            2'd3: return seg3 | ~mapper_mask;
        endcase
    endmethod

    method Action io_write(Bit#(8) port, Bit#(8) val);
        case (port[1:0])
            2'd0: seg0 <= val & mapper_mask;
            2'd1: seg1 <= val & mapper_mask;
            2'd2: seg2 <= val & mapper_mask;
            2'd3: seg3 <= val & mapper_mask;
        endcase
    endmethod

    method BusAddr translate_ram(Bit#(16) cpu_addr);
        Bit#(2)  page = cpu_addr[15:14];
        Bit#(14) offset = cpu_addr[13:0];

        Bit#(8) seg = case (page)
            2'd0: seg0;
            2'd1: seg1;
            2'd2: seg2;
            2'd3: seg3;
        endcase;

        Bit#(22) ram_offset = {seg & mapper_mask, offset};
        return 24'h010000 + zeroExtend(ram_offset);
    endmethod
endmodule: mkMapper

endpackage: Mapper
