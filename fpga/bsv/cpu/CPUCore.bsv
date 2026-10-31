/*
 * CPUCore.bsv — Unified Z80A / R800 Dual-Mode Clean-Room CPU Engine.
 *
 * Implements:
 *   1. Full register set: AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, SP, PC, I, R, WZ.
 *   2. Cristian Dinu systematic opcode decoding (x, y, z, p, q factorization).
 *   3. Integrated T9769C MSX turbo R engine (Ports 0xE4..0xE7, 0xF4).
 *   4. Integrated R800 hardware multiplier (MULUB: 14t, MULUQ: 36t).
 *   5. Dual clock gating:
 *        - Z80 mode: advances on tick_3m58 (3.579545 MHz)
 *        - R800 mode: advances on tick_7m (7.15909 MHz)
 *   6. Split-phase memory bus interface directly into MemoryArbiter.
 *   7. Architectural state introspection for lockstep Verilator co-sim.
 *
 * References:
 *   src/emulators/msx/z80.c
 *   src/emulators/msx/r800.c
 *   src/emulators/msx/t9769c.c
 */

package CPUCore;

import FIFOF::*;
import GetPut::*;

import Bus::*;
import ALU::*;
import Decode::*;
import T9769C::*;

// ── CPU Architectural State for Co-Sim Introspection ────────────────────────
typedef struct {
    Bit#(16) pc;
    Bit#(8)  a;
    Bit#(8)  f;
    Bit#(8)  b;
    Bit#(8)  c;
    Bit#(8)  d;
    Bit#(8)  e;
    Bit#(8)  h;
    Bit#(8)  l;
    Bit#(16) ix;
    Bit#(16) iy;
    Bit#(16) sp;
} CPURegs deriving (Bits, Eq);

// ── CPU Micro-Sequencer States ──────────────────────────────────────────────
typedef enum {
    CpuReset,
    FetchOp,
    WaitOp,
    DecodeOp,
    // Multi-byte immediate operands
    FetchImm8,
    WaitImm8,
    FetchImm16Lo,
    WaitImm16Lo,
    FetchImm16Hi,
    WaitImm16Hi,
    // Memory data access
    ReadMem8,
    WaitMemRead8,
    WriteMem8,
    WaitMemWrite8,
    // Stack operations
    PushLo,
    WaitPushLo,
    PushHi,
    WaitPushHi,
    PopLo,
    WaitPopLo,
    PopHi,
    WaitPopHi,
    // Multiplier
    ExecMul,
    // Complete
    RetireInst
} CpuState deriving (Bits, Eq);

// ── Sub-target for pending memory or immediate operation ────────────────────
typedef enum {
    TargetNone,
    TargetRegA,
    TargetRegB,
    TargetRegC,
    TargetRegD,
    TargetRegE,
    TargetRegH,
    TargetRegL,
    TargetPairBC,
    TargetPairDE,
    TargetPairHL,
    TargetPairSP,
    TargetMemHL,
    TargetJp,
    TargetCall,
    TargetOutA,
    TargetInA,
    TargetAluImm
} OpTarget deriving (Bits, Eq);

// ── CPU Core Interface ──────────────────────────────────────────────────────
interface CPUCore;
    // Clock tick enables from MSXClocks
    method Action tick_28m();
    method Action tick_7m();
    method Action tick_3m58();
    method Action tick_e6();

    // Outbound memory/IO bus request to MemoryArbiter
    interface Get#(BusReq) bus_req;

    // Inbound memory/IO bus response from MemoryArbiter
    interface Put#(BusResp) bus_resp;

    // Architectural state introspection
    (* always_ready *) method CPURegs get_regs();
    (* always_ready *) method Bool    inst_retired();
    (* always_ready *) method Bool    is_r800_mode();
    (* always_ready *) method Bit#(5) get_state();
    (* always_ready *) method Bit#(8) get_opcode();
endinterface: CPUCore

