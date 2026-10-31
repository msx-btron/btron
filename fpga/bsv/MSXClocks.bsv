/*
 * Clocks.bsv — Single Synchronous Master Clock & Phase-Enable Generators.
 *
 * Implements a 57.272728 MHz master clock domain with zero-CDC phase ticks:
 *  - tick_28m   : 28.636 MHz (div 2)  - V9958 dot clock & R800 internal sequencer
 *  - tick_7m    : 7.159 MHz  (div 8)  - R800 bus clock
 *  - tick_3m58  : 3.580 MHz  (div 16) - Z80A CPU & MSX legacy bus clock
 *  - tick_e6    : 255.682 kHz (div 224)- T9769C Gate Array Port #E6/#E7 timer
 *  - tick_pcm   : 15.700 kHz (div 3648)- turboR PCM DAC sample strobe
 *  - tick_audio : 44.124 kHz (div 1298)- Saturation audio mixer output strobe
 */

package MSXClocks;

interface MSXClocks;
    (* always_ready *) method Bool tick_28m();
    (* always_ready *) method Bool tick_7m();
    (* always_ready *) method Bool tick_3m58();
    (* always_ready *) method Bool tick_e6();
    (* always_ready *) method Bool tick_pcm();
    (* always_ready *) method Bool tick_audio();
endinterface: MSXClocks

(* synthesize *)
module mkMSXClocks(MSXClocks);
    Reg#(Bit#(1))  cnt_2    <- mkReg(0);
    Reg#(Bit#(3))  cnt_8    <- mkReg(0);
    Reg#(Bit#(4))  cnt_16   <- mkReg(0);
    Reg#(Bit#(8))  cnt_224  <- mkReg(0);
    Reg#(Bit#(12)) cnt_3648 <- mkReg(0);
    Reg#(Bit#(11)) cnt_1298 <- mkReg(0);

    rule step_counters;
        cnt_2    <= cnt_2 + 1;
        cnt_8    <= cnt_8 + 1;
        cnt_16   <= cnt_16 + 1;
        cnt_224  <= (cnt_224 == 223) ? 0 : cnt_224 + 1;
        cnt_3648 <= (cnt_3648 == 3647) ? 0 : cnt_3648 + 1;
        cnt_1298 <= (cnt_1298 == 1297) ? 0 : cnt_1298 + 1;
    endrule

    method Bool tick_28m();
        return (cnt_2 == 0);
    endmethod

    method Bool tick_7m();
        return (cnt_8 == 0);
    endmethod

    method Bool tick_3m58();
        return (cnt_16 == 0);
    endmethod

    method Bool tick_e6();
        return (cnt_224 == 0);
    endmethod

    method Bool tick_pcm();
        return (cnt_3648 == 0);
    endmethod

    method Bool tick_audio();
        return (cnt_1298 == 0);
    endmethod
endmodule: mkMSXClocks

endpackage: MSXClocks
