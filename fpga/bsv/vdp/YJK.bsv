/*
 * YJK.bsv — Yamaha V9958 YJK and YAE Color Decoder.
 *
 * Implements:
 *   - Screen 10 / Screen 12 (YJK=1, YAE=0: 19,268 colors)
 *   - Screen 11 (YJK=1, YAE=1: YJK + 16-color palette attribute)
 *   - Hardware arithmetic clamp to [0..31] for 15-bit RGB
 *   - 1:1 match with openMSX and MSX turbo R hardware formulas
 *
 * Formulas:
 *   J = 6-bit signed from P2[2:0] and P3[2:0]
 *   K = 6-bit signed from P0[2:0] and P1[2:0]
 *   Y = Pn[7:3] (5 bits unsigned, 0..31)
 *   R = clamp(Y + J, 0, 31)
 *   G = clamp(Y + K, 0, 31)
 *   B = clamp((5*Y - 2*J - K + 2) / 4, 0, 31)
 */

package YJK;

import Vector::*;

function Bit#(16) rgb555ToRgb565(Bit#(5) r, Bit#(5) g, Bit#(5) b);
    Bit#(6) g6 = {g, g[4]};
    return {r, g6, b};
endfunction

function Bit#(5) clamp31(Int#(12) val);
    if (val < 0)
        return 5'd0;
    else if (val > 31)
        return 5'd31;
    else
        return truncate(pack(val)[4:0]);
endfunction

// Decodes a single pixel within a 4-pixel YJK cluster
function Bit#(16) decodeYJKPixel(
    Bit#(8) p0,
    Bit#(8) p1,
    Bit#(8) p2,
    Bit#(8) p3,
    Bit#(2) pixelIdx,
    Bool    yae,
    Vector#(16, Bit#(16)) pal16
);
    // Unpack pixel byte for the requested pixel index
    Bit#(8) p = case (pixelIdx)
        2'd0: p0;
        2'd1: p1;
        2'd2: p2;
        2'd3: p3;
    endcase;

    if (yae && (p[3] == 1'b1)) begin
        // YAE Palette attribute mode: high 4 bits select 16-color palette
        Bit#(4) palIdx = p[7:4];
        return pal16[palIdx];
    end else begin
        // Signed J & K extraction (6-bit two's complement)
        Bit#(6) rawJ = {p3[2:0], p2[2:0]};
        Bit#(6) rawK = {p1[2:0], p0[2:0]};

        Int#(12) j = signExtend(unpack(rawJ));
        Int#(12) k = signExtend(unpack(rawK));
        Int#(12) y = unpack({7'b0, p[7:3]});

        Int#(12) rVal = y + j;
        Int#(12) gVal = y + k;
        Int#(12) bVal = (5 * y - 2 * j - k + 2) / 4;

        Bit#(5) r = clamp31(rVal);
        Bit#(5) g = clamp31(gVal);
        Bit#(5) b = clamp31(bVal);

        return rgb555ToRgb565(r, g, b);
    end
endfunction

endpackage: YJK

