/*
 * Decode.bsv — Systematic Z80 / R800 Instruction Decoder Definitions.
 *
 * Implements the standard Cristian Dinu Z80 opcode octal table mapping:
 *   Opcode = {x[1:0], y[2:0], z[2:0]}
 *   x = bits 7:6 (0, 1, 2, 3)
 *   y = bits 5:3 (register or opcode sub-function)
 *   z = bits 2:0 (operation class)
 *   p = y[2:1]   (16-bit register pair: 00=BC, 01=DE, 10=HL, 11=SP/AF)
 *   q = y[0]     (variant selector)
 *
 * References:
 *   "Decoding Z80 Opcodes" — Cristian Dinu
 *   src/emulators/msx/z80.c
 *   src/emulators/msx/r800.c
 */

package Decode;

import ALU::*;

// ── 8-bit Register Selection (y or z index) ─────────────────────────────────
typedef enum {
    RegB = 3'b000,
    RegC = 3'b001,
    RegD = 3'b010,
    RegE = 3'b011,
    RegH = 3'b100,
    RegL = 3'b101,
    RegM = 3'b110, // (HL) or (IX+d)/(IY+d)
    RegA = 3'b111
} Reg8ID deriving (Bits, Eq);

// ── 16-bit Register Pair Selection (p index) ────────────────────────────────
typedef enum {
    PairBC = 2'b00,
    PairDE = 2'b01,
    PairHL = 2'b10,
    PairSP = 2'b11  // or PairAF in PUSH/POP
} Reg16ID deriving (Bits, Eq);

// ── Condition Code (cc index in bits 5:3) ────────────────────────────────────
typedef enum {
    CondNZ = 3'b000,
    CondZ  = 3'b001,
    CondNC = 3'b010,
    CondC  = 3'b011,
    CondPO = 3'b100, // Parity Odd / No Overflow
    CondPE = 3'b101, // Parity Even / Overflow
    CondP  = 3'b110, // Positive
    CondM  = 3'b111  // Minus / Negative
} CondCode deriving (Bits, Eq);

// ── Instruction Prefix ──────────────────────────────────────────────────────
typedef enum {
    PreNone,
    PreCB,
    PreED,
    PreDD,
    PreFD,
    PreDDCB,
    PreFDCB
} PrefixType deriving (Bits, Eq);

// ── Evaluate Condition ──────────────────────────────────────────────────────
function Bool evalCondition(CondCode cc, Z80Flags f);
    case (cc)
        CondNZ: return !f.zf;
        CondZ:  return f.zf;
        CondNC: return !f.cf;
        CondC:  return f.cf;
        CondPO: return !f.pf;
        CondPE: return f.pf;
        CondP:  return !f.sf;
        CondM:  return f.sf;
    endcase
endfunction

// ── Opcode Octal Fields ─────────────────────────────────────────────────────
typedef struct {
    Bit#(2)  x;
    Bit#(3)  y;
    Bit#(3)  z;
    Bit#(2)  p;
    Bit#(1)  q;
} DecodedOp deriving (Bits, Eq);

function DecodedOp splitOpcode(Bit#(8) op);
    return DecodedOp {
        x: op[7:6],
        y: op[5:3],
        z: op[2:0],
        p: op[5:4],
        q: op[3]
    };
endfunction

endpackage: Decode

