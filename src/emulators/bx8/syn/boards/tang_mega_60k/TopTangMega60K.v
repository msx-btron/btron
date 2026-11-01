/*
 * TopTangMega60K.v — Sipeed Tang Mega 60K SOM.
 *
 * This is not the Tang Primer 20K. Primer 20K is a GW2A-18 board and is
 * not a target. Mega 60K is a GW5AT-60.
 *
 * From the Sipeed wiki page for Tang Mega 60K (updated 2025-09-25):
 *   Device:  GOWIN GW5AT-LV60PG484A
 *   Fabric:  59,904 LUT4, 59,904 flip-flops, 2,124 Kbit block SRAM,
 *            8 PLLs
 *   RAM:     512 MB DDR3 on the SOM (4 Gbit, one device, 16-bit)
 *   Flash:   64 Mbit on the SOM
 * The dock (shared with the Mega 138K) adds HDMI, a headphone jack,
 * buttons, and LEDs. The DDR3 stays on the SOM, so the cartridge image
 * does not depend on the dock. 512 MB covers every MegaROM in the
 * ledger, the 512 KB mapper, and the 128 KB VRAM, with room to spare.
 * This is the large-RAM board of the three cheap targets.
 *
 * The wiki does not name the SOM oscillator frequency. This wrapper
 * does not invent a PLL. clk_soc is clk_in until that frequency is
 * copied from the Sipeed schematic. The master clock is 57.2727 MHz
 * only after that ratio exists.
 *
 * The DDR3 controller is not in this file. Balls are not assigned.
 * Nothing here is programmed.
 */

`default_nettype none

module TopTangMega60K (
    input  wire clk_in,
    input  wire key_reset_n,

    output wire [3:0] led,

    output wire audio_l,
    output wire audio_r,

    output wire [3:0] video_p,
    output wire [3:0] video_n
);

    wire clk_soc = clk_in;

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
        .rst_n(key_reset_n),
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
    assign led[3] = video_vsync;

    assign audio_l = audio_bit;
    assign audio_r = audio_bit;

    // Not a TMDS encoder. The dock's HDMI jack is not pinned here.
    assign video_p = {clk_soc, video_rgb565[15], video_rgb565[10], video_rgb565[4]};
    assign video_n = ~video_p;

    wire unused_status = video_hsync ^ video_blank ^ vdp_irq;
    (* keep *) wire keep_status = unused_status;

endmodule

`default_nettype wire
