/*
 * SCC.bsv — Konami SCC 5-channel wavetable synthesizer.
 *
 * Isomorphic with src/emulators/msx/scc.c.
 * Plain SCC register space at 0x9800–0x9FFF (offset = addr & 0xFF):
 *   0x00–0x7F  waveforms ch0-ch3 (32 bytes each); ch3 write copies to ch4
 *   0x80–0x89  12-bit frequencies (2 bytes per channel, LE)
 *   0x8A–0x8E  4-bit volumes
 *   0x8F       channel enable (bit 0 = ch0)
 *
 * Tone law: f = MSX_Z80_HZ / (32 * period). Period < 9 = silent.
 * Each tick_scc advances one Z80 clock; each channel counter increments,
 * wraps at period, and advances the waveform position by 1 of 32 bytes.
 */

package SCC;

import Vector::*;

interface SCC;
    method Action mem_write(Bit#(16) addr, Bit#(8) data);
    method ActionValue#(Bit#(8)) mem_read(Bit#(16) addr);
    method Action tick_scc;
    method Action set_bank2(Bit#(8) v);
    method Action set_bank3(Bit#(8) v);
    method Action set_mode(Bit#(8) v);
    (* always_ready *) method Bit#(16) pcm_sample;
    (* always_ready *) method Bit#(8) peek_enable;
endinterface

(* synthesize *)
module mkSCC(SCC);

    // 5 channels × 32 bytes waveform (signed, interpreted as Int#(8))
    Vector#(5, Vector#(32, Reg#(Bit#(8)))) r_wave <-
        replicateM(replicateM(mkReg(0)));

    // 12-bit frequency registers per channel
    Vector#(5, Reg#(Bit#(12))) r_freq   <- replicateM(mkReg(0));
    // 4-bit volume registers
    Vector#(5, Reg#(Bit#(4)))  r_vol    <- replicateM(mkReg(0));
    // Channel enable bits
    Reg#(Bit#(8)) r_en   <- mkReg(0);
    // Test register
    Reg#(Bit#(8)) r_test <- mkReg(0);

    // Bank / mode registers
    Reg#(Bit#(8)) r_bank2 <- mkReg(0);
    Reg#(Bit#(8)) r_bank3 <- mkReg(0);
    Reg#(Bit#(8)) r_mode  <- mkReg(0);
    // active: 0=off, 1=plain, 2=SCC+
    Reg#(Bit#(2)) r_active <- mkReg(0);

    // Per-channel clock counters and waveform positions
    Vector#(5, Reg#(Bit#(16))) r_clock <- replicateM(mkReg(0));
    Vector#(5, Reg#(Bit#(5)))  r_pos   <- replicateM(mkReg(0));

    // PCM output register
    Reg#(Bit#(16)) r_pcm <- mkReg(0);

    // --- refresh active mode ---
    function Bit#(2) calc_active(Bit#(8) mode, Bit#(8) bank2, Bit#(8) bank3);
        if ((mode & 8'h20) != 0 && (bank3 & 8'h80) != 0)
            return 2;
        else if ((mode & 8'h20) == 0 && (bank2 & 8'h3F) == 8'h3F)
            return 1;
        else
            return 0;
    endfunction

    // --- plain SCC register write (offset = addr & 0xFF) ---
    function Action do_reg_plain(Bit#(8) off, Bit#(8) value);
        action
            if (off < 8'h80) begin
                Bit#(3) ch = {1'b0, off[6:5]};
                Bit#(5) i  = off[4:0];
                r_wave[ch][i] <= value;
                if (ch == 3) r_wave[4][i] <= value;
            end else if (off >= 8'hE0) begin
                r_test <= value;
            end else if (off < 8'hA0) begin
                // mirror at 0x90: off = 0x80 + (off & 0x0F)
                Bit#(8) idx = 8'h80 | (off & 8'h0F);
                if (idx < 8'h8A) begin
                    // frequency bytes
                    Bit#(3) ch = (idx - 8'h80)[3:1];
                    if ((idx & 1) == 0)
                        r_freq[ch] <= {r_freq[ch][11:8], value};
                    else
                        r_freq[ch] <= {value[3:0], r_freq[ch][7:0]};
                end else if (idx < 8'h8F) begin
                    r_vol[idx - 8'h8A] <= value[3:0];
                end else if (idx == 8'h8F) begin
                    r_en <= value;
                end
            end
        endaction
    endfunction

    // --- SCC+ register write ---
    function Action do_reg_plus(Bit#(8) off, Bit#(8) value);
        action
            if (off < 8'hA0) begin
                Bit#(3) ch = off[7:5];
                if (ch < 5) r_wave[ch][off[4:0]] <= value;
            end else if (off >= 8'hC0) begin
                r_test <= value;
            end else if (off < 8'hB0) begin
                // 0xA0–0xAF: freq/vol/enable
                if (off < 8'hAA) begin
                    Bit#(3) ch = (off - 8'hA0)[3:1];
                    if ((off & 1) == 0)
                        r_freq[ch] <= {r_freq[ch][11:8], value};
                    else
                        r_freq[ch] <= {value[3:0], r_freq[ch][7:0]};
                end else if (off < 8'hAF) begin
                    r_vol[off - 8'hAA] <= value[3:0];
                end else if (off == 8'hAF) begin
                    r_en <= value;
                end
            end
        endaction
    endfunction

    // --- compute PCM mix ---
    function Bit#(16) do_mix();
        Int#(32) sum = 0;
        for (Integer c = 0; c < 5; c = c + 1) begin
            Bit#(12) per = r_freq[c];
            if ((r_en[c] == 1) && (per >= 9)) begin
                Int#(8)  w = unpack(r_wave[c][r_pos[c]]);
                Int#(32) s = signExtend(w) * signExtend(unpack(r_vol[c]));
                sum = sum + s;
            end
        end
        // Clamp to 16-bit signed
        if (sum > 32767)       sum = 32767;
        else if (sum < -32768) sum = -32768;
        return pack(truncate(sum));
    endfunction

    // ---- mem_write ----
    method Action mem_write(Bit#(16) addr, Bit#(8) data);
        Bit#(2) act = calc_active(r_mode, r_bank2, r_bank3);
        if ((addr & 16'hFFFE) == 16'hBFFE) begin
            r_mode   <= data;
            r_active <= calc_active(data, r_bank2, r_bank3);
        end else if (act == 1 && addr >= 16'h9800 && addr <= 16'h9FFF) begin
            do_reg_plain(addr[7:0], data);
        end else if (act == 2 && addr >= 16'hB800 && addr <= 16'hBFFD) begin
            do_reg_plus(addr[7:0], data);
        end else if (addr >= 16'h9000 && addr < 16'h9800) begin
            r_bank2  <= data;
            r_active <= calc_active(r_mode, data, r_bank3);
        end else if (addr >= 16'hB000 && addr < 16'hB800) begin
            r_bank3  <= data;
            r_active <= calc_active(r_mode, r_bank2, data);
        end
    endmethod

    // ---- mem_read ----
    method ActionValue#(Bit#(8)) mem_read(Bit#(16) addr);
        Bit#(8) result = 8'hFF;
        Bit#(2) act = calc_active(r_mode, r_bank2, r_bank3);
        if (act == 1 && addr >= 16'h9800 && addr <= 16'h9FFF) begin
            Bit#(8) off = addr[7:0];
            if (off < 8'h80)
                result = r_wave[off[6:5]][off[4:0]];
            else if (off >= 8'hE0)
                result = r_test;
            // else 0xFF
        end else if (act == 2 && addr >= 16'hB800 && addr < 16'hBFFE) begin
            Bit#(8) off = addr[7:0];
            if (off < 8'hA0) begin
                Bit#(3) ch = off[7:5];
                if (ch < 5) result = r_wave[ch][off[4:0]];
            end else if (off >= 8'hC0 && off < 8'hE0)
                result = r_test;
        end
        return result;
    endmethod

    // ---- tick_scc: advance one Z80 clock ----
    method Action tick_scc;
        for (Integer c = 0; c < 5; c = c + 1) begin
            Bit#(12) per = r_freq[c];
            if ((r_en[c] == 1) && (per >= 9)) begin
                Bit#(16) nc = r_clock[c] + 1;
                if (nc >= zeroExtend(per)) begin
                    r_clock[c] <= 0;
                    r_pos[c]   <= (r_pos[c] + 1) & 5'h1F;
                end else begin
                    r_clock[c] <= nc;
                end
            end
        end
        r_pcm <= do_mix();
    endmethod

    method Action set_bank2(Bit#(8) v);
        r_bank2  <= v;
        r_active <= calc_active(r_mode, v, r_bank3);
    endmethod

    method Action set_bank3(Bit#(8) v);
        r_bank3  <= v;
        r_active <= calc_active(r_mode, r_bank2, v);
    endmethod

    method Action set_mode(Bit#(8) v);
        r_mode   <= v;
        r_active <= calc_active(v, r_bank2, r_bank3);
    endmethod

    method Bit#(16) pcm_sample = r_pcm;
    method Bit#(8)  peek_enable = r_en;

endmodule: mkSCC

endpackage: SCC
