/*
 * PCM.bsv — MSX turboR PCM DAC (T9769 ports 0xA4/0xA5).
 *
 * Isomorphic with src/emulators/msx/pcm.c.
 * Sample clock: every 228 Z80 T-states (~15700 Hz).
 * Port 0xA4 write = 8-bit unsigned PCM sample (0x80 = silence).
 * Port 0xA5 = ctrl: bit0=latched mode, bit1=enable DAC.
 *   Latched (ctrl[0]=1): sample latched on next clock tick.
 *   Slew (ctrl[0]=0): DAC slews toward written value at ±48/tick.
 * Port 0xA4 read = low 2 bits of tick counter (handshake).
 * Port 0xA5 read = ctrl & 0x1F.
 */

package PCM;

import FIFOF::*;

interface PCM;
    method Action io_write(Bit#(8) port, Bit#(8) data);
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
    method Action tick_pcm;           // advance one 228-T-state tick
    (* always_ready *) method Bit#(8) pcm_sample;   // current DAC output
    (* always_ready *) method Bit#(8) peek_ctrl;
endinterface

(* synthesize *)
module mkPCM(PCM);

    Reg#(Bit#(8)) r_dac     <- mkReg(8'h80);  // last written sample
    Reg#(Bit#(8)) r_hold    <- mkReg(8'h80);  // current DAC output
    Reg#(Bit#(8)) r_pend    <- mkReg(8'h80);  // latched pending sample
    Reg#(Bit#(1)) r_pend_on <- mkReg(0);      // pending latch valid
    Reg#(Bit#(8)) r_ctrl    <- mkReg(8'h03);  // control register
    Reg#(Bit#(8)) r_clock   <- mkReg(0);      // tick counter
    Reg#(Bit#(9)) r_slew    <- mkReg(9'd128); // slew DAC level (9-bit for signed arithmetic)

    // PCM_SLEW = 48 (max step per 15700 Hz tick in slew mode)
    Bit#(9) pcm_slew = 9'd48;

    // Queue for CPU I/O writes to decouple from tick
    FIFOF#(Tuple2#(Bit#(8), Bit#(8))) q_write <- mkFIFOF;

    // ---- Process CPU I/O writes from FIFO ----
    rule do_io_write (q_write.notEmpty);
        match {.port, .data} = q_write.first;
        q_write.deq;

        if (port == 8'hA4) begin
            r_dac <= data;
            if (r_ctrl[0] == 1) begin
                r_pend    <= data;
                r_pend_on <= 1;
            end else begin
                r_pend_on <= 0;
            end
        end else if (port == 8'hA5) begin
            Bit#(8) prev = r_ctrl;
            r_ctrl <= data;
            // if bit0 falls: clear pending
            if (prev[0] == 1 && data[0] == 0)
                r_pend_on <= 0;
        end
    endrule

    // ---- I/O write method ----
    method Action io_write(Bit#(8) port, Bit#(8) data);
        q_write.enq(tuple2(port, data));
    endmethod

    // ---- I/O read ----
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
        Bit#(8) result = 8'hFF;
        if (port == 8'hA4)
            result = {6'h00, r_clock[1:0]};   // low 2 bits of tick counter
        else if (port == 8'hA5)
            result = r_ctrl & 8'h1F;
        return result;
    endmethod

    // ---- tick_pcm: advance one sample period ----
    method Action tick_pcm;
        r_clock <= r_clock + 1;
        Bit#(8) raw_sample;
        if (r_ctrl[0] == 1) begin
            // Latched mode
            if (r_pend_on == 1) begin
                r_pend_on <= 0;
                r_slew    <= zeroExtend(r_pend);
                raw_sample = r_pend;
            end else begin
                raw_sample = r_hold;
            end
        end else begin
            // Slew mode: r_slew tracks dac
            Int#(9) slew_i   = unpack(r_slew);
            Int#(9) dac_i    = zeroExtend(unpack(r_dac));
            Int#(9) d        = dac_i - slew_i;
            Int#(9) slew_max = unpack(pcm_slew);
            if (d >  unpack(pcm_slew)) d = slew_max;
            if (d < -unpack(pcm_slew)) d = -slew_max;
            Int#(9) new_slew = slew_i + d;
            r_slew     <= pack(new_slew);
            raw_sample  = pack(new_slew)[7:0];
        end
        // Single write to r_hold: gate if ctrl[1]=0 (DAC muted = silence 0x80)
        r_hold <= (r_ctrl[1] == 1) ? raw_sample : 8'h80;
    endmethod

    method Bit#(8) pcm_sample = r_hold;
    method Bit#(8) peek_ctrl  = r_ctrl;

endmodule: mkPCM

endpackage: PCM
