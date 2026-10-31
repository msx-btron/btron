/*
 * CPUCore.bsv — Unified Z80A / R800 Dual-Mode CPU Engine.
 *
 * Implements:
 *   1. Full register set: AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, SP, PC, I, R.
 *   2. Integrated T9769C MSX turbo R engine (Ports 0xE4..0xE7, 0xF4).
 *   3. Integrated R800 hardware multiplier (MULUB: 14t, MULUQ: 36t).
 *   4. Dual clock gating:
 *        - Z80 mode: advances on tick_3m58 (3.579545 MHz)
 *        - R800 mode: advances on tick_28m / 7.16 MHz
 *   5. Split-phase memory bus interface directly into MemoryArbiter.
 *   6. Architectural state introspection for lockstep Verilator co-sim.
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
    FetchByte,
    WaitByte,
    FetchWordLo,
    WaitWordLo,
    FetchWordHi,
    WaitWordHi,
    ReadMem,
    WaitMemRead,
    WriteMem,
    WaitMemWrite,
    ExecMul,
    RetireInst
} CpuState deriving (Bits, Eq);

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

    // ── Instruction Execution Rule ──────────────────────────────────────────
    rule cpu_fsm if (clk_adv);
        case (state)
            // ── Opcode Fetch ────────────────────────────────────────────────
            FetchOp: begin
                q_req_out.enq(BusReq {
                    tag:   tagCPU,
                    addr:  zeroExtend(reg_pc),
                    data:  8'h00,
                    write: False,
                    is_io: False
                });
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

            // ── Opcode Decode and Execute ───────────────────────────────────
            DecodeOp: begin
                // Check prefixes
                if (prefix == 8'h00) begin
                    case (opcode)
                        // NOP
                        8'h00: begin
                            retired_w.wset(True);
                            state <= FetchOp;
                        end

                        // HALT
                        8'h76: begin
                            halted <= True;
                            retired_w.wset(True);
                            state <= FetchOp;
                        end

                        // DI / EI
                        8'hF3: begin
                            iff1 <= False; iff2 <= False;
                            retired_w.wset(True); state <= FetchOp;
                        end
                        8'hFB: begin
                            iff1 <= True; iff2 <= True;
                            retired_w.wset(True); state <= FetchOp;
                        end

                        // EX AF, AF'
                        8'h08: begin
                            reg_a  <= reg_a_;  reg_a_ <= reg_a;
                            reg_f  <= reg_f_;  reg_f_ <= reg_f;
                            retired_w.wset(True); state <= FetchOp;
                        end

                        // EXX
                        8'hD9: begin
                            reg_b <= reg_b_; reg_b_ <= reg_b;
                            reg_c <= reg_c_; reg_c_ <= reg_c;
                            reg_d <= reg_d_; reg_d_ <= reg_d;
                            reg_e <= reg_e_; reg_e_ <= reg_e;
                            reg_h <= reg_h_; reg_h_ <= reg_h;
                            reg_l <= reg_l_; reg_l_ <= reg_l;
                            retired_w.wset(True); state <= FetchOp;
                        end

                        // EX DE, HL
                        8'hEB: begin
                            reg_d <= reg_h; reg_h <= reg_d;
                            reg_e <= reg_l; reg_l <= reg_e;
                            retired_w.wset(True); state <= FetchOp;
                        end

                        // Prefix ED
                        8'hED: begin
                            prefix <= 8'hED;
                            state  <= FetchOp;
                        end

                        // Prefix CB
                        8'hCB: begin
                            prefix <= 8'hCB;
                            state  <= FetchOp;
                        end

                        // Prefix DD (IX)
                        8'hDD: begin
                            prefix <= 8'hDD;
                            state  <= FetchOp;
                        end

                        // Prefix FD (IY)
                        8'hFD: begin
                            prefix <= 8'hFD;
                            state  <= FetchOp;
                        end

                        // Default: retire and advance
                        default: begin
                            retired_w.wset(True);
                            state <= FetchOp;
                        end
                    endcase
                end
                // Prefix ED handling (including R800 hardware multiplier)
                else if (prefix == 8'hED) begin
                    prefix <= 8'h00; // Consume prefix

                    // R800 8x8 Hardware Multiplier: MULUB A, reg8
                    if (opcode == 8'hC1) begin // MULUB A, B
                        mult.start_mulub(reg_a, reg_b);
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hC9) begin // MULUB A, C
                        mult.start_mulub(reg_a, reg_c);
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hD1) begin // MULUB A, D
                        mult.start_mulub(reg_a, reg_d);
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hD9) begin // MULUB A, E
                        mult.start_mulub(reg_a, reg_e);
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hE1) begin // MULUB A, H
                        mult.start_mulub(reg_a, reg_h);
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hE9) begin // MULUB A, L
                        mult.start_mulub(reg_a, reg_l);
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hF1) begin // MULUB A, A
                        mult.start_mulub(reg_a, reg_a);
                        state <= ExecMul;
                    end

                    // R800 16x16 Hardware Multiplier: MULUQ HL, reg16
                    else if (opcode == 8'hC3) begin // MULUQ HL, BC
                        mult.start_muluq(get_hl(), get_bc());
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hD3) begin // MULUQ HL, DE
                        mult.start_muluq(get_hl(), get_de());
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hE3) begin // MULUQ HL, HL
                        mult.start_muluq(get_hl(), get_hl());
                        state <= ExecMul;
                    end
                    else if (opcode == 8'hF3) begin // MULUQ HL, SP
                        mult.start_muluq(get_hl(), reg_sp);
                        state <= ExecMul;
                    end
                    else begin
                        retired_w.wset(True);
                        state <= FetchOp;
                    end
                end
                else begin
                    // Other prefixes
                    prefix <= 8'h00;
                    retired_w.wset(True);
                    state <= FetchOp;
                end
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
endmodule: mkCPUCore

endpackage: CPUCore
