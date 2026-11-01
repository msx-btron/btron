/*
 * PSG.bsv — AY-3-8910 PSG: register file, three tone generators, 17-bit
 * noise LFSR, envelope sequencer, and mixer.
 *
 * Isomorphic with src/emulators/msx/psg.c.
 * Ports: 0xA0 = register select, 0xA1 = write, 0xA2 = read.
 * tick_psg is called once per 8 PSG clocks (one "tick" unit).
 * PCM sample = mixer sum << 5 (unipolar 16-bit).
 *
 * LFSR polynomial: x^17 + x^14 + 1 (feedback = (lfsr ^ (lfsr >> 3)) & 1).
 * LFSR seed: 0x0ACE.  Noise period NP = max(1, R6 & 0x1F) * 2.
 */

package PSG;

import Vector::*;
import FIFOF::*;

interface PSG;
    (* always_ready *) method Action tick_psg;           // advance one /8 PSG tick
    method Action io_write(Bit#(8) port, Bit#(8) data);
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
    (* always_ready *) method Bit#(16) pcm_sample;       // current mixed output
    (* always_ready *) method Bit#(8) peek_reg(Bit#(4) idx);
    (* always_ready *) method Bit#(17) peek_lfsr;
endinterface

(* synthesize *)
module mkPSG(PSG);

    // Register file R0..R15
    Vector#(16, Reg#(Bit#(8))) r_reg <- replicateM(mkReg(0));

    // Port select latch
    Reg#(Bit#(4)) r_select <- mkReg(0);

    // Tone generators: counter, period, output polarity
    Vector#(3, Reg#(Bit#(16))) r_tone_c <- replicateM(mkReg(0));
    Vector#(3, Reg#(Bit#(16))) r_tone_p <- replicateM(mkReg(1));
    Vector#(3, Reg#(Bit#(1)))  r_tone_o <- replicateM(mkReg(0));

    // Noise generator
    Reg#(Bit#(16)) r_noise_c <- mkReg(0);
    Reg#(Bit#(16)) r_noise_p <- mkReg(2);
    Reg#(Bit#(17)) r_lfsr    <- mkReg(17'h0ACE);
    Reg#(Bit#(1))  r_noise_o <- mkReg(0);

    // Envelope generator
    Reg#(Bit#(16)) r_env_c    <- mkReg(0);
    Reg#(Bit#(16)) r_env_p    <- mkReg(1);
    Reg#(Bit#(5))  r_env_pos  <- mkReg(0);
    Reg#(Bit#(1))  r_env_dec  <- mkReg(0);
    Reg#(Bit#(1))  r_env_done <- mkReg(0);
    Reg#(Bit#(4))  r_env_vol  <- mkReg(0);

    // Last computed PCM sample
    Reg#(Bit#(16)) r_pcm <- mkReg(0);

    // Queue for CPU I/O writes to decouple from tick
    FIFOF#(Tuple2#(Bit#(8), Bit#(8))) q_write <- mkFIFOF;

    // --- Volume table (1.5 dB/step, level 15 = 255) ---
    function Bit#(8) vol_table(Bit#(4) lvl);
        case (lvl)
            4'd0:  return 8'd0;
            4'd1:  return 8'd51;
            4'd2:  return 8'd57;
            4'd3:  return 8'd64;
            4'd4:  return 8'd71;
            4'd5:  return 8'd80;
            4'd6:  return 8'd90;
            4'd7:  return 8'd101;
            4'd8:  return 8'd113;
            4'd9:  return 8'd127;
            4'd10: return 8'd143;
            4'd11: return 8'd160;
            4'd12: return 8'd180;
            4'd13: return 8'd202;
            4'd14: return 8'd227;
            default: return 8'd255;
        endcase
    endfunction

    // --- Tone period decode ---
    function Bit#(16) decode_tone_period(Bit#(8) ch);
        Bit#(16) v = zeroExtend(r_reg[ch * 2]) |
                     (zeroExtend(r_reg[ch * 2 + 1] & 8'h0F) << 8);
        return (v == 0) ? 1 : v;
    endfunction

    // --- Noise period decode ---
    function Bit#(16) decode_noise_period();
        Bit#(16) v = zeroExtend(r_reg[6] & 8'h1F);
        return (v == 0) ? 2 : v * 2;
    endfunction

    // --- Envelope period decode ---
    function Bit#(16) decode_env_period();
        Bit#(16) v = zeroExtend(r_reg[11]) | (zeroExtend(r_reg[12]) << 8);
        return (v == 0) ? 1 : v;
    endfunction

    // --- Envelope step logic (called when env_c wraps) ---
    // Returns new (env_pos, env_dec, env_done, env_vol)
    function Tuple4#(Bit#(5), Bit#(1), Bit#(1), Bit#(4))
             env_step(Bit#(5) pos, Bit#(1) dec, Bit#(1) done, Bit#(4) vol);
        Bit#(4) shape = r_reg[13][3:0];
        Bit#(5)  new_pos  = pos;
        Bit#(1)  new_dec  = dec;
        Bit#(1)  new_done = done;
        Bit#(4)  new_vol  = vol;

        if (done == 1) begin
            if (shape[0] == 1) begin
                // hold: nothing moves
                new_vol = vol;
            end else if (shape[3] == 1) begin
                // continue: restart
                new_done = 0;
                if (shape[1] == 1) new_dec = dec ^ 1;
            end else begin
                // fell silent
                new_vol = 0;
            end
        end else begin
            // Compute volume: 32 steps over 16 levels; pos[4:1] gives level 0..15
            new_vol = dec == 1 ? (4'd15 - pos[4:1]) : pos[4:1];
            // Increment with overflow check via 6-bit arithmetic
            Bit#(6) npos6 = zeroExtend(pos) + 1;
            if (npos6 >= 6'd32) begin
                new_pos = 0;
                if (shape[0] == 1) begin
                    new_done = 1;
                    Bit#(1) alt = shape[1];
                    new_vol = ((dec ^ alt) == 1) ? 0 : 15;
                end else if (shape[3] == 0) begin
                    new_done = 1;
                    new_vol  = 0;
                end else if (shape[1] == 1) begin
                    new_dec = dec ^ 1;
                end
            end else begin
                new_pos = npos6[4:0];
            end
        end
        return tuple4(new_pos, new_dec, new_done, new_vol);
    endfunction

    // --- Mixer: compute PCM sum ---
    function Bit#(16) do_mix();
        Bit#(32) sum = 0;
        for (Integer c = 0; c < 3; c = c + 1) begin
            Bit#(1) tone_dis  = r_reg[7][c];
            Bit#(1) noise_dis = r_reg[7][3 + c];
            Bit#(1) tone_out  = (tone_dis == 1) ? 1 : r_tone_o[c];
            Bit#(1) noise_out = (noise_dis == 1) ? 1 : r_noise_o;
            if ((tone_out & noise_out) == 1) begin
                Bit#(8) v = r_reg[8 + fromInteger(c)];
                Bit#(4) lvl = (v[4] == 1) ? r_env_vol : v[3:0];
                sum = sum + zeroExtend(vol_table(lvl));
            end
        end
        // Saturate to 16 bits: sum * 32 (shift left 5)
        Bit#(32) scaled = sum << 5;
        return (scaled > 32'hFFFF) ? 16'hFFFF : scaled[15:0];
    endfunction

    // ---- Process CPU I/O writes from FIFO ----
    rule do_io_write (q_write.notEmpty);
        match {.port, .data} = q_write.first;
        q_write.deq;

        case (port & 8'h03)
            8'h00: r_select <= data[3:0];
            8'h01: begin
                Bit#(4) sel = r_select;
                r_reg[sel] <= data;
                // Update derived periods
                if (sel <= 5) begin
                    // Tone period for channel sel/2
                    Bit#(4) ch4 = sel >> 1;
                    if (sel[0] == 0) begin
                        // low byte written
                        Bit#(16) hi = zeroExtend(r_reg[sel + 1] & 8'h0F);
                        Bit#(16) val = zeroExtend(data) | (hi << 8);
                        r_tone_p[ch4] <= (val == 0) ? 1 : val;
                    end else begin
                        // high byte written
                        Bit#(16) lo = zeroExtend(r_reg[sel - 1]);
                        Bit#(16) val = lo | (zeroExtend(data & 8'h0F) << 8);
                        r_tone_p[ch4] <= (val == 0) ? 1 : val;
                    end
                end else if (sel == 6) begin
                    Bit#(16) v = zeroExtend(data & 8'h1F);
                    r_noise_p <= (v == 0) ? 2 : v * 2;
                end else if (sel == 11 || sel == 12) begin
                    Bit#(16) v = zeroExtend(r_reg[11]) | (zeroExtend(r_reg[12]) << 8);
                    r_env_p <= (v == 0) ? 1 : v;
                end else if (sel == 13) begin
                    // Shape write restarts envelope
                    r_env_pos  <= 0;
                    r_env_dec  <= (data[2] == 1) ? 0 : 1;
                    r_env_done <= 0;
                    r_env_c    <= 0;
                end
            end
            default: noAction;
        endcase
    endrule

    // ---- tick_psg method: advance all generators ----
    method Action tick_psg;
        // Tone generators
        for (Integer c = 0; c < 3; c = c + 1) begin
            Bit#(16) nc = r_tone_c[c] + 1;
            if (nc >= r_tone_p[c]) begin
                r_tone_c[c] <= 0;
                r_tone_o[c] <= r_tone_o[c] ^ 1;
            end else begin
                r_tone_c[c] <= nc;
            end
        end

        // Noise LFSR
        Bit#(16) nn = r_noise_c + 1;
        if (nn >= r_noise_p) begin
            r_noise_c <= 0;
            Bit#(17) lfsr = r_lfsr;
            Bit#(1) fb = (lfsr[0] ^ lfsr[3]) & 1;
            Bit#(17) new_lfsr = {fb, lfsr[16:1]};
            r_lfsr    <= new_lfsr;
            r_noise_o <= new_lfsr[0];
        end else begin
            r_noise_c <= nn;
        end

        // Envelope
        Bit#(16) ne = r_env_c + 1;
        if (ne >= r_env_p) begin
            r_env_c <= 0;
            match {.np, .nd, .ndn, .nv} =
                env_step(r_env_pos, r_env_dec, r_env_done, r_env_vol);
            r_env_pos  <= np;
            r_env_dec  <= nd;
            r_env_done <= ndn;
            r_env_vol  <= nv;
        end else begin
            r_env_c <= ne;
        end

        // Update PCM sample
        r_pcm <= do_mix();
    endmethod

    // ---- I/O Write Method ----
    method Action io_write(Bit#(8) port, Bit#(8) data);
        q_write.enq(tuple2(port, data));
    endmethod

    // ---- I/O Read ----
    method ActionValue#(Bit#(8)) io_read(Bit#(8) port);
        Bit#(8) result = 8'hFF;
        if ((port & 8'h03) == 8'h02) begin
            case (r_select)
                4'd14: result = 8'h3F;      // Port A: joystick idle
                4'd15: result = r_reg[15];  // Port B: echo drive
                default: result = r_reg[r_select];
            endcase
        end
        return result;
    endmethod

    // ---- Outputs ----
    method Bit#(16) pcm_sample = r_pcm;

    method Bit#(8) peek_reg(Bit#(4) idx) = r_reg[idx];

    method Bit#(17) peek_lfsr = r_lfsr;

endmodule: mkPSG

endpackage: PSG
