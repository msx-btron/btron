/*
 * ALU.bsv — Unified Z80 / R800 Arithmetic Logic Unit & Hardware Multiplier.
 *
 * Implements:
 *   1. Full 8-bit and 16-bit arithmetic, logic, shift/rotate operations with
 *      precise Z80 flag calculation (SF, ZF, YF, HF, XF, PF/VF, NF, CF).
 *   2. R800 hardware multiplier:
 *        - MULUB (8x8 -> 16-bit, 14 cycles, sets ZF, CF, clears SF, PF)
 *        - MULUQ (16x16 -> 32-bit, 36 cycles, sets ZF, CF, clears SF, PF)
 *
 * References:
 *   src/emulators/msx/z80.c
 *   src/emulators/msx/r800.c
 */

package ALU;

// ── Flag bit indices ────────────────────────────────────────────────────────
typedef struct {
    Bool sf;   // Sign
    Bool zf;   // Zero
    Bool yf;   // Bit 5 of result
    Bool hf;   // Half carry
    Bool xf;   // Bit 3 of result
    Bool pf;   // Parity / Overflow (V)
    Bool nf;   // Add/Subtract
    Bool cf;   // Carry
} Z80Flags deriving(Bits, Eq);

function Bit#(8) packFlags(Z80Flags f);
    return { pack(f.sf), pack(f.zf), pack(f.yf), pack(f.hf),
             pack(f.xf), pack(f.pf), pack(f.nf), pack(f.cf) };
endfunction

