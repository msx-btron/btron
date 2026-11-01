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
    RetireInst,
    // Absolute (nn) data phase, block transfer, IX/IY displacement, CB (HL)
    AbsReadLo,
    AbsReadHi,
    AbsWriteHi,
    // EX (SP),HL / EX (SP),IX / EX (SP),IY. SP itself does not move.
    ExRdLo,
    ExRdHi,
    ExWrLo,
    BlkSrc,
    BlkDst,
    IdxDisp,
    IdxCB,
    CbMem,
    InPort
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
    TargetAluImm,
    // LD A,(nn) / LD (nn),A and LD rr,(nn) / LD (nn),rr (HL, IX, IY, SP, BC, DE)
    TargetLdAInd,
    TargetStAInd,
    TargetLdPairInd,
    TargetStPairInd,
    TargetPairIX,
    TargetPairIY,
    TargetMemAbs,   // INC/DEC (IX+d) read-modify-write at temp_addr
    TargetStIndex,  // LD (IX+d), n
    TargetExSP      // EX (SP), rp — kept so a stale TargetRegA cannot swallow it
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

    // Level-sensitive maskable interrupt (MSX VDP INT, data bus pulled to 0xFF).
    method Action set_irq(Bool level);

    // Architectural state introspection
    (* always_ready *) method CPURegs get_regs();
    (* always_ready *) method Bool    inst_retired();
    (* always_ready *) method Bool    is_r800_mode();
    (* always_ready *) method Bool    is_dram_mode();
    (* always_ready *) method Bit#(6) get_state();
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
    // iff_delay: EI arms IFF only after the following instruction retires.
    Reg#(Bool)     iff1      <- mkReg(False);
    Reg#(Bool)     iff2      <- mkReg(False);
    Reg#(Bit#(2))  iff_delay <- mkReg(0);
    Reg#(Bit#(2))  im        <- mkReg(0);
    Reg#(Bool)     halted    <- mkReg(False);
    RWire#(Bool)   irq_w     <- mkRWire;

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

    // One T9769C write port per cycle. The CPU rule only posts the request.
    Wire#(Bool)    t_wr_en   <- mkDWire(False);
    Wire#(Bit#(8)) t_wr_port <- mkDWire(0);
    Wire#(Bit#(8)) t_wr_data <- mkDWire(0);

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

    // ind_sel: 0 HL, 1 IX, 2 IY, 3 SP, 4 BC, 5 DE
    Reg#(Bit#(3)) ind_sel <- mkReg(0);

    function Bit#(16) sel_pair(Bit#(3) s);
        case (s)
            3'd0: return get_hl();
            3'd1: return reg_ix;
            3'd2: return reg_iy;
            3'd3: return reg_sp;
            3'd4: return get_bc();
            3'd5: return get_de();
            default: return 16'h0000;
        endcase
    endfunction

    function Action set_sel_pair(Bit#(3) s, Bit#(16) v);
        action
            case (s)
                3'd0: set_hl(v);
                3'd1: reg_ix <= v;
                3'd2: reg_iy <= v;
                3'd3: reg_sp <= v;
                3'd4: set_bc(v);
                3'd5: set_de(v);
                default: noAction;
            endcase
        endaction
    endfunction

    // DD/FD replaces HL/(HL) with IX/IY/(IX+d). 8-bit IXH/IXL are not used
    // by C-BIOS. EX DE,HL (EB) is not an index op. CB under DD/FD is (IX+d).
    function Bool fn_index_mem(Bit#(8) op);
        DecodedOp d = splitOpcode(op);
        Bool cb  = (op == 8'hCB);
        Bool inc = (d.x == 2'b00 && d.y == 3'b110 &&
                    (d.z == 3'b100 || d.z == 3'b101 || d.z == 3'b110));
        Bool ld  = (d.x == 2'b01 && (d.y == 3'b110 || d.z == 3'b110));
        Bool alu = (d.x == 2'b10 && d.z == 3'b110);
        return cb || inc || ld || alu;
    endfunction

    function Bool fn_index_pair(Bit#(8) op);
        DecodedOp d = splitOpcode(op);
        Bool add = (d.x == 2'b00 && d.z == 3'b001 && d.q == 1'b1);
        Bool ops = (op == 8'h21 || op == 8'h22 || op == 8'h2A ||
                    op == 8'h23 || op == 8'h2B ||
                    op == 8'hE1 || op == 8'hE3 || op == 8'hE5 ||
                    op == 8'hE9 || op == 8'hF9);
        return add || ops;
    endfunction

    function Bool fn_index_op(Bit#(8) op);
        return fn_index_mem(op) || fn_index_pair(op);
    endfunction

    function Z80Flags fn_in_flags(Bit#(8) val, Z80Flags oldf);
        return Z80Flags {
            sf: unpack(val[7]), zf: (val == 0), yf: unpack(val[5]),
            hf: False, xf: unpack(val[3]), pf: calcParity(val),
            nf: False, cf: oldf.cf
        };
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

    // EX (SP), rp. temp_addr keeps the register we write back. The value
    // read from the stack replaces that register. SP is left alone.
    function Action begin_ex_sp(Bit#(16) oldv, Bit#(3) sel);
        action
            temp_addr <= oldv;
            ind_sel   <= sel;
            op_target <= TargetExSP;
            send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
            state <= ExRdLo;
        endaction
    endfunction

    rule apply_t9769c_write (t_wr_en);
        t9769c.io_write(t_wr_port, t_wr_data);
    endrule

    // ── Instruction Execution FSM ───────────────────────────────────────────
    rule cpu_fsm if (clk_adv);
        let dec = splitOpcode(opcode);

        case (state)
            // ── Opcode Fetch / HALT / maskable interrupt ───────────────────
            // The VDP pulls the interrupt bus to 0xFF, so IM 0 executes
            // RST 38h and IM 1 jumps to the same vector. Push PC, then 0x0038.
            // EI opens IFF on the fetch of the following instruction; the
            // line is sampled on the fetch after that instruction retires.
            FetchOp: begin
                Bool irq = fromMaybe(False, irq_w.wget());
                if (iff_delay != 0) begin
                    iff_delay <= iff_delay - 1;
                    if (iff_delay == 1) begin
                        iff1 <= True;
                        iff2 <= True;
                    end
                    if (halted) begin
                        reg_r <= reg_r + 1;
                    end else begin
                        send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                        state <= WaitOp;
                    end
                end else if (irq && iff1) begin
                    halted    <= False;
                    iff1      <= False;
                    iff2      <= False;
                    // A prefix that has not yet fetched its opcode is discarded,
                    // the same way a Z80 interrupt response drops it. Otherwise
                    // the byte at $0038 (JP) runs as ED C3, the R800 multiply.
                    prefix    <= 8'h00;
                    reg_r     <= reg_r + 1;
                    reg_sp    <= reg_sp - 1;
                    send_bus_req(zeroExtend(reg_sp - 1), reg_pc[15:8], True, False);
                    temp_data8 <= reg_pc[7:0];
                    temp_addr  <= 16'h0038;
                    op_target  <= TargetCall;
                    state      <= PushHi;
                end else if (halted) begin
                    reg_r <= reg_r + 1;
                end else begin
                    send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                    state <= WaitOp;
                end
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

                // ── Unprefixed, or a DD/FD that does not touch IX/IY ──────
                // A DD/FD prefix on an unrelated opcode is a 4-cycle no-op.
                // The prefixed opcode itself still executes.
                Bool idx    = (prefix == 8'hDD || prefix == 8'hFD);
                Bool idx_op = idx && fn_index_op(opcode);
                if (prefix == 8'h00 || (idx && !idx_op)) begin
                    if (idx && opcode != 8'hCB && opcode != 8'hED &&
                        opcode != 8'hDD && opcode != 8'hFD)
                        prefix <= 8'h00;
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
                                        default: begin // DJNZ / JR e / JR cc,e
                                            // Always read the displacement. A jump that
                                            // is not taken leaves TargetNone, so WaitImm8
                                            // steps over the byte and falls through.
                                            // DJNZ decrements B and jumps when the new B
                                            // is not zero. JR cc uses the same flags as JP cc.
                                            Bool take = case (d.y)
                                                3'b010: ((reg_b - 1) != 0);                 // DJNZ
                                                3'b011: True;                               // JR e
                                                3'b100: evalCondition(CondNZ, reg_f);       // JR NZ
                                                3'b101: evalCondition(CondZ,  reg_f);       // JR Z
                                                3'b110: evalCondition(CondNC, reg_f);       // JR NC
                                                3'b111: evalCondition(CondC,  reg_f);       // JR C
                                                default: True;
                                            endcase;
                                            if (d.y == 3'b010) reg_b <= reg_b - 1;
                                            send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                            op_target <= take ? TargetJp : TargetNone;
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

                                // z = 2: Indirect loads.
                                // 22/2A/32/3A used to retire as a one-byte NOP, so the
                                // operand of LD A,($FFFF) (3A FF FF) executed as RST 38h
                                // and entered keyint during CHKRAM with IFF clear.
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
                                            2'b10: begin // LD (nn), HL
                                                ind_sel <= 3'd0;
                                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                                op_target <= TargetStPairInd;
                                                state <= WaitImm16Lo;
                                            end
                                            2'b11: begin // LD (nn), A
                                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                                op_target <= TargetStAInd;
                                                state <= WaitImm16Lo;
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
                                            2'b10: begin // LD HL, (nn)
                                                ind_sel <= 3'd0;
                                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                                op_target <= TargetLdPairInd;
                                                state <= WaitImm16Lo;
                                            end
                                            2'b11: begin // LD A, (nn)
                                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                                op_target <= TargetLdAInd;
                                                state <= WaitImm16Lo;
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
                                        // RET must not inherit a stale TargetCall from the CALL.
                                        op_target <= TargetNone;
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
                                                op_target <= TargetNone;
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
                                            begin_ex_sp(get_hl(), 3'd0);
                                        end
                                        3'b101: begin // EX DE, HL
                                            reg_d <= reg_h; reg_h <= reg_d;
                                            reg_e <= reg_l; reg_l <= reg_e;
                                            retired_w.wset(True);
                                            state <= FetchOp;
                                        end
                                        3'b110: begin // DI
                                            iff1 <= False; iff2 <= False;
                                            iff_delay <= 0;
                                            retired_w.wset(True);
                                            state <= FetchOp;
                                        end
                                        3'b111: begin // EI
                                            // IFF1 opens after the next instruction.
                                            iff_delay <= 1;
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
                                        // A leftover TargetCall would reload PC on PushLo.
                                        op_target <= TargetNone;
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

                    // RETN (ED 45) and RETI (ED 4D).
                    // $0066 is JP $1A2A, and $1A2D is RETN. A NOP there
                    // falls into the keyboard scanner and leaves the
                    // interrupt frame on the stack. IFF1 is restored
                    // from IFF2; this machine has no interrupt daisy.
                    else if (opcode == 8'h45 || opcode == 8'h4D) begin
                        iff1 <= iff2;
                        op_target <= TargetNone;
                        send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                        reg_sp <= reg_sp + 1;
                        state <= PopLo;
                    end

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
                    // LDI / LDD / LDIR / LDDR and OUTI family: read (HL) first.
                    // INI family: B is the counter, port is C.
                    else if (opcode == 8'hA0 || opcode == 8'hA8 ||
                             opcode == 8'hB0 || opcode == 8'hB8 ||
                             opcode == 8'hA3 || opcode == 8'hAB ||
                             opcode == 8'hB3 || opcode == 8'hBB) begin
                        send_bus_req(zeroExtend(get_hl()), 8'h00, False, False);
                        state <= BlkSrc;
                    end
                    else if (opcode == 8'hA2 || opcode == 8'hAA ||
                             opcode == 8'hB2 || opcode == 8'hBA) begin
                        reg_b <= reg_b - 1;
                        send_bus_req(zeroExtend(reg_c), 8'h00, False, True);
                        state <= BlkSrc;
                    end
                    // IN r,(C) / OUT (C),r   (y == 6 is IN (C) / OUT (C),0)
                    else if (d.x == 2'b01 && d.z == 3'b000) begin
                        // IN r,(C). VDP and PPI ports come back on the bus.
                        // E4-F4 are read by IN A,(n), which is what CHGCPU uses.
                        send_bus_req(zeroExtend(reg_c), 8'h00, False, True);
                        state <= InPort;
                    end
                    else if (d.x == 2'b01 && d.z == 3'b001) begin
                        Bit#(8) val = (d.y == 3'b110) ? 8'h00 : read_reg8(unpack(d.y));
                        t_wr_en <= True;
                        t_wr_port <= reg_c;
                        t_wr_data <= val;
                        send_bus_req(zeroExtend(reg_c), val, True, True);
                        state <= WaitMemWrite8;
                    end
                    // SBC HL,ss (ED 42/52/62/72) and ADC HL,ss (ED 4A/5A/6A/7A).
                    // Bad Apple's line divide is ADD HL,DE / CCF / SBC HL,DE.
                    // A missing SBC left HL unchanged, so a divisor >= $8000
                    // carried on every add and the divide never returned.
                    else if ((opcode & 8'hCF) == 8'h42 || (opcode & 8'hCF) == 8'h4A) begin
                        Bit#(16) ss = case (opcode[5:4])
                            2'b00: get_bc();
                            2'b01: get_de();
                            2'b10: get_hl();
                            default: reg_sp;
                        endcase;
                        let res16 = ((opcode & 8'hCF) == 8'h42)
                            ? evalSbc16(get_hl(), ss, reg_f.cf)
                            : evalAdc16(get_hl(), ss, reg_f.cf);
                        set_hl(res16.res);
                        reg_f <= res16.flags;
                        retired_w.wset(True);
                        state <= FetchOp;
                    end
                    else if (opcode == 8'h43 || opcode == 8'h53 || opcode == 8'h73 ||
                             opcode == 8'h4B || opcode == 8'h5B || opcode == 8'h7B) begin
                        ind_sel <= case (opcode)
                            8'h43, 8'h4B: 3'd4; // BC
                            8'h53, 8'h5B: 3'd5; // DE
                            default:      3'd3; // SP
                        endcase;
                        send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                        op_target <= (opcode[3] == 1'b1) ? TargetLdPairInd : TargetStPairInd;
                        state <= WaitImm16Lo;
                    end
                    else begin
                        retired_w.wset(True);
                        state <= FetchOp;
                    end
                end
                // ── Prefix CB (Bit Manipulations) ───────────────────────────
                else if (prefix == 8'hCB) begin
                    prefix <= 8'h00;
                    if (d.z == 3'b110) begin
                        temp_addr <= get_hl();
                        send_bus_req(zeroExtend(get_hl()), 8'h00, False, False);
                        state <= CbMem;
                    end else begin
                        let r8 = unpack(d.z);
                        let val = read_reg8(r8);
                        case (d.x)
                            2'b00: begin
                                let s_res = evalShift8(unpack(d.y), val, reg_f.cf);
                                write_reg8(r8, s_res.res);
                                reg_f <= s_res.flags;
                            end
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
                            2'b10: begin
                                Bit#(8) mask = ~(8'd1 << d.y);
                                write_reg8(r8, val & mask);
                            end
                            2'b11: begin
                                Bit#(8) mask = (8'd1 << d.y);
                                write_reg8(r8, val | mask);
                            end
                        endcase
                        retired_w.wset(True);
                        state <= FetchOp;
                    end
                end
                else begin
                    // DD/FD index operation. Displacement forms fetch d first.
                    Bit#(16) ixv = (prefix == 8'hFD) ? reg_iy : reg_ix;
                    if (fn_index_mem(opcode)) begin
                        send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                        state <= IdxDisp;
                    end else begin
                        prefix <= 8'h00;
                        case (opcode)
                            8'h21: begin // LD IX/IY, nn
                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                op_target <= (prefix == 8'hFD) ? TargetPairIY : TargetPairIX;
                                state <= WaitImm16Lo;
                            end
                            8'h22: begin // LD (nn), IX/IY
                                ind_sel <= (prefix == 8'hFD) ? 3'd2 : 3'd1;
                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                op_target <= TargetStPairInd;
                                state <= WaitImm16Lo;
                            end
                            8'h2A: begin // LD IX/IY, (nn)
                                ind_sel <= (prefix == 8'hFD) ? 3'd2 : 3'd1;
                                send_bus_req(zeroExtend(reg_pc), 8'h00, False, False);
                                op_target <= TargetLdPairInd;
                                state <= WaitImm16Lo;
                            end
                            8'h23: begin
                                if (prefix == 8'hFD) reg_iy <= ixv + 1;
                                else reg_ix <= ixv + 1;
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                            8'h2B: begin
                                if (prefix == 8'hFD) reg_iy <= ixv - 1;
                                else reg_ix <= ixv - 1;
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                            8'h09, 8'h19, 8'h29, 8'h39: begin
                                Bit#(16) ss = case (opcode[5:4])
                                    2'b00: get_bc();
                                    2'b01: get_de();
                                    2'b10: ixv;
                                    2'b11: reg_sp;
                                endcase;
                                let radd = evalAdd16(ixv, ss, reg_f);
                                if (prefix == 8'hFD) reg_iy <= radd.res;
                                else reg_ix <= radd.res;
                                reg_f <= radd.flags;
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                            8'hE5: begin // PUSH IX/IY
                                reg_sp <= reg_sp - 1;
                                send_bus_req(zeroExtend(reg_sp - 1), ixv[15:8], True, False);
                                temp_data8 <= ixv[7:0];
                                op_target <= TargetNone;
                                state <= PushHi;
                            end
                            8'hE1: begin // POP IX/IY
                                send_bus_req(zeroExtend(reg_sp), 8'h00, False, False);
                                reg_sp <= reg_sp + 1;
                                op_target <= (prefix == 8'hFD) ? TargetPairIY : TargetPairIX;
                                state <= PopLo;
                            end
                            8'hE3: begin // EX (SP), IX/IY
                                begin_ex_sp(ixv, (prefix == 8'hFD) ? 3'd2 : 3'd1);
                            end
                            8'hE9: begin // JP (IX/IY)
                                reg_pc <= ixv;
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                            8'hF9: begin // LD SP, IX/IY
                                reg_sp <= ixv;
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                            default: begin
                                retired_w.wset(True);
                                state <= FetchOp;
                            end
                        endcase
                    end
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
                        // I/O write OUT (n), A: internal T9769C and the external bus.
                        t_wr_en <= True;
                        t_wr_port <= imm;
                        t_wr_data <= reg_a;
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
                    TargetStIndex: begin
                        send_bus_req(zeroExtend(temp_addr), imm, True, False);
                        state <= WaitMemWrite8;
                    end
                    default: noAction;
                endcase

                if (op_target != TargetMemHL && op_target != TargetOutA &&
                    op_target != TargetInA && op_target != TargetAluImm &&
                    op_target != TargetStIndex &&
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
                    TargetPairIX: reg_ix <= imm16;
                    TargetPairIY: reg_iy <= imm16;
                    TargetLdAInd: begin
                        send_bus_req(zeroExtend(imm16), 8'h00, False, False);
                        op_target <= TargetRegA;
                        state <= WaitMemRead8;
                    end
                    TargetStAInd: begin
                        send_bus_req(zeroExtend(imm16), reg_a, True, False);
                        state <= WaitMemWrite8;
                    end
                    TargetLdPairInd: begin
                        temp_addr <= imm16;
                        send_bus_req(zeroExtend(imm16), 8'h00, False, False);
                        state <= AbsReadLo;
                    end
                    TargetStPairInd: begin
                        temp_addr  <= imm16;
                        temp_data8 <= sel_pair(ind_sel)[15:8];
                        send_bus_req(zeroExtend(imm16), sel_pair(ind_sel)[7:0], True, False);
                        state <= AbsWriteHi;
                    end
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

                if (op_target != TargetCall &&
                    op_target != TargetLdAInd && op_target != TargetStAInd &&
                    op_target != TargetLdPairInd && op_target != TargetStPairInd) begin
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
                    TargetMemAbs: begin // INC/DEC (IX+d)
                        let new_d = (alu_subop == AluADD) ? d + 1 : d - 1;
                        let flags = (alu_subop == AluADD) ? evalInc8(d, reg_f).flags : evalDec8(d, reg_f).flags;
                        reg_f <= flags;
                        send_bus_req(zeroExtend(temp_addr), new_d, True, False);
                        state <= WaitMemWrite8;
                    end
                    default: noAction;
                endcase

                if (op_target != TargetMemHL && op_target != TargetMemAbs) begin
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
                    TargetPairIX: reg_ix <= v;
                    TargetPairIY: reg_iy <= v;
                    TargetNone: begin // POP AF or RET
                        // ED 45 / ED 4D are RETN / RETI. Their z field is not 0,
                        // so the RET cc test does not see them.
                        if (opcode == 8'hC9 || opcode == 8'h45 || opcode == 8'h4D ||
                            dec.z == 3'b000) begin // RET
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

            // ── LD rr,(nn) / LD (nn),rr data phase ──────────────────────
            AbsReadLo: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                temp_data8 <= resp.data;
                send_bus_req(zeroExtend(temp_addr + 1), 8'h00, False, False);
                state <= AbsReadHi;
            end

            AbsReadHi: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                set_sel_pair(ind_sel, {resp.data, temp_data8});
                retired_w.wset(True);
                state <= FetchOp;
            end

            // EX (SP), rp: read lo, read hi, write old lo, write old hi.
            ExRdLo: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                temp_data8 <= resp.data;
                send_bus_req(zeroExtend(reg_sp + 1), 8'h00, False, False);
                state <= ExRdHi;
            end

            ExRdHi: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                set_sel_pair(ind_sel, {resp.data, temp_data8});
                reg_wz <= {resp.data, temp_data8};
                send_bus_req(zeroExtend(reg_sp), temp_addr[7:0], True, False);
                state <= ExWrLo;
            end

            ExWrLo: begin
                q_resp_in.deq;
                send_bus_req(zeroExtend(reg_sp + 1), temp_addr[15:8], True, False);
                state <= WaitMemWrite8;
            end

            AbsWriteHi: begin
                q_resp_in.deq;
                send_bus_req(zeroExtend(temp_addr + 1), temp_data8, True, False);
                state <= WaitMemWrite8;
            end

            // ── LDI/LDIR/LDD/LDDR, INI/INIR, OUTI/OTIR ──────────────────────
            BlkSrc: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                Bool is_ld = (opcode == 8'hA0 || opcode == 8'hA8 ||
                              opcode == 8'hB0 || opcode == 8'hB8);
                Bool is_out = (opcode == 8'hA3 || opcode == 8'hAB ||
                               opcode == 8'hB3 || opcode == 8'hBB);
                if (is_ld) begin
                    temp_data8 <= resp.data;
                    send_bus_req(zeroExtend(get_de()), resp.data, True, False);
                    state <= BlkDst;
                end else if (is_out) begin
                    reg_b <= reg_b - 1;
                    t_wr_en <= True;
                    t_wr_port <= reg_c;
                    t_wr_data <= resp.data;
                    send_bus_req(zeroExtend(reg_c), resp.data, True, True);
                    state <= BlkDst;
                end else begin
                    send_bus_req(zeroExtend(get_hl()), resp.data, True, False);
                    state <= BlkDst;
                end
            end

            BlkDst: begin
                q_resp_in.deq;
                Bool is_ld = (opcode == 8'hA0 || opcode == 8'hA8 ||
                              opcode == 8'hB0 || opcode == 8'hB8);
                Bool down = (opcode == 8'hA8 || opcode == 8'hB8 ||
                             opcode == 8'hAA || opcode == 8'hBA ||
                             opcode == 8'hAB || opcode == 8'hBB);
                Bit#(16) step = down ? 16'hFFFF : 16'h0001;
                if (is_ld) begin
                    set_hl(get_hl() + step);
                    set_de(get_de() + step);
                    Bit#(16) bc1 = get_bc() - 1;
                    set_bc(bc1);
                    Bit#(8) n = temp_data8 + reg_a;
                    reg_f <= Z80Flags {
                        sf: reg_f.sf, zf: reg_f.zf,
                        yf: unpack(n[1]), hf: False, xf: unpack(n[3]),
                        pf: (bc1 != 0), nf: False, cf: reg_f.cf
                    };
                    if ((opcode == 8'hB0 || opcode == 8'hB8) && bc1 != 0)
                        reg_pc <= reg_pc - 2;
                end else begin
                    set_hl(get_hl() + step);
                    reg_f <= Z80Flags {
                        sf: reg_f.sf, zf: (reg_b == 0),
                        yf: reg_f.yf, hf: False, xf: reg_f.xf,
                        pf: reg_f.pf, nf: True, cf: reg_f.cf
                    };
                    Bool rep = (opcode == 8'hB2 || opcode == 8'hBA ||
                                opcode == 8'hB3 || opcode == 8'hBB);
                    if (rep && reg_b != 0)
                        reg_pc <= reg_pc - 2;
                end
                retired_w.wset(True);
                state <= FetchOp;
            end

            // ── DD/FD displacement byte ─────────────────────────────────────
            IdxDisp: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                Bit#(16) base = (prefix == 8'hFD) ? reg_iy : reg_ix;
                Bit#(16) addr = base + signExtend(resp.data);
                temp_addr <= addr;
                reg_pc <= reg_pc + 1;
                prefix <= 8'h00;
                let id = splitOpcode(opcode);
                if (opcode == 8'h76) begin
                    retired_w.wset(True);
                    state <= FetchOp;
                end else if (opcode == 8'hCB) begin
                    send_bus_req(zeroExtend(reg_pc + 1), 8'h00, False, False);
                    state <= IdxCB;
                end else if (opcode == 8'h36) begin
                    send_bus_req(zeroExtend(reg_pc + 1), 8'h00, False, False);
                    op_target <= TargetStIndex;
                    state <= WaitImm8;
                end else if (opcode == 8'h34 || opcode == 8'h35) begin
                    send_bus_req(zeroExtend(addr), 8'h00, False, False);
                    op_target <= TargetMemAbs;
                    alu_subop <= (opcode == 8'h34) ? AluADD : AluSUB;
                    state <= WaitMemRead8;
                end else if (id.x == 2'b01 && id.z == 3'b110) begin
                    send_bus_req(zeroExtend(addr), 8'h00, False, False);
                    op_target <= case (id.y)
                        3'b000: TargetRegB;
                        3'b001: TargetRegC;
                        3'b010: TargetRegD;
                        3'b011: TargetRegE;
                        3'b100: TargetRegH;
                        3'b101: TargetRegL;
                        default: TargetRegA;
                    endcase;
                    state <= WaitMemRead8;
                end else if (id.x == 2'b01 && id.y == 3'b110) begin
                    send_bus_req(zeroExtend(addr), read_reg8(unpack(id.z)), True, False);
                    state <= WaitMemWrite8;
                end else if (id.x == 2'b10 && id.z == 3'b110) begin
                    send_bus_req(zeroExtend(addr), 8'h00, False, False);
                    alu_subop <= unpack(id.y);
                    op_target <= TargetAluImm;
                    state <= WaitMemRead8;
                end else begin
                    retired_w.wset(True);
                    state <= FetchOp;
                end
            end

            IdxCB: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                opcode <= resp.data;
                reg_pc <= reg_pc + 1;
                send_bus_req(zeroExtend(temp_addr), 8'h00, False, False);
                state <= CbMem;
            end

            // CB operation on (HL) or (IX+d). Address is temp_addr.
            CbMem: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                let cd = splitOpcode(opcode);
                Bit#(8) val = resp.data;
                let sh = evalShift8(unpack(cd.y), val, reg_f.cf);
                Bit#(8) res = case (cd.x)
                    2'b00: sh.res;
                    2'b01: val;
                    2'b10: (val & ~(8'd1 << cd.y));
                    2'b11: (val | (8'd1 << cd.y));
                endcase;
                if (cd.x == 2'b00) reg_f <= sh.flags;
                else if (cd.x == 2'b01) begin
                    Bit#(1) bit_val = val[cd.y];
                    reg_f <= Z80Flags {
                        sf: (cd.y == 3'd7) && (bit_val == 1'b1),
                        zf: (bit_val == 1'b0),
                        yf: unpack(val[5]),
                        hf: True,
                        xf: unpack(val[3]),
                        pf: (bit_val == 1'b0),
                        nf: False,
                        cf: reg_f.cf
                    };
                end
                if (cd.x == 2'b01) begin
                    retired_w.wset(True);
                    state <= FetchOp;
                end else begin
                    send_bus_req(zeroExtend(temp_addr), res, True, False);
                    state <= WaitMemWrite8;
                end
            end

            // IN r,(C) bus response
            InPort: begin
                let resp = q_resp_in.first;
                q_resp_in.deq;
                let id = splitOpcode(opcode);
                if (id.y != 3'b110) write_reg8(unpack(id.y), resp.data);
                reg_f <= fn_in_flags(resp.data, reg_f);
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

    method Action set_irq(Bool level);
        irq_w.wset(level);
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
    method Bool is_dram_mode() = t9769c.is_dram_mode();
    method Bit#(6) get_state() = pack(state);
    method Bit#(8) get_opcode() = opcode;
endmodule: mkCPUCore

endpackage: CPUCore
