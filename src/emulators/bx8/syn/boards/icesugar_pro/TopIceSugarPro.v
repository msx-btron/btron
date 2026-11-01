/*
 * TopIceSugarPro.v — Muse Lab iCESugar-Pro.
 *
 * Cheap open-toolchain board. Yosys reads this top (see syn/yosys/ecp5_synth.ys).
 * The script has not been run to a bitstream, and the board is not programmed.
 *
 * From the Muse Lab board README (wuxx/icesugar-pro):
 *   Device:  Lattice ECP5 LFE5U-25F-6BG256C
 *   Fabric:  24K LUT, 56 sysMEM blocks, 1,008 Kbit embedded, 1 PLL
 *   RAM:     32 MB ISSI IS42S16160B SDRAM
 *   Flash:   32 MB W25Q256JV
 *   Clock:   25 MHz crystal (Muse Lab names the ball P6; it is not
 *            assigned in this tree)
 * 32 MB holds a 4 MB MegaROM, the 512 KB mapper, the 128 KB VRAM, and
 * a second cartridge image. 1,008 Kbit is 126 KB, so the VRAM does not
 * fit in block RAM and stays in the SDRAM with the MegaROM.
 *
 * The module itself has an RGB LED and a USB-C debugger. HDMI and a
 * headphone jack belong to a dock. Those ports are logical. Balls are
 * not assigned. The SDRAM controller is not in this file.
 *
 * Exact PLL ratio: 25 MHz * 126 / 5 / 11 = 57.2727 MHz.
 * VCO is 630 MHz. The phase detector then runs at 5 MHz. If the ECP5
 * PLL rejects that, the nearest integer set with a 12.5 MHz detector
 * is feedback 55 and output divide 12, which lands at 57.2917 MHz.
 */

`default_nettype none

module TopIceSugarPro (
    input  wire clk_25mhz,
    input  wire reset_n,

    output wire led_r,
    output wire led_g,
    output wire led_b,

    output wire audio_l,
    output wire audio_r,

    output wire [3:0] video_p,
    output wire [3:0] video_n
);

    wire clk_soc;
    wire pll_locked;

    (* ICP_CURRENT="12" *) (* LPF_RESISTOR="8" *) (* MFG_ENABLE_FILTEROPAMP="1" *) (* MFG_GMCREF_SEL="2" *)
    EHXPLLL #(
        .PLLRST_ENA("DISABLED"),
        .INTFB_WAKE("DISABLED"),
        .STDBY_ENABLE("DISABLED"),
        .DPHASE_SOURCE("DISABLED"),
        .CLKOP_FPHASE(0),
        .CLKOP_CPHASE(10),
        .OUTDIVIDER_MUXA("DIVA"),
        .CLKOP_ENABLE("ENABLED"),
        .CLKOP_DIV(11),
        .CLKFB_DIV(126),
        .CLKI_DIV(5),
        .FEEDBK_PATH("INT_OP")
    ) pll_inst (
        .CLKI(clk_25mhz),
        .CLKFB(clk_soc),
        .CLKOP(clk_soc),
        .RST(1'b0),
        .STDBY(1'b0),
        .PHASESEL0(1'b0),
        .PHASESEL1(1'b0),
        .PHASEDIR(1'b0),
        .PHASESTEP(1'b0),
        .PHASELOADREG(1'b0),
        .LOCK(pll_locked)
    );

    wire [15:0] video_rgb565;
    wire        video_hsync;
    wire        video_vsync;
    wire        video_blank;
    wire        vdp_irq;
    wire        audio_bit;
    wire        led_turbo;
    wire        led_caps;
    wire        led_kana;

    MsxSocShell shell (
        .clk(clk_soc),
        .rst_n(reset_n & pll_locked),
        .video_rgb565(video_rgb565),
        .video_hsync(video_hsync),
        .video_vsync(video_vsync),
        .video_blank(video_blank),
        .vdp_irq(vdp_irq),
        .audio_bit(audio_bit),
        .led_turbo(led_turbo),
        .led_caps(led_caps),
        .led_kana(led_kana)
    );

    assign led_r = led_turbo;
    assign led_g = led_caps;
    assign led_b = pll_locked;

    assign audio_l = audio_bit;
    assign audio_r = audio_bit;

    assign video_p = {clk_soc, video_rgb565[15], video_rgb565[10], video_rgb565[4]};
    assign video_n = ~video_p;

    wire unused_status = video_hsync ^ video_vsync ^ video_blank ^ vdp_irq ^ led_kana;
    (* keep *) wire keep_status = unused_status;

endmodule

`default_nettype wire