function Z80Flags unpackFlags(Bit#(8) b);
    return Z80Flags {
        sf: unpack(b[7]), zf: unpack(b[6]), yf: unpack(b[5]), hf: unpack(b[4]),
        xf: unpack(b[3]), pf: unpack(b[2]), nf: unpack(b[1]), cf: unpack(b[0])
    };
endfunction

// ── Parity helper ───────────────────────────────────────────────────────────
function Bool calcParity(Bit#(8) val);
    Bit#(1) p = 1;
    for (Integer i = 0; i < 8; i = i + 1)
        p = p ^ val[i];
    return unpack(p);
endfunction

// ── ALU Operation Types ─────────────────────────────────────────────────────
typedef enum {
    AluADD, AluADC, AluSUB, AluSBC,
    AluAND, AluXOR, AluOR,  AluCP
} AluOp8 deriving(Bits, Eq);

typedef enum {
    AluRLC, AluRRC, AluRL,  AluRR,
    AluSLA, AluSRA, AluSLL, AluSRL
} ShiftOp deriving(Bits, Eq);

// ── ALU Result Structure ────────────────────────────────────────────────────
typedef struct {
    Bit#(8)   res;
    Z80Flags  flags;
} AluResult8 deriving(Bits, Eq);

typedef struct {
    Bit#(16)  res;
    Z80Flags  flags;
} AluResult16 deriving(Bits, Eq);

// ── 8-bit ALU Combinatorial Function ────────────────────────────────────────
function AluResult8 evalAlu8(AluOp8 op, Bit#(8) a, Bit#(8) b, Bool cin);
    Bit#(9) ext_a = {1'b0, a};
    Bit#(9) ext_b = {1'b0, b};
    Bit#(9) c_bit = cin ? 9'd1 : 9'd0;

    Bit#(8)   res = 0;
    Bool sf = False, zf = False, yf = False, hf = False;
    Bool xf = False, pf = False, nf = False, cf = False;

    case (op)
        AluADD: begin
            Bit#(9) sum = ext_a + ext_b;
            res = sum[7:0];
            cf = unpack(sum[8]);
            Bit#(5) sum4 = {1'b0, a[3:0]} + {1'b0, b[3:0]};
            hf = unpack(sum4[4]);
            // Overflow: operands same sign, result opposite
            pf = (a[7] == b[7]) && (res[7] != a[7]);
            nf = False;
        end
        AluADC: begin
            Bit#(9) sum = ext_a + ext_b + c_bit;
            res = sum[7:0];
            cf = unpack(sum[8]);
            Bit#(5) sum4 = {1'b0, a[3:0]} + {1'b0, b[3:0]} + {4'd0, c_bit[0]};
            hf = unpack(sum4[4]);
            pf = (a[7] == b[7]) && (res[7] != a[7]);
            nf = False;
        end
        AluSUB, AluCP: begin
            Bit#(9) diff = ext_a - ext_b;
            res = diff[7:0];
            cf = unpack(diff[8]);
            hf = (a[3:0] < b[3:0]);
            // Overflow: operands different sign, result != a sign
            pf = (a[7] != b[7]) && (res[7] != a[7]);
            nf = True;
        end
        AluSBC: begin
            Bit#(9) diff = ext_a - ext_b - c_bit;
            res = diff[7:0];
            cf = unpack(diff[8]);
            hf = (a[3:0] < (b[3:0] + c_bit[3:0]));
            pf = (a[7] != b[7]) && (res[7] != a[7]);
            nf = True;
        end
        AluAND: begin
            res = a & b;
            cf = False;
            hf = True;
            pf = calcParity(res);
            nf = False;
        end
        AluXOR: begin
            res = a ^ b;
            cf = False;
            hf = False;
            pf = calcParity(res);
            nf = False;
        end
        AluOR: begin
            res = a | b;
            cf = False;
            hf = False;
            pf = calcParity(res);
            nf = False;
        end
    endcase

    sf = unpack(res[7]);
    zf = (res == 0);
    yf = unpack(res[5]);
    xf = unpack(res[3]);

    return AluResult8 {
        res:   res,
        flags: Z80Flags { sf: sf, zf: zf, yf: yf, hf: hf, xf: xf, pf: pf, nf: nf, cf: cf }
    };
endfunction

// ── 8-bit Increment / Decrement ─────────────────────────────────────────────
function AluResult8 evalInc8(Bit#(8) a, Z80Flags oldFlags);
    Bit#(8) res = a + 1;
    return AluResult8 {
        res: res,
        flags: Z80Flags {
            sf: unpack(res[7]),
            zf: (res == 0),
            yf: unpack(res[5]),
            hf: unpack((a[3:0] == 4'hF) ? 1'b1 : 1'b0),
            xf: unpack(res[3]),
            pf: (a == 8'h7F), // V overflow
            nf: False,
            cf: oldFlags.cf  // CF preserved
        }
    };
endfunction

function AluResult8 evalDec8(Bit#(8) a, Z80Flags oldFlags);
    Bit#(8) res = a - 1;
    return AluResult8 {
        res: res,
        flags: Z80Flags {
            sf: unpack(res[7]),
            zf: (res == 0),
            yf: unpack(res[5]),
            hf: unpack((a[3:0] == 4'h0) ? 1'b1 : 1'b0),
            xf: unpack(res[3]),
            pf: (a == 8'h80), // V underflow
            nf: True,
            cf: oldFlags.cf  // CF preserved
        }
    };
endfunction

// ── 8-bit Shift / Rotate ────────────────────────────────────────────────────
function AluResult8 evalShift8(ShiftOp op, Bit#(8) a, Bool oldCarry);
    Bit#(8) res = 0;
    Bool    cf  = False;

    case (op)
        AluRLC: begin
            cf  = unpack(a[7]);
            res = {a[6:0], a[7]};
        end
        AluRRC: begin
            cf  = unpack(a[0]);
            res = {a[0], a[7:1]};
        end
        AluRL: begin
            cf  = unpack(a[7]);
            res = {a[6:0], pack(oldCarry)};
        end
        AluRR: begin
            cf  = unpack(a[0]);
            res = {pack(oldCarry), a[7:1]};
        end
        AluSLA: begin
            cf  = unpack(a[7]);
            res = {a[6:0], 1'b0};
        end
        AluSRA: begin
            cf  = unpack(a[0]);
            res = {a[7], a[7:1]};
        end
        AluSLL: begin // Undocumented Z80 SLL: bit 0 filled with 1
            cf  = unpack(a[7]);
            res = {a[6:0], 1'b1};
        end
        AluSRL: begin
            cf  = unpack(a[0]);
            res = {1'b0, a[7:1]};
        end
    endcase

    return AluResult8 {
        res: res,
        flags: Z80Flags {
            sf: unpack(res[7]),
            zf: (res == 0),
            yf: unpack(res[5]),
            hf: False,
            xf: unpack(res[3]),
            pf: calcParity(res),
            nf: False,
            cf: cf
        }
    };
endfunction

// ── 16-bit ADD HL, ss ───────────────────────────────────────────────────────
function AluResult16 evalAdd16(Bit#(16) hl, Bit#(16) ss, Z80Flags oldFlags);
    Bit#(17) sum   = {1'b0, hl} + {1'b0, ss};
    Bit#(16) res   = sum[15:0];
    Bit#(13) sum12 = {1'b0, hl[11:0]} + {1'b0, ss[11:0]};
    Bool hf = unpack(sum12[12]);
    return AluResult16 {
        res: res,
        flags: Z80Flags {
            sf: oldFlags.sf,
            zf: oldFlags.zf,
            yf: unpack(res[13]),
            hf: hf,
            xf: unpack(res[11]),
            pf: oldFlags.pf,
            nf: False,
            cf: unpack(sum[16])
        }
    };
endfunction

// ── 16-bit ADC HL, ss ───────────────────────────────────────────────────────
function AluResult16 evalAdc16(Bit#(16) hl, Bit#(16) ss, Bool cin);
    Bit#(17) c_bit = cin ? 17'd1 : 17'd0;
    Bit#(17) sum   = {1'b0, hl} + {1'b0, ss} + c_bit;
    Bit#(16) res   = sum[15:0];
    Bit#(13) sum12 = {1'b0, hl[11:0]} + {1'b0, ss[11:0]} + c_bit[12:0];
    Bool hf = unpack(sum12[12]);
    Bool pf = (hl[15] == ss[15]) && (res[15] != hl[15]);
    return AluResult16 {
        res: res,
        flags: Z80Flags {
            sf: unpack(res[15]),
            zf: (res == 0),
            yf: unpack(res[13]),
            hf: hf,
            xf: unpack(res[11]),
            pf: pf,
            nf: False,
            cf: unpack(sum[16])
        }
    };
endfunction

// ── 16-bit SBC HL, ss ───────────────────────────────────────────────────────
function AluResult16 evalSbc16(Bit#(16) hl, Bit#(16) ss, Bool cin);
    Bit#(17) c_bit = cin ? 17'd1 : 17'd0;
    Bit#(17) diff  = {1'b0, hl} - {1'b0, ss} - c_bit;
    Bit#(16) res   = diff[15:0];
    Bool hf = (hl[11:0] < (ss[11:0] + c_bit[11:0]));
    Bool pf = (hl[15] != ss[15]) && (res[15] != hl[15]);
    return AluResult16 {
        res: res,
        flags: Z80Flags {
            sf: unpack(res[15]),
            zf: (res == 0),
            yf: unpack(res[13]),
            hf: hf,
            xf: unpack(res[11]),
            pf: pf,
            nf: True,
            cf: unpack(diff[16])
        }
    };
endfunction

// ── R800 Hardware Multiplier Interface ──────────────────────────────────────
interface R800Multiplier;
    // Start an 8x8 -> 16 bit multiply (MULUB)
    method Action start_mulub(Bit#(8) a, Bit#(8) b);
    // Start a 16x16 -> 32 bit multiply (MULUQ)
    method Action start_muluq(Bit#(16) hl, Bit#(16) ss);
    // Poll completion
    method Bool   done();
    // Get result: for MULUB, res[15:0] is HL; for MULUQ, res[31:0] is DE:HL
    method Bit#(32) get_result();
    method Z80Flags get_flags();
endinterface: R800Multiplier

(* synthesize *)
module mkR800Multiplier(R800Multiplier);
    Reg#(Bit#(6))  counter   <- mkReg(0);
    Reg#(Bool)     busy      <- mkReg(False);
    Reg#(Bool)     is_16bit  <- mkReg(False);
    Reg#(Bit#(32)) prod_reg  <- mkReg(0);
    Reg#(Z80Flags) flags_reg <- mkReg(unpack(0));

    // Multi-cycle down-counter for timing accuracy
    rule step_mult if (busy);
        if (counter > 1) begin
            counter <= counter - 1;
        end else begin
            busy    <= False;
            counter <= 0;
        end
    endrule

    method Action start_mulub(Bit#(8) a, Bit#(8) b) if (!busy);
        Bit#(16) p = zeroExtend(a) * zeroExtend(b);
        prod_reg  <= zeroExtend(p);
        flags_reg <= Z80Flags {
            sf: False,
            zf: (p == 0),
            yf: unpack(p[13]),
            hf: False,
            xf: unpack(p[11]),
            pf: False,
            nf: False,
            cf: (p > 16'h00FF)
        };
        is_16bit <= False;
        counter  <= 6'd14; // 14 T-states for MULUB
        busy     <= True;
    endmethod

    method Action start_muluq(Bit#(16) hl, Bit#(16) ss) if (!busy);
        Bit#(32) p = zeroExtend(hl) * zeroExtend(ss);
        prod_reg  <= p;
        flags_reg <= Z80Flags {
            sf: False,
            zf: (p == 0),
            yf: unpack(p[29]),
            hf: False,
            xf: unpack(p[27]),
            pf: False,
            nf: False,
            cf: (p > 32'h0000FFFF)
        };
        is_16bit <= True;
        counter  <= 6'd36; // 36 T-states for MULUQ (matching r800.c)
        busy     <= True;
    endmethod

    method Bool done() = !busy;

    method Bit#(32) get_result() if (!busy);
        return prod_reg;
    endmethod

    method Z80Flags get_flags() if (!busy);
        return flags_reg;
    endmethod
endmodule: mkR800Multiplier

endpackage: ALU
