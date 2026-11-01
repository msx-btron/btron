/*
 * TopQmtechXc7a35t.v — QMTECH Artix-7 XC7A35T SDRAM core board.
 *
 * Cheap Xilinx board with enough external RAM for a MegaROM.
 * From the QMTECH XC7A35T core-board manual (GitHub ChinaQMTECH,
 * QMTECH_XC7A15T_35T_CSG325_CORE_BOARD):
 *   Device:  XC7A35T-1CSG325C
 *   Fabric:  33,280 logic cells, 1,800 Kbit block RAM
 *   RAM:     32 MB Winbond W9825G6KH-6 SDRAM
 *   Flash:   N25Q064A, 8 MB
 *   Clock:   50 MHz
 *   LEDs:    4 user LEDs, 3 user switches
 * 32 MB holds a 4 MB MegaROM, the 512 KB mapper, the 128 KB VRAM,
 * and a second cartridge image.
 *
 * The SDRAM controller is not in this file. Balls are not assigned.
 * Nothing here is programmed.
 *
 * MMCM: DIVCLK 5, CLKFBOUT_MULT 63, CLKOUT0_DIVIDE 11.
 * VCO = 50 * 63 / 5 = 630 MHz. Output = 630 / 11 = 57.2727 MHz.
 * 630 MHz sits just above the Artix-7 -1 MMCM VCO floor of 600 MHz.
 */

`default_nettype none

module TopQmtechXc7a35t (
    input  wire clk_50mhz,
    input  wire key_reset_n,

    output wire [3:0] led,

    output wire audio_l,
    output wire audio_r,

    output wire [3:0] video_p,
    output wire [3:0] video_n
);

    wire clk_soc;
    wire pll_locked;
    wire clkfb;

    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0),
        .DIVCLK_DIVIDE(5),
        .CLKFBOUT_MULT_F(63),
        .CLKOUT0_DIVIDE_F(11),
        .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKOUT0_PHASE(0.0)
    ) pll_inst (
        .CLKIN1(clk_50mhz),
        .CLKFBIN(clkfb),
        .RST(~key_reset_n),
        .PWRDWN(1'b0),
        .CLKOUT0(clk_soc),
        .CLKFBOUT(clkfb),
        .LOCKED(pll_locked)
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
        .rst_n(key_reset_n & pll_locked),
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

    assign led[0] = led_turbo;
    assign led[1] = led_caps;
    assign led[2] = led_kana;
    assign led[3] = pll_locked;

    assign audio_l = audio_bit;
    assign audio_r = audio_bit;

    assign video_p = {clk_soc, video_rgb565[15], video_rgb565[10], video_rgb565[4]};
    assign video_n = ~video_p;

    wire unused_status = video_hsync ^ video_vsync ^ video_blank ^ vdp_irq;
    (* keep *) wire keep_status = unused_status;

endmodule

`default_nettype wire
