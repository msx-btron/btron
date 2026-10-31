/*
 * Keyboard.bsv — MSX keyboard matrix (11 rows × 8 columns).
 *
 * Isomorphic with src/emulators/msx/kbd.c.
 * Idle state: all bits 1 (released).  A pressed key clears its bit.
 * PPI port C low nibble selects row; PPI port B returns that row's bits.
 *
 * MSX_INPUT_ROWS = 11 (rows 0–10, rows 11–15 return 0xFF).
 */

package Keyboard;

import Vector::*;

interface Keyboard;
    method Action kbd_down(Bit#(4) row, Bit#(3) col);
    method Action kbd_up(Bit#(4) row, Bit#(3) col);
    method Bit#(8) kbd_row_bits(Bit#(4) row);
    method Bit#(8) peek_row_bits(Bit#(4) row);
endinterface

(* synthesize *)
module mkKeyboard(Keyboard);

    // 11 rows, each 8 bits; idle = 0xFF (all released)
    Vector#(11, Reg#(Bit#(8))) r_matrix <- replicateM(mkReg(8'hFF));

    function Bit#(8) col_mask(Bit#(3) col);
        case (col)
            3'd0: return 8'h01;
            3'd1: return 8'h02;
            3'd2: return 8'h04;
            3'd3: return 8'h08;
            3'd4: return 8'h10;
            3'd5: return 8'h20;
            3'd6: return 8'h40;
            default: return 8'h80;
        endcase
    endfunction

    method Action kbd_down(Bit#(4) row, Bit#(3) col);
        if (row < 11) begin
            Bit#(8) mask = col_mask(col) ^ 8'hFF;
            r_matrix[row] <= r_matrix[row] & mask;
        end
    endmethod

    method Action kbd_up(Bit#(4) row, Bit#(3) col);
        if (row < 11) begin
            Bit#(8) mask = col_mask(col);
            r_matrix[row] <= r_matrix[row] | mask;
        end
    endmethod

    method Bit#(8) kbd_row_bits(Bit#(4) row);
        return (row < 11) ? r_matrix[row] : 8'hFF;
    endmethod

    method Bit#(8) peek_row_bits(Bit#(4) row);
        return (row < 11) ? r_matrix[row] : 8'hFF;
    endmethod

endmodule: mkKeyboard

endpackage: Keyboard

