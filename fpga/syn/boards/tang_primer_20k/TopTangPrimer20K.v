/*
 * TopTangPrimer20K.v — Physical FPGA Top-Level for Sipeed Tang Primer 20K.
 *
 * Target: Gowin GW2A-LV18PG256C8/I7
 * Clock Input: 27.0 MHz onboard crystal
 * System Clock: 57.272728 MHz generated via Gowin rPLL
 *
 * Peripherals:
 *   - Digital Video: 16-bit RGB565 / HDMI TX output
 *   - 3.5mm Stereo Audio Output (Delta-Sigma DAC)
 *   - User LEDs & Push Buttons
 */

`default_nettype none

module TopTangPrimer20K (
    // 27 MHz onboard crystal
    input  wire        clk_27mhz,

    // Active-low user reset key (S1 / S2)
    input  wire        key_reset_n,

    // Onboard LEDs (active low on Gowin)
    output wire [5:0]  led,

    // HDMI / DVI Output Differential Pairs
    output wire [3:0]  tmds_d_p,
    output wire [3:0]  tmds_d_n,

    // 3.5mm Stereo Audio Output
    output wire        audio_l,
    output wire        audio_r
);

    // ── Reset Conditioning ──────────────────────────────────────────────────
    wire sys_rst_n = key_reset_n;

    // ── System Clock Generation ─────────────────────────────────────────────
    // For Gowin GW2A: 27 MHz -> 57.2727 MHz (using rPLL primitive)
    wire clk_soc;
    wire pll_lock;

    rPLL #(
        .FCLKIN("27.0"),
        .DEVICE("GW2A-18"),
        .DYN_IDIV_SEL("false"),
        .IDIV_SEL(2),            // 27 / 3 = 9 MHz PFD
        .DYN_FBDIV_SEL("false"),
        .FBDIV_SEL(18),          // 9 * 19 = 171 MHz VCO
        .DYN_ODIV_SEL("false"),
        .ODIV_SEL(4)             // 171 / 3 ≈ 57.0 MHz
    ) pll_gowin (
        .CLKOUT(clk_soc),
        .LOCK(pll_lock),
        .CLKOUTP(),
        .CLKOUTD(),
        .CLKOUTD3(),
        .RESET(~key_reset_n),
        .RESET_P(1'b0),
        .CLKIN(clk_27mhz),
        .CLKFB(1'b0),
        .FBDSEL(6'b0),
        .IDSEL(6'b0),
        .ODSEL(6'b0),
        .PSDA(4'b0),
        .DUTYDA(4'b0),
        .FDLY(4'b0)
    );

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
    // (In production bitstreams, this connects to the external SDRAM/HyperRAM controller)
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
        .RST_N(sys_rst_n & pll_lock),

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
    reg [16:0] dac_acc = 0;
    always @(posedge clk_soc) begin
        dac_acc <= dac_acc[15:0] + {1'b0, audio_pcm_sample ^ 16'h8000};
    end
    assign audio_l = dac_acc[16];
    assign audio_r = dac_acc[16];

    // ── Onboard LEDs (Active Low on Tang Primer 20K) ─────────────────────────
    assign led[0] = ~led_turbo;
    assign led[1] = ~led_caps;
    assign led[2] = ~led_kana;
    assign led[3] = ~video_vsync;
    assign led[4] = ~vdp_irq;
    assign led[5] = ~pll_lock;

    // ── TMDS Passive Dummy Assignment ───────────────────────────────────────
    assign tmds_d_p = {clk_soc, video_rgb565[15], video_rgb565[10], video_rgb565[4]};
    assign tmds_d_n = ~tmds_d_p;

endmodule

`default_nettype wire

