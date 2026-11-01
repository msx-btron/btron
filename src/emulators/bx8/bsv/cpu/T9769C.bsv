/*
 * T9769C.bsv — Toshiba T9769C / S1990 MSX turbo R Engine.
 *
 * Implements:
 *   1. S1990 register select (Port 0xE4) and data read/write (Port 0xE5).
 *   2. Dynamic CPU switching between Z80A (3.58 MHz) and R800 (7.16 MHz).
 *   3. DRAM shadow mode switching (Slot 0-0 fast DRAM shadow vs ROM).
 *   4. E6 counter timer (Ports 0xE6, 0xE7).
 *   5. System flags (Port 0xF4).
 *   6. MSX-MIDI status (Port 0xE9) and data (Port 0xE8).
 *      C-BIOS keyint reads E9 on every frame. TxRdy|TxEmpty (0x05) keeps
 *      RxRdy and the timer bit clear, so the MIDI hooks are not taken.
 *      An open-bus 0xFF calls H_MDIN and jumps H_MDTM every interrupt.
 *
 * References:
 *   src/emulators/msx/t9769c.c
 *   src/emulators/msx/t9769c.h
 */

package T9769C;

interface T9769C;
    // Tick enables from MSXClocks
    method Action tick_e6();

    // I/O Port Interface
    method Bit#(8) io_read(Bit#(8) port);
    method Action  io_write(Bit#(8) port, Bit#(8) val);

    // CPU mode outputs
    (* always_ready *) method Bool is_r800();
    (* always_ready *) method Bool is_dram_mode();
    (* always_ready *) method Bit#(8) get_cpu_status();
endinterface: T9769C

(* synthesize *)
module mkT9769C(T9769C);
    Reg#(Bit#(8))  s1990_reg   <- mkReg(8'h00);
    Reg#(Bit#(8))  cpu_status  <- mkReg(8'h60); // bit 5: 1=Z80, bit 6: 1=ROM mode
    Reg#(Bit#(8))  sys_flags   <- mkReg(8'hA0); // bit 7: cold reset, bit 5: boot
    Reg#(Bit#(16)) e6_ticks    <- mkReg(16'h0000);
    Reg#(Bit#(8))  midi_data  <- mkReg(8'h00);

    RWire#(Bool)   e6_tick_w   <- mkRWire;
    RWire#(Bool)   e6_reset_w  <- mkRWire;

    // Harmonize timer counter in a single rule
    (* fire_when_enabled, no_implicit_conditions *)
    rule update_e6;
        if (isValid(e6_reset_w.wget())) begin
            e6_ticks <= 16'h0000;
        end else if (isValid(e6_tick_w.wget())) begin
            e6_ticks <= e6_ticks + 1;
        end
    endrule

    // Advance E6 timer on tick
    method Action tick_e6();
        e6_tick_w.wset(True);
    endmethod

    // CPU Mode outputs
    method Bool is_r800()      = (cpu_status[5] == 1'b0);
    method Bool is_dram_mode() = (cpu_status[6] == 1'b0);
    method Bit#(8) get_cpu_status() = cpu_status;

    // I/O read
    method Bit#(8) io_read(Bit#(8) port);
        case (port)
            8'hE4: return s1990_reg;
            8'hE5: begin
                case (s1990_reg)
                    8'd5:  return 8'h40; // Firmware switch
                    8'd6:  return cpu_status;
                    8'd13: return 8'h03;
                    8'd14: return 8'h2F;
                    8'd15: return 8'h8B;
                    default: return 8'hFF;
                endcase
            end
            8'hE6: return e6_ticks[7:0];
            8'hE7: return e6_ticks[15:8];
            8'hE8: return midi_data;
            // 0x05 = TxRdy|TxEmpty. Same idle status as t9769c.c. Bit 1
            // (RxRdy) and bit 7 (timer IRQ) stay clear.
            8'hE9: return 8'h05;
            8'hF4: return sys_flags;
            default: return 8'hFF;
        endcase
    endmethod

    // I/O write
    method Action io_write(Bit#(8) port, Bit#(8) val);
        case (port)
            8'hE4: s1990_reg <= val;
            8'hE5: begin
                if (s1990_reg == 8'd6) begin
                    cpu_status <= val & 8'h60;
                end
            end
            8'hE6, 8'hE7: begin
                e6_reset_w.wset(True);
            end
            8'hE8: midi_data <= val;
            8'hF4: sys_flags <= val;
        endcase
    endmethod
endmodule: mkT9769C

endpackage: T9769C
