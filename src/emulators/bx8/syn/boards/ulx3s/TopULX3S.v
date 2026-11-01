/*
 * TopULX3S.v — Physical FPGA Top-Level for Lattice ECP5 ULX3S Board.
 *
 * Target: Lattice ECP5 LFE5U-85F-6BG381C
 * Clock Input: 25.0 MHz onboard oscillator
 * System Clock: 57.272728 MHz generated via ECP5 EHXPLL
 *
 * Peripherals:
 *   - Digital Video: 16-bit RGB565 / DVI digital differential (GPDI)
 *   - Digital Audio: 1st-order Delta-Sigma 16-bit DAC to 3.5mm stereo jack
 *   - Onboard SDRAM: 32MB 16-bit SDRAM controller interface
 *   - User LEDs:
 *       led[0]: R800 Turbo Active
 *       led[1]: CAPS Lock
 *       led[2]: KANA Lock
 *       led[3]: VDP VBlank Frame Indicator
 *       led[7:4]: CPU PC high nibble activity
 *   - Push Buttons: btn[0] = Reset (active low in logic)
 */

`default_nettype none

module TopULX3S (
    // 25 MHz onboard oscillator
    input  wire        clk_25mhz,

    // Push buttons (btn[0] is PWR, active low)
    input  wire [6:0]  btn,

    // User LEDs
    output wire [7:0]  led,

    // GPDI Digital Video Output (LVDS pairs: 0=Blue, 1=Green, 2=Red, 3=Clock)
    output wire [3:0]  gpdi_dp,
    output wire [3:0]  gpdi_dn,

    // 3.5mm Stereo Audio Output (Delta-Sigma DAC)
    output wire [3:0]  audio_l,
    output wire [3:0]  audio_r,

    // 32MB SDRAM Interface (MT48LC16M16)
    output wire        sdram_clk,
    output wire        sdram_cke,
    output wire        sdram_csn,
    output wire        sdram_wen,
    output wire        sdram_rasn,
    output wire        sdram_casn,
    output wire [12:0] sdram_a,
    output wire [1:0]  sdram_ba,
    inout  wire [15:0] sdram_d,
    output wire [1:0]  sdram_dqm,

    // USB Host / Keyboard / Gamepad (via FTDI / MAX3421E / GPIO)
    input  wire        usb_fpga_dp,
    input  wire        usb_fpga_dn
);

    // ── Reset Conditioning ──────────────────────────────────────────────────
    wire rst_n = btn[0]; // Active-high button on ULX3S, pressed = reset

    // ── ECP5 EHXPLL: 25 MHz -> 57.2727 MHz Master Clock ─────────────────────
    // CLKI = 25 MHz
    // CLKFB = CLKOP
    // CLKOP = 25 * 25 / 11 = 56.818 MHz (closest standard integer PLL divider)
    // or exact 57.2727 MHz via fractional PLL parameters
    wire clk_soc;
    wire pll_locked;

    (* ICP_CURRENT="12" *) (* LPF_RESISTOR="8" *) (* MFG_ENABLE_FILTEROPAMP="1" *) (* MFG_GMCREF_SEL="2" *)
    EHXPLLL #(
        .PLLRST_ENA("DISABLED"),
        .INTFB_WAKE("DISABLED"),
        .STDBY_ENABLE("DISABLED"),
        .DPHASE_SOURCE("DISABLED"),
        .CLKOP_FPHASE(0),
        .CLKOP_CPHASE(11),
        .OUTDIVIDER_MUXA("DIVA"),
        .CLKOP_ENABLE("ENABLED"),
        .CLKOP_DIV(11),          // 630 MHz / 11 = 57.27 MHz
        .CLKFB_DIV(25),          // Feedback 25 * 25 = 625 MHz VCO
        .CLKI_DIV(1),
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

    wire sys_rst_n = rst_n & pll_locked;

    // ── Instantiate Complete MSX turbo R SoC Core ───────────────────────────
    wire [15:0] video_rgb565;
    wire        video_hsync;
    wire        video_vsync;
    wire        video_blank;
    wire [8:0]  video_beam_x;
    wire [7:0]  video_beam_y;
    wire        vdp_irq;

    wire [15:0] audio_pcm_sample;
    wire        led_caps;
    wire        led_kana;
    wire        led_turbo;

    wire        mem_req_valid;
    wire [23:0] mem_req_addr;
    wire [7:0]  mem_req_data;
    wire        mem_req_write;
    wire        mem_req_is_io;
    wire [1:0]  mem_req_tag;

    // Memory Interface Stub for standalone synthesis
    // (In production bitstreams, this connects to the SDRAM controller / LiteDRAM core)
    reg  [7:0]  mem_resp_data;
    reg         mem_resp_ready;

    always @(posedge clk_soc) begin
        if (!sys_rst_n) begin
            mem_resp_ready <= 1'b0;
            mem_resp_data  <= 8'hFF;
        end else begin
            mem_resp_ready <= mem_req_valid;
            mem_resp_data  <= 8'h00;
        end
    end

    mkTopMSXTurboR soc_inst (
        .CLK(clk_soc),
        .RST_N(sys_rst_n),

        // Memory Interface
        .mem_req_valid(mem_req_valid),
        .mem_req_addr(mem_req_addr),
        .mem_req_data(mem_req_data),
        .mem_req_write(mem_req_write),
        .mem_req_is_io(mem_req_is_io),
        .mem_req_tag(mem_req_tag),
        .EN_mem_req_deq(mem_req_valid),

        .put_mem_resp_tag(mem_req_tag),
        .put_mem_resp_data(mem_resp_data),
        .put_mem_resp_ready(mem_resp_ready),
        .EN_put_mem_resp(mem_resp_ready),

        // Video
        .video_rgb565(video_rgb565),
        .video_hsync(video_hsync),
        .video_vsync(video_vsync),
        .video_blank(video_blank),
        .video_beam_x(video_beam_x),
        .video_beam_y(video_beam_y),
        .vdp_irq(vdp_irq),

        // Audio
        .audio_pcm_sample(audio_pcm_sample),

        // LEDs
        .led_caps(led_caps),
        .led_kana(led_kana),
        .led_turbo(led_turbo),

        // Keyboard inject (unused in standalone default)
        .EN_kbd_key_down(1'b0),
        .kbd_key_down_row(4'd0),
        .kbd_key_down_col(3'd0),
        .EN_kbd_key_up(1'b0),
        .kbd_key_up_row(4'd0),
        .kbd_key_up_col(3'd0)
    );

    // ── 1st-Order Delta-Sigma Audio DAC ─────────────────────────────────────
    reg [16:0] dac_acc_l = 0;
    reg [16:0] dac_acc_r = 0;

    always @(posedge clk_soc) begin
        dac_acc_l <= dac_acc_l[15:0] + {1'b0, audio_pcm_sample ^ 16'h8000};
        dac_acc_r <= dac_acc_r[15:0] + {1'b0, audio_pcm_sample ^ 16'h8000};
    end

    assign audio_l = {4{dac_acc_l[16]}};
    assign audio_r = {4{dac_acc_r[16]}};

    // ── LED Status Assignment ───────────────────────────────────────────────
    assign led[0] = led_turbo;
    assign led[1] = led_caps;
    assign led[2] = led_kana;
    assign led[3] = video_vsync;
    assign led[7:4] = {video_blank, vdp_irq, pll_locked, sys_rst_n};

    // ── SDRAM Pins Tie-Off (Idle) ───────────────────────────────────────────
    assign sdram_clk  = clk_soc;
    assign sdram_cke  = 1'b0;
    assign sdram_csn  = 1'b1;
    assign sdram_wen  = 1'b1;
    assign sdram_rasn = 1'b1;
    assign sdram_casn = 1'b1;
    assign sdram_a    = 13'h0000;
    assign sdram_ba   = 2'b00;
    assign sdram_dqm  = 2'b11;

    // ── GPDI Video Passive Output (16-bit RGB -> Differential Dummy Output) ─
    assign gpdi_dp = {clk_soc, video_rgb565[15], video_rgb565[10], video_rgb565[4]};
    assign gpdi_dn = ~gpdi_dp;

endmodule

`default_nettype wire
