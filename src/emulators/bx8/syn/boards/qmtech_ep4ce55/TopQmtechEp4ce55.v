/*
 * TopQmtechEp4ce55.v — QMTECH Cyclone IV EP4CE55 core board.
 *
 * This is the replacement for the EP4CE15F17C8. That part is not a target.
 * Cyclone IV E EP4CE15 has 15,408 LEs and 56 M9K blocks (504 Kbit, 63 KB).
 * The F17 package bonds 165 user I/Os, and C8 is the slow speed grade.
 * 63 KB is smaller than the 128 KB VRAM, and a 4 MB MegaROM cannot live
 * on the die. A card that wires SDRAM onto an F17 still leaves the fabric
 * at 15,408 LEs.
 *
 * Replacement, from the Land Boards RETRO-EP4CE15 note (QMTECH EP4CE55
 * section) and the Cyclone IV E device tables:
 *   Device:  EP4CE55F23C8N
 *   Fabric:  55,856 LEs, 260 M9K, 2,340 Kbit, 324 user I/Os, FBGA-484
 *   RAM:     32 MB Winbond W9825C6KH-6 (4M x 4 banks x 16)
 *   Clock:   50 MHz crystal
 * 32 MB holds a 4 MB MegaROM, the 512 KB mapper, and the 128 KB VRAM.
 *
 * The SDRAM controller is not in this file. Memory balls are not assigned.
 * The vendor schematic is the pin source. Nothing here is programmed.
 *
 * PLL ratio, exact: 50 MHz * 63 / 55 = 57.2727 MHz.
 * One legal Cyclone IV point is VCO 50 * 63 / 5 = 630 MHz, then / 11.
 */

`default_nettype none

module TopQmtechEp4ce55 (
    input  wire clk_50mhz,
    input  wire key_reset_n,

    output wire [1:0] led,

    output wire audio_l,
    output wire audio_r,

    // Not a TMDS encoder. Four bits for a later video jack.
    output wire [3:0] video_p,
    output wire [3:0] video_n
);

    wire        clk_soc;
    wire        pll_locked;
    wire [4:0]  pll_clk;

    altpll #(
        .bandwidth_type("AUTO"),
        .clk0_divide_by(55),
        .clk0_duty_cycle(50),
        .clk0_multiply_by(63),
        .clk0_phase_shift("0"),
        .inclk0_input_frequency(20000),
        .intended_device_family("Cyclone IV E"),
        .operation_mode("NORMAL"),
        .pll_type("AUTO"),
        .port_clk0("PORT_USED"),
        .port_inclk0("PORT_USED"),
        .width_clock(5)
    ) pll_inst (
        .inclk({1'b0, clk_50mhz}),
        .clk(pll_clk),
        .locked(pll_locked)
    );

    assign clk_soc = pll_clk[0];

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

    // The core board's user LED is active low.
    assign led[0] = ~led_turbo;
    assign led[1] = ~pll_locked;

    assign audio_l = audio_bit;
    assign audio_r = audio_bit;

    assign video_p = {clk_soc, video_rgb565[15], video_rgb565[10], video_rgb565[4]};
    assign video_n = ~video_p;

    // hsync, vsync, blank, vdp_irq, caps, and kana stay in the shell until
    // a schematic names their pins. Referenced so the nets are not dropped.
    wire unused_status = video_hsync ^ video_vsync ^ video_blank ^ vdp_irq
                       ^ led_caps ^ led_kana;
    (* keep *) wire keep_status = unused_status;

endmodule

`default_nettype wire