(* synthesize *)
module mkCPUCore(CPUCore);
    // Submodules
    R800Multiplier mult   <- mkR800Multiplier;
    T9769C         t9769c <- mkT9769C;

    // Bus staging FIFOs
    FIFOF#(BusReq)  q_req_out <- mkFIFOF;
    FIFOF#(BusResp) q_resp_in <- mkFIFOF;

    // Architectural Registers
    Reg#(Bit#(8))  reg_a  <- mkReg(8'hFF);
    Reg#(Z80Flags) reg_f  <- mkReg(unpack(8'hFF));
    Reg#(Bit#(8))  reg_b  <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_c  <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_d  <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_e  <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_h  <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_l  <- mkReg(8'hFF);

    // Alternate (shadow) Registers
    Reg#(Bit#(8))  reg_a_ <- mkReg(8'hFF);
    Reg#(Z80Flags) reg_f_ <- mkReg(unpack(8'hFF));
    Reg#(Bit#(8))  reg_b_ <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_c_ <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_d_ <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_e_ <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_h_ <- mkReg(8'hFF);
    Reg#(Bit#(8))  reg_l_ <- mkReg(8'hFF);

    // Pointer and Index Registers
    Reg#(Bit#(16)) reg_pc <- mkReg(16'h0000);
    Reg#(Bit#(16)) reg_sp <- mkReg(16'hFFFF);
    Reg#(Bit#(16)) reg_ix <- mkReg(16'hFFFF);
    Reg#(Bit#(16)) reg_iy <- mkReg(16'hFFFF);
    Reg#(Bit#(16)) reg_wz <- mkReg(16'h0000);
    Reg#(Bit#(8))  reg_i  <- mkReg(8'h00);
    Reg#(Bit#(8))  reg_r  <- mkReg(8'h00);

    // Interrupt Controls
    Reg#(Bool)     iff1   <- mkReg(False);
    Reg#(Bool)     iff2   <- mkReg(False);
    Reg#(Bit#(2))  im     <- mkReg(0);
    Reg#(Bool)     halted <- mkReg(False);

    // Micro-Sequencer Control Registers
    Reg#(CpuState) state       <- mkReg(FetchOp);
    Reg#(Bit#(8))  opcode      <- mkReg(8'h00);
    Reg#(Bit#(8))  prefix      <- mkReg(8'h00); // 0=none, 0xCB, 0xED, 0xDD, 0xFD
    Reg#(Bit#(16)) temp_addr   <- mkReg(16'h0000);
    Reg#(Bit#(16)) temp_data16 <- mkReg(16'h0000);
    Reg#(Bit#(8))  temp_data8  <- mkReg(8'h00);
    Reg#(OpTarget) op_target   <- mkReg(TargetNone);
    Reg#(AluOp8)   alu_subop   <- mkReg(AluADD);
    RWire#(Bool)   retired_w   <- mkRWire;

    // Clock tick wires
    RWire#(Bool) tick_3m58_w <- mkRWire;
    RWire#(Bool) tick_7m_w   <- mkRWire;
    RWire#(Bool) tick_28m_w  <- mkRWire;

    // Determine if CPU should advance this master clock cycle
    Bool is_r800 = t9769c.is_r800();
    Bool clk_adv = is_r800 ? isValid(tick_7m_w.wget()) : isValid(tick_3m58_w.wget());

    // ── Register Pairing Helpers ────────────────────────────────────────────
    function Bit#(16) get_bc(); return {reg_b, reg_c}; endfunction
    function Bit#(16) get_de(); return {reg_d, reg_e}; endfunction
    function Bit#(16) get_hl(); return {reg_h, reg_l}; endfunction

    function Action set_bc(Bit#(16) v);
        action
            reg_b <= v[15:8];
            reg_c <= v[7:0];
        endaction
    endfunction

    function Action set_de(Bit#(16) v);
        action
            reg_d <= v[15:8];
            reg_e <= v[7:0];
        endaction
    endfunction

    function Action set_hl(Bit#(16) v);
        action
            reg_h <= v[15:8];
            reg_l <= v[7:0];
        endaction
    endfunction

    // ── Parameterized 8-bit Register Read / Write ───────────────────────────
    function Bit#(8) read_reg8(Reg8ID r);
        case (r)
            RegB: return reg_b;
            RegC: return reg_c;
            RegD: return reg_d;
            RegE: return reg_e;
            RegH: return reg_h;
            RegL: return reg_l;
            RegA: return reg_a;
            default: return 8'h00;
        endcase
    endfunction

    function Action write_reg8(Reg8ID r, Bit#(8) v);
        action
            case (r)
                RegB: reg_b <= v;
                RegC: reg_c <= v;
                RegD: reg_d <= v;
                RegE: reg_e <= v;
                RegH: reg_h <= v;
                RegL: reg_l <= v;
                RegA: reg_a <= v;
                default: noAction;
            endcase
        endaction
    endfunction

    // ── Parameterized 16-bit Register Pair Read / Write ─────────────────────
    function Bit#(16) read_reg16(Reg16ID r);
        case (r)
            PairBC: return get_bc();
            PairDE: return get_de();
            PairHL: return get_hl();
            PairSP: return reg_sp;
        endcase
    endfunction

    function Action write_reg16(Reg16ID r, Bit#(16) v);
        action
            case (r)
                PairBC: set_bc(v);
                PairDE: set_de(v);
                PairHL: set_hl(v);
                PairSP: reg_sp <= v;
            endcase
        endaction
    endfunction

    // ── Bus Request Helper ──────────────────────────────────────────────────
    function Action send_bus_req(BusAddr addr, BusData data, Bool write, Bool is_io);
        action
            q_req_out.enq(BusReq {
                tag:   tagCPU,
                addr:  addr,
                data:  data,
                write: write,
                is_io: is_io
            });
        endaction
    endfunction

    // ── Instruction Execution FSM ───────────────────────────────────────────
    rule cpu_fsm if (clk_adv);
        let dec = splitOpcode(opcode);

        case (state)
            // ── Opcode Fetch ────────────────────────────────────────────────
            FetchOp: begin
                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                state <= WaitOp;
            end

            WaitOp: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                opcode <= resp.data;
                reg_pc <= reg_pc + 1;
                reg_r  <= reg_r + 1;
                state  <= DecodeOp;
            end

            // ── Opcode Decode ───────────────────────────────────────────────
            DecodeOp: begin
                let d = dec;

                // ── Non-prefixed instructions ───────────────────────────────
                if (prefix == 8'h00) begin
                    case (d.x)
                        // ── x = 0: Relative jumps, 16-bit load/add, 8-bit inc/dec/ld imm
                        2'b00: begin
                            case (d.z)
                                // z = 0: NOP, EX AF,AF', DJNZ, JR, JR cc
                                3'b000: begin
                                    case (d.y)
                                        3'b000: begin // NOP
                                            retired_w.wset(True);
                                            state <= FetchOp;
                                        end
                                        3'b001: begin // EX AF, AF'
                                            reg_a  <= reg_a_; reg_a_ <= reg_a;
                                            reg_f  <= reg_f_; reg_f_ <= reg_f;
                                            retired_w.wset(True);
                                            state  <= FetchOp;
                                        end
                                        default: begin // JR / DJNZ
                                            send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                            op_target <= TargetJp;
                                            state <= WaitImm8;
                                        end
                                    endcase
                                end

                                // z = 1: 16-bit LD rp, nn (q=0) or ADD HL, rp (q=1)
                                3'b001: begin
                                    if (d.q == 1'b0) begin // LD rp, nn
                                        send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                        op_target <= case (d.p)
                                            2'b00: TargetPairBC;
                                            2'b01: TargetPairDE;
                                            2'b10: TargetPairHL;
                                            2'b11: TargetPairSP;
                                        endcase;
                                        state <= WaitImm16Lo;
                                    end else begin // ADD HL, rp
                                        let res16 = evalAdd16(get_hl(), read_reg16(unpack(d.p)), reg_f);
                                        set_hl(res16.res);
                                        reg_f <= res16.flags;
                                        retired_w.wset(True);
                                        state <= FetchOp;
                                    end
                                end

                                // z = 2: Indirect loads
                                3'b010: begin
                                    if (d.q == 1'b0) begin
                                        case (d.p)
                                            2'b00: begin // LD (BC), A
                                                send_bus_req(zeroExtend(get_bc()), reg_a, True, False);
                                                state <= WaitMemWrite8;
                                            end
                                            2'b01: begin // LD (DE), A
                                                send_bus_req(zeroExtend(get_de()), reg_a, True, False);
                                                state <= WaitMemWrite8;
                                            end
                                            default: begin
                                                retired_w.wset(True);
                                                state <= FetchOp;
                                            end
                                        endcase
                                    end else begin
                                        case (d.p)
                                            2'b00: begin // LD A, (BC)
                                                send_bus_req(zeroExtend(get_bc()), 8'h00, False, False);
                                                op_target <= TargetRegA;
                                                state <= WaitMemRead8;
                                            end
                                            2'b01: begin // LD A, (DE)
                                                send_bus_req(zeroExtend(get_de()), 8'h00, False, False);
                                                op_target <= TargetRegA;
                                                state <= WaitMemRead8;
                                            end
                                            default: begin
                                                retired_w.wset(True);
                                                state <= FetchOp;
                                            end
                                        endcase
                                    end
                                end

                                // z = 3: 16-bit INC / DEC
                                3'b011: begin
                                    let rp  = unpack(d.p);
                                    let val = read_reg16(rp);
                                    write_reg16(rp, d.q == 1'b0 ? val + 1 : val - 1);
                                    retired_w.wset(True);
                                    state <= FetchOp;
                                end

                                // z = 4: 8-bit INC r
                                3'b100: begin
                                    if (d.y == 3'b110) begin // INC (HL)
                                        send_bus_req(zeroExtend(get_hl()), 8'h00, False, False);
                                        op_target <= TargetMemHL;
                                        alu_subop <= AluADD;
                                        state <= WaitMemRead8;
                                    end else begin
                                        let r8 = unpack(d.y);
                                        let inc8 = evalInc8(read_reg8(r8), reg_f);
                                        write_reg8(r8, inc8.res);
                                        reg_f <= inc8.flags;
                                        retired_w.wset(True);
                                        state <= FetchOp;
                                    end
                                end

                                // z = 5: 8-bit DEC r
                                3'b101: begin
                                    if (d.y == 3'b110) begin // DEC (HL)
                                        send_bus_req(zeroExtend(get_hl()), 8'h00, False, False);
                                        op_target <= TargetMemHL;
                                        alu_subop <= AluSUB;
                                        state <= WaitMemRead8;
                                    end else begin
                                        let r8 = unpack(d.y);
                                        let dec8 = evalDec8(read_reg8(r8), reg_f);
                                        write_reg8(r8, dec8.res);
                                        reg_f <= dec8.flags;
                                        retired_w.wset(True);
                                        state <= FetchOp;
                                    end
                                end

                                // z = 6: LD r, n (8-bit immediate)
                                3'b110: begin
                                    send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                    op_target <= case (d.y)
                                        3'b000: TargetRegB;
                                        3'b001: TargetRegC;
                                        3'b010: TargetRegD;
                                        3'b011: TargetRegE;
                                        3'b100: TargetRegH;
                                        3'b101: TargetRegL;
                                        3'b110: TargetMemHL;
                                        3'b111: TargetRegA;
                                    endcase;
                                    state <= WaitImm8;
                                end

                                // z = 7: Rotates on A, DAA, CPL, SCF, CCF
                                3'b111: begin
                                    case (d.y)
                                        3'b000: begin // RLCA
                                            let rot = evalShift8(AluRLC, reg_a, reg_f.cf);
                                            reg_a <= rot.res;
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: rot.flags.yf,
                                                               hf: False, xf: rot.flags.xf, pf: reg_f.pf, nf: False, cf: rot.flags.cf };
                                        end
                                        3'b001: begin // RRCA
                                            let rot = evalShift8(AluRRC, reg_a, reg_f.cf);
                                            reg_a <= rot.res;
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: rot.flags.yf,
                                                               hf: False, xf: rot.flags.xf, pf: reg_f.pf, nf: False, cf: rot.flags.cf };
                                        end
                                        3'b010: begin // RLA
                                            let rot = evalShift8(AluRL, reg_a, reg_f.cf);
                                            reg_a <= rot.res;
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: rot.flags.yf,
                                                               hf: False, xf: rot.flags.xf, pf: reg_f.pf, nf: False, cf: rot.flags.cf };
                                        end
                                        3'b011: begin // RRA
                                            let rot = evalShift8(AluRR, reg_a, reg_f.cf);
                                            reg_a <= rot.res;
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: rot.flags.yf,
                                                               hf: False, xf: rot.flags.xf, pf: reg_f.pf, nf: False, cf: rot.flags.cf };
                                        end
                                        3'b100: begin // DAA
                                            // Decimal Adjust Accumulator
                                            Bit#(8) adj = 0;
                                            Bool cf_new = reg_f.cf;
                                            if (reg_f.hf || (reg_a[3:0] > 4'd9)) adj = adj | 8'h06;
                                            if (reg_f.cf || (reg_a > 8'h99)) begin
                                                adj = adj | 8'h60;
                                                cf_new = True;
                                            end
                                            Bit#(8) res = reg_f.nf ? (reg_a - adj) : (reg_a + adj);
                                            reg_a <= res;
                                            reg_f <= Z80Flags {
                                                sf: unpack(res[7]), zf: (res == 0), yf: unpack(res[5]),
                                                hf: unpack((reg_a[4] ^ res[4])), xf: unpack(res[3]),
                                                pf: calcParity(res), nf: reg_f.nf, cf: cf_new
                                            };
                                        end
                                        3'b101: begin // CPL
                                            let res = ~reg_a;
                                            reg_a <= res;
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: unpack(res[5]),
                                                               hf: True, xf: unpack(res[3]), pf: reg_f.pf, nf: True, cf: reg_f.cf };
                                        end
                                        3'b110: begin // SCF
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: unpack(reg_a[5]),
                                                               hf: False, xf: unpack(reg_a[3]), pf: reg_f.pf, nf: False, cf: True };
                                        end
                                        3'b111: begin // CCF
                                            reg_f <= Z80Flags { sf: reg_f.sf, zf: reg_f.zf, yf: unpack(reg_a[5]),
                                                               hf: reg_f.cf, xf: unpack(reg_a[3]), pf: reg_f.pf, nf: False, cf: !reg_f.cf };
                                        end
                                    endcase
                                    retired_w.wset(True);
                                    state <= FetchOp;
                                end
                            endcase
                        end

                        // ── x = 1: LD r[y], r[z] and HALT ───────────────────
                        2'b01: begin
                            if (d.y == 3'b110 && d.z == 3'b110) begin // HALT (0x76)
                                halted <= True;
                                retired_w.wset(True);
                                state  <= FetchOp;
                            end
                            else if (d.y == 3'b110) begin // LD (HL), r
                                send_bus_req(zeroExtend(get_hl()), read_reg8(unpack(d.z)), True, False);
                                state <= WaitMemWrite8;
                            end
                            else if (d.z == 3'b110) begin // LD r, (HL)
                                send_bus_req(zeroExtend(get_hl()), 8'h00, False, False);
                                op_target <= case (d.y)
                                    3'b000: TargetRegB;
                                    3'b001: TargetRegC;
                                    3'b010: TargetRegD;
                                    3'b011: TargetRegE;
                                    3'b100: TargetRegH;
                                    3'b101: TargetRegL;
                                    default: TargetRegA;
                                endcase;
                                state <= WaitMemRead8;
                            end
                            else begin // Register-to-register LD
                                write_reg8(unpack(d.y), read_reg8(unpack(d.z)));
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                        end

                        // ── x = 2: 8-bit ALU A, r[z] ────────────────────────
                        2'b10: begin
                            if (d.z == 3'b110) begin // ALU A, (HL)
                                send_bus_req(zeroExtend(get_hl()), 8'h00, False, False);
                                alu_subop <= unpack(d.y);
                                op_target <= TargetAluImm;
                                state <= WaitMemRead8;
                            end else begin
                                let alu_res = evalAlu8(unpack(d.y), reg_a, read_reg8(unpack(d.z)), reg_f.cf);
                                if (unpack(d.y) != AluCP) reg_a <= alu_res.res;
                                reg_f <= alu_res.flags;
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                        end

                        // ── x = 3: Jumps, Calls, Returns, Stack, I/O, Prefixes
                        2'b11: begin
                            case (d.z)
                                // z = 0: RET cc
                                3'b000: begin
                                    if (evalCondition(unpack(d.y), reg_f)) begin
                                        send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                                        reg_sp <= reg_sp + 1;
                                        state  <= PopLo;
                                    end else begin
                                        retired_w.wset(True);
                                        state <= FetchOp;
                                    end
                                end

                                // z = 1: POP rp2 / RET / EXX / JP (HL) / LD SP,HL
                                3'b001: begin
                                    if (d.q == 1'b0) begin // POP rp2
                                        send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                                        reg_sp <= reg_sp + 1;
                                        op_target <= case (d.p)
                                            2'b00: TargetPairBC;
                                            2'b01: TargetPairDE;
                                            2'b10: TargetPairHL;
                                            2'b11: TargetNone; // AF
                                        endcase;
                                        state <= PopLo;
                                    end else begin
                                        case (d.p)
                                            2'b00: begin // RET
                                                send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                                                reg_sp <= reg_sp + 1;
                                                state <= PopLo;
                                            end
                                            2'b01: begin // EXX
                                                reg_b <= reg_b_; reg_b_ <= reg_b;
                                                reg_c <= reg_c_; reg_c_ <= reg_c;
                                                reg_d <= reg_d_; reg_d_ <= reg_d;
                                                reg_e <= reg_e_; reg_e_ <= reg_e;
                                                reg_h <= reg_h_; reg_h_ <= reg_h;
                                                reg_l <= reg_l_; reg_l_ <= reg_l;
                                                retired_w.wset(True);
                                                state <= FetchOp;
                                            end
                                            2'b10: begin // JP (HL)
                                                reg_pc <= get_hl();
                                                retired_w.wset(True);
                                                state <= FetchOp;
                                            end
                                            2'b11: begin // LD SP, HL
                                                reg_sp <= get_hl();
                                                retired_w.wset(True);
                                                state <= FetchOp;
                                            end
                                        endcase
                                    end
                                end

                                // z = 2: JP cc, nn
                                3'b010: begin
                                    send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                    op_target <= evalCondition(unpack(d.y), reg_f) ? TargetJp : TargetNone;
                                    state <= WaitImm16Lo;
                                end

                                // z = 3: Absolute JP, OUT (n),A, IN A,(n), EX (SP),HL, EX DE,HL, DI, EI
                                3'b011: begin
                                    case (d.y)
                                        3'b000: begin // JP nn
                                            send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                            op_target <= TargetJp;
                                            state <= WaitImm16Lo;
                                        end
                                        3'b001: begin // CB prefix
                                            prefix <= 8'hCB;
                                            state  <= FetchOp;
                                        end
                                        3'b010: begin // OUT (n), A
                                            send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                            op_target <= TargetOutA;
                                            state <= WaitImm8;
                                        end
                                        3'b011: begin // IN A, (n)
                                            send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                            op_target <= TargetInA;
                                            state <= WaitImm8;
                                        end
                                        3'b100: begin // EX (SP), HL
                                            send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                                            state <= WaitMemRead8;
                                        end
                                        3'b101: begin // EX DE, HL
                                            reg_d <= reg_h; reg_h <= reg_d;
                                            reg_e <= reg_l; reg_l <= reg_e;
                                            retired_w.wset(True);
                                            state <= FetchOp;
                                        end
                                        3'b110: begin // DI
                                            iff1 <= False; iff2 <= False;
                                            retired_w.wset(True);
                                            state <= FetchOp;
                                        end
                                        3'b111: begin // EI
                                            iff1 <= True; iff2 <= True;
                                            retired_w.wset(True);
                                            state <= FetchOp;
                                        end
                                    endcase
                                end

                                // z = 4: CALL cc, nn
                                3'b100: begin
                                    send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                    op_target <= evalCondition(unpack(d.y), reg_f) ? TargetCall : TargetNone;
                                    state <= WaitImm16Lo;
                                end

                                // z = 5: PUSH rp2 / Prefixes (DD, ED, FD)
                                3'b101: begin
                                    if (d.q == 1'b0) begin // PUSH rp2
                                        Bit#(16) v = case (d.p)
                                            2'b00: get_bc();
                                            2'b01: get_de();
                                            2'b10: get_hl();
                                            2'b11: {reg_a, packFlags(reg_f)};
                                        endcase;
                                        reg_sp <= reg_sp - 1;
                                        send_bus_req(zeroExtend(reg_sp - 1), v[15:8], True, False);
                                        temp_data8 <= v[7:0];
                                        state <= PushHi;
                                    end else begin
                                        case (d.p)
                                            2'b00: begin // CALL nn
                                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                                op_target <= TargetCall;
                                                state <= WaitImm16Lo;
                                            end
                                            2'b01: begin // DD prefix (IX)
                                                prefix <= 8'hDD;
                                                state  <= FetchOp;
                                            end
                                            2'b10: begin // ED prefix
                                                prefix <= 8'hED;
                                                state  <= FetchOp;
                                            end
                                            2'b11: begin // FD prefix (IY)
                                                prefix <= 8'hFD;
                                                state  <= FetchOp;
                                            end
                                        endcase
                                    end
                                end

                                // z = 6: ALU A, n (immediate)
                                3'b110: begin
                                    send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                    alu_subop <= unpack(d.y);
                                    op_target <= TargetAluImm;
                                    state <= WaitImm8;
                                end

                                // z = 7: RST y*8
                                3'b111: begin
                                    reg_sp <= reg_sp - 1;
                                    send_bus_req(zeroExtend(reg_sp - 1), reg_pc[15:8], True, False);
                                    temp_data8 <= reg_pc[7:0];
                                    temp_addr  <= {10'd0, d.y, 3'd0};
                                    op_target  <= TargetCall;
                                    state      <= PushHi;
                                end
                            endcase
                        end
                    endcase
                end
                // ── Prefix ED (Extended & R800 Multipliers) ─────────────────
                else if (prefix == 8'hED) begin
                    prefix <= 8'h00; // Consume prefix

                    // R800 8x8 Hardware Multiplier: MULUB A, reg8
                    if (opcode == 8'hC1) begin mult.start_mulub(reg_a, reg_b); state <= ExecMul; end
                    else if (opcode == 8'hC9) begin mult.start_mulub(reg_a, reg_c); state <= ExecMul; end
                    else if (opcode == 8'hD1) begin mult.start_mulub(reg_a, reg_d); state <= ExecMul; end
                    else if (opcode == 8'hD9) begin mult.start_mulub(reg_a, reg_e); state <= ExecMul; end
                    else if (opcode == 8'hE1) begin mult.start_mulub(reg_a, reg_h); state <= ExecMul; end
                    else if (opcode == 8'hE9) begin mult.start_mulub(reg_a, reg_l); state <= ExecMul; end
                    else if (opcode == 8'hF1) begin mult.start_mulub(reg_a, reg_a); state <= ExecMul; end

                    // R800 16x16 Hardware Multiplier: MULUQ HL, reg16
                    else if (opcode == 8'hC3) begin mult.start_muluq(get_hl(), get_bc()); state <= ExecMul; end
                    else if (opcode == 8'hD3) begin mult.start_muluq(get_hl(), get_de()); state <= ExecMul; end
                    else if (opcode == 8'hE3) begin mult.start_muluq(get_hl(), get_hl()); state <= ExecMul; end
                    else if (opcode == 8'hF3) begin mult.start_muluq(get_hl(), reg_sp);   state <= ExecMul; end

                    // NEG
                    else if (opcode == 8'h44) begin
                        let alu_res = evalAlu8(AluSUB, 8'h00, reg_a, False);
                        reg_a <= alu_res.res;
                        reg_f <= alu_res.flags;
                        retired_w.wset(True);
                        state <= FetchOp;
                    end

                    // IM 0 / 1 / 2
                    else if (opcode == 8'h46) begin im <= 0; retired_w.wset(True); state <= FetchOp; end
                    else if (opcode == 8'h56) begin im <= 1; retired_w.wset(True); state <= FetchOp; end
                    else if (opcode == 8'h5E) begin im <= 2; retired_w.wset(True); state <= FetchOp; end

                    // LD I, A / LD R, A / LD A, I / LD A, R
                    else if (opcode == 8'h47) begin reg_i <= reg_a; retired_w.wset(True); state <= FetchOp; end
                    else if (opcode == 8'h4F) begin reg_r <= reg_a; retired_w.wset(True); state <= FetchOp; end
                    else if (opcode == 8'h57) begin
                        reg_a <= reg_i;
                        reg_f <= Z80Flags { sf: unpack(reg_i[7]), zf: (reg_i == 0), yf: unpack(reg_i[5]),
                                           hf: False, xf: unpack(reg_i[3]), pf: iff2, nf: False, cf: reg_f.cf };
                        retired_w.wset(True); state <= FetchOp;
                    end
                    else if (opcode == 8'h5F) begin
                        reg_a <= reg_r;
                        reg_f <= Z80Flags { sf: unpack(reg_r[7]), zf: (reg_r == 0), yf: unpack(reg_r[5]),
                                           hf: False, xf: unpack(reg_r[3]), pf: iff2, nf: False, cf: reg_f.cf };
                        retired_w.wset(True); state <= FetchOp;
                    end
                    else begin
                        retired_w.wset(True);
                        state <= FetchOp;
                    end
                end
                // ── Prefix CB (Bit Manipulations) ───────────────────────────
                else if (prefix == 8'hCB) begin
                    prefix <= 8'h00;
                    let r8 = unpack(d.z);
                    let val = read_reg8(r8);
                    case (d.x)
                        // Shift / Rotate
                        2'b00: begin
                            let s_res = evalShift8(unpack(d.y), val, reg_f.cf);
                            write_reg8(r8, s_res.res);
                            reg_f <= s_res.flags;
                        end
                        // BIT y, r
                        2'b01: begin
                            Bit#(1) bit_val = val[d.y];
                            reg_f <= Z80Flags {
                                sf: (d.y == 3'd7) && (bit_val == 1'b1),
                                zf: (bit_val == 1'b0),
                                yf: unpack(val[5]),
                                hf: True,
                                xf: unpack(val[3]),
                                pf: (bit_val == 1'b0),
                                nf: False,
                                cf: reg_f.cf
                            };
                        end
                        // RES y, r
                        2'b10: begin
                            Bit#(8) mask = ~(8'd1 << d.y);
                            write_reg8(r8, val & mask);
                        end
                        // SET y, r
                        2'b11: begin
                            Bit#(8) mask = (8'd1 << d.y);
                            write_reg8(r8, val | mask);
                        end
                    endcase
                    retired_w.wset(True);
                    state <= FetchOp;
                end
                else begin
                    // Other prefixes (DD, FD) fall-through
                    prefix <= 8'h00;
                    retired_w.wset(True);
                    state  <= FetchOp;
                end
            end

            // ── 8-bit Immediate Fetch ───────────────────────────────────────
            WaitImm8: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                let imm = resp.data;

                if (op_target == TargetJp) begin
                    // Relative jump JR e (signed 8-bit offset relative to PC after fetch)
                    Int#(8) offset = unpack(imm);
                    reg_pc <= unpack(pack(signExtend(offset) + unpack(pack(reg_pc + 1))));
                    retired_w.wset(True);
                    state <= FetchOp;
                end else begin
                    reg_pc <= reg_pc + 1;
                end

                case (op_target)
                    TargetRegB: write_reg8(RegB, imm);
                    TargetRegC: write_reg8(RegC, imm);
                    TargetRegD: write_reg8(RegD, imm);
                    TargetRegE: write_reg8(RegE, imm);
                    TargetRegH: write_reg8(RegH, imm);
                    TargetRegL: write_reg8(RegL, imm);
                    TargetRegA: write_reg8(RegA, imm);
                    TargetMemHL: begin
                        send_bus_req(zeroExtend(get_hl()), imm, True, False);
                        state <= WaitMemWrite8;
                    end
                    TargetOutA: begin
                        // I/O write OUT (n), A: check internal T9769C ports first
                        t9769c.io_write(imm, reg_a);
                        // Also broadcast on bus
                        send_bus_req(zeroExtend(imm), reg_a, True, True);
                        state <= WaitMemWrite8;
                    end
                    TargetInA: begin
                        // I/O read IN A, (n)
                        if (imm >= 8'hE4 && imm <= 8'hF4) begin
                            reg_a <= t9769c.io_read(imm);
                            retired_w.wset(True);
                            state <= FetchOp;
                        end else begin
                            send_bus_req(zeroExtend(imm), 8'h00, False, True);
                            op_target <= TargetRegA;
                            state <= WaitMemRead8;
                        end
                    end
                    TargetAluImm: begin
                        let alu_res = evalAlu8(alu_subop, reg_a, imm, reg_f.cf);
                        if (alu_subop != AluCP) reg_a <= alu_res.res;
                        reg_f <= alu_res.flags;
                        retired_w.wset(True);
                        state <= FetchOp;
                    end
                    default: noAction;
                endcase

                if (op_target != TargetMemHL && op_target != TargetOutA &&
                    op_target != TargetInA && op_target != TargetAluImm &&
                    op_target != TargetJp) begin
                    retired_w.wset(True);
                    state <= FetchOp;
                end
            end

            // ── 16-bit Immediate Fetch: Low Byte ────────────────────────────
            WaitImm16Lo: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                reg_pc <= reg_pc + 1;
                temp_data8 <= resp.data;
                send_bus_req(zeroExtend(reg_pc + 1), 8'h00, False, False);
                state <= WaitImm16Hi;
            end

            // ── 16-bit Immediate Fetch: High Byte ───────────────────────────
            WaitImm16Hi: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                Bit#(16) imm16 = {resp.data, temp_data8};

                if (op_target == TargetJp) begin
                    reg_pc <= imm16;
                end else begin
                    reg_pc <= reg_pc + 1;
                end

                case (op_target)
                    TargetPairBC: set_bc(imm16);
                    TargetPairDE: set_de(imm16);
                    TargetPairHL: set_hl(imm16);
                    TargetPairSP: reg_sp <= imm16;
                    TargetCall: begin
                        // Push return address (current PC) and jump to imm16
                        reg_sp <= reg_sp - 1;
                        send_bus_req(zeroExtend(reg_sp - 1), (reg_pc + 1)[15:8], True, False);
                        temp_data8 <= (reg_pc + 1)[7:0];
                        temp_addr  <= imm16;
                        state <= PushHi;
                    end
                    default: noAction;
                endcase

                if (op_target != TargetCall) begin
                    retired_w.wset(True);
                    state <= FetchOp;
                end
            end

            // ── Memory Read Completion ──────────────────────────────────────
            WaitMemRead8: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                let d = resp.data;

                case (op_target)
                    TargetRegA: write_reg8(RegA, d);
                    TargetRegB: write_reg8(RegB, d);
                    TargetRegC: write_reg8(RegC, d);
                    TargetRegD: write_reg8(RegD, d);
                    TargetRegE: write_reg8(RegE, d);
                    TargetRegH: write_reg8(RegH, d);
                    TargetRegL: write_reg8(RegL, d);
                    TargetAluImm: begin
                        let alu_res = evalAlu8(alu_subop, reg_a, d, reg_f.cf);
                        if (alu_subop != AluCP) reg_a <= alu_res.res;
                        reg_f <= alu_res.flags;
                    end
                    TargetMemHL: begin // For INC/DEC (HL)
                        let new_d = (alu_subop == AluADD) ? d + 1 : d - 1;
                        let flags = (alu_subop == AluADD) ? evalInc8(d, reg_f).flags : evalDec8(d, reg_f).flags;
                        reg_f <= flags;
                        send_bus_req(zeroExtend(get_hl()), new_d, True, False);
                        state <= WaitMemWrite8;
                    end
                    default: noAction;
                endcase

                if (op_target != TargetMemHL) begin
                    retired_w.wset(True);
                    state <= FetchOp;
                end
            end

            // ── Memory Write Completion ─────────────────────────────────────
            WaitMemWrite8: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                retired_w.wset(True);
                state <= FetchOp;
            end

            // ── Stack Push: Push High byte then Low byte ────────────────────
            PushHi: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                reg_sp <= reg_sp - 1;
                send_bus_req(zeroExtend(reg_sp - 1), temp_data8, True, False);
                state <= PushLo;
            end

            PushLo: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                if (op_target == TargetCall) reg_pc <= temp_addr;
                retired_w.wset(True);
                state <= FetchOp;
            end

            // ── Stack Pop: Pop Low byte then High byte ──────────────────────
            PopLo: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                temp_data8 <= resp.data;
                send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                reg_sp <= reg_sp + 1;
                state <= PopHi;
            end

            PopHi: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                Bit#(16) v = {resp.data, temp_data8};

                case (op_target)
                    TargetPairBC: set_bc(v);
                    TargetPairDE: set_de(v);
                    TargetPairHL: set_hl(v);
                    TargetNone: begin // POP AF or RET
                        if (opcode == 8'hC9 || dec.z == 3'b000) begin // RET
                            reg_pc <= v;
                        end else begin // POP AF
                            reg_a <= v[15:8];
                            reg_f <= unpackFlags(v[7:0]);
                        end
                    end
                    default: noAction;
                endcase

                retired_w.wset(True);
                state <= FetchOp;
            end

            // ── Wait for Hardware Multiplier ────────────────────────────────
            ExecMul: begin
                if (mult.done()) begin
                    let res   = mult.get_result();
                    let flags = mult.get_flags();
                    set_hl(res[15:0]);
                    set_de(res[31:16]);
                    reg_f       <= flags;
                    retired_w.wset(True);
                    state       <= FetchOp;
                end
            end

            default: begin
                retired_w.wset(True);
                state <= FetchOp;
            end
        endcase
    endrule

    // ── Clock Tick Enables ──────────────────────────────────────────────────
    method Action tick_28m();
        tick_28m_w.wset(True);
    endmethod

    method Action tick_7m();
        tick_7m_w.wset(True);
    endmethod

    method Action tick_3m58();
        tick_3m58_w.wset(True);
    endmethod

    method Action tick_e6();
        t9769c.tick_e6();
    endmethod

    // ── Arbiter Memory Bus Client Interface ─────────────────────────────────
    interface bus_req  = toGet(q_req_out);
    interface bus_resp = toPut(q_resp_in);

    // ── Architectural State Introspection ───────────────────────────────────
    method CPURegs get_regs();
        return CPURegs {
            pc: reg_pc,
            a:  reg_a,
            f:  packFlags(reg_f),
            b:  reg_b,
            c:  reg_c,
            d:  reg_d,
            e:  reg_e,
            h:  reg_h,
            l:  reg_l,
            ix: reg_ix,
            iy: reg_iy,
            sp: reg_sp
        };
    endmethod

    method Bool inst_retired() = isValid(retired_w.wget());
    method Bool is_r800_mode() = t9769c.is_r800();
    method Bit#(5) get_state() = pack(state);
    method Bit#(8) get_opcode() = opcode;
endmodule: mkCPUCore

endpackage: CPUCore
