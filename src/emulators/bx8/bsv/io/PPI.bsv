/*
 * PPI.bsv — Intel 8255 PPI at ports 0xA8–0xAB.
 *
 * Isomorphic with src/emulators/msx/ppi.c.
 * Port map:
 *   0xA8  Port A: primary slot-select latch (write side effect in SlotManager)
 *   0xA9  Port B: keyboard column data (input, driven by Keyboard module)
 *   0xAA  Port C: low nibble = keyboard row select, high = cassette
 *   0xAB  Control word or BSR (bit-set/reset)
 *
 * On MSX, Port A's slot-select side effect is applied by SlotManager;
 * PPI simply stores the latch for readback.
 */

package PPI;

import FIFOF::*;

interface PPI;
    method Action io_write(Bit#(8) port, Bit#(8) data);
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
    (* always_ready *) method Bit#(4) kbd_row;        // current row (Port C low nibble)
    (* always_ready *) method Bit#(8) read_ppi_a;     // primary slot select latch
    method Action set_kbd_data(Bit#(8) data);          // drive Port B from keyboard
    (* always_ready *) method Bit#(8) peek_pc;
endinterface

(* synthesize *)
module mkPPI(PPI);

    Reg#(Bit#(8)) r_pa          <- mkReg(0);  // Port A latch (slot select)
    Reg#(Bit#(8)) r_kbd_in      <- mkReg(8'hFF); // Port B: keyboard columns (active low)
    Reg#(Bit#(8)) r_pc          <- mkReg(0);  // Port C: row select + cassette
    Reg#(Bit#(8)) r_ctrl        <- mkReg(0);  // Control word

    // Queue for CPU I/O writes to decouple from reading/driving
    FIFOF#(Tuple2#(Bit#(8), Bit#(8))) q_write <- mkFIFOF;

    // ---- Process CPU I/O writes from FIFO ----
    rule do_io_write (q_write.notEmpty);
        match {.port, .data} = q_write.first;
        q_write.deq;

        case (port & 8'h03)
            8'h00: r_pa <= data;     // Port A: store latch (SlotManager handles side effect)
            8'h01: noAction;         // Port B is input on MSX; writes ignored
            8'h02: r_pc <= data;     // Port C: row select and cassette
            8'h03: begin
                if (data[7] == 1) begin
                    r_ctrl <= data;  // mode-set word
                end else begin
                    // BSR: set or clear one bit of Port C
                    Bit#(3) bit_idx = data[3:1];
                    Bit#(8) set_mask  = case (bit_idx)
                        3'd0: 8'h01; 3'd1: 8'h02; 3'd2: 8'h04; 3'd3: 8'h08;
                        3'd4: 8'h10; 3'd5: 8'h20; 3'd6: 8'h40; default: 8'h80;
                    endcase;
                    Bit#(8) clr_mask = set_mask ^ 8'hFF;
                    if (data[0] == 1)
                        r_pc <= r_pc | set_mask;
                    else
                        r_pc <= r_pc & clr_mask;
                end
            end
        endcase
    endrule

    // ---- I/O write method ----
    method Action io_write(Bit#(8) port, Bit#(8) data);
        q_write.enq(tuple2(port, data));
    endmethod

    // ---- I/O read ----
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
        Bit#(8) result = 8'hFF;
        case (port & 8'h03)
            8'h00: result = r_pa;       // Port A: slot select latch
            8'h01: result = r_kbd_in;   // Port B: keyboard column data
            8'h02: result = r_pc;       // Port C: readback
            8'h03: result = 8'hFF;      // Control: not readable
        endcase
        return result;
    endmethod

    method Bit#(4) kbd_row   = r_pc[3:0];
    method Bit#(8) read_ppi_a = r_pa;
    method Action  set_kbd_data(Bit#(8) data); r_kbd_in <= data; endmethod
    method Bit#(8) peek_pc    = r_pc;

endmodule: mkPPI

endpackage: PPI
