/*
 * msx_soc_shell.v — Board-independent MSX turbo R shell.
 *
 * Instantiates mkTopMSXTurboR, answers its memory port with a stub, and
 * turns the mixed PCM sample into one delta-sigma bit.
 *
 * The stub is not a memory controller. Ready follows the request, and the
 * data byte is zero. Mapper RAM, VRAM, and a MegaROM image are not in this
 * file. No board is programmed from this tree.
 */

`default_nettype none

module MsxSocShell (
    input  wire        clk,
    input  wire        rst_n,

    output wire [15:0] video_rgb565,
    output wire        video_hsync,
    output wire        video_vsync,
    output wire        video_blank,
    output wire        vdp_irq,

    output wire        audio_bit,

    output wire        led_turbo,
    output wire        led_caps,
    output wire        led_kana
);

    wire [8:0] video_beam_x;
    wire [7:0] video_beam_y;
    wire [15:0] audio_pcm_sample;

    wire        mem_req_valid;
    wire [23:0] mem_req_addr;
    wire [7:0]  mem_req_data;
    wire        mem_req_write;
    wire        mem_req_is_io;
    wire [1:0]  mem_req_tag;

    reg  [7:0]  mem_resp_data;
    reg         mem_resp_ready;

    always @(posedge clk) begin
        if (!rst_n) begin
            mem_resp_ready <= 1'b0;
            mem_resp_data  <= 8'hFF;
        end else begin
            mem_resp_ready <= mem_req_valid;
            mem_resp_data  <= 8'h00;
        end
    end

    mkTopMSXTurboR soc_inst (
        .CLK(clk),
        .RST_N(rst_n),

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

        .video_rgb565(video_rgb565),
        .video_hsync(video_hsync),
        .video_vsync(video_vsync),
        .video_blank(video_blank),
        .video_beam_x(video_beam_x),
        .video_beam_y(video_beam_y),
        .vdp_irq(vdp_irq),

        .audio_pcm_sample(audio_pcm_sample),

        .led_caps(led_caps),
        .led_kana(led_kana),
        .led_turbo(led_turbo),

        .EN_kbd_key_down(1'b0),
        .kbd_key_down_row(4'd0),
        .kbd_key_down_col(3'd0),
        .EN_kbd_key_up(1'b0),
        .kbd_key_up_row(4'd0),
        .kbd_key_up_col(3'd0)
    );

    // 1st-order delta-sigma. Both ears carry the same mixed sample.
    reg [16:0] dac_acc = 17'd0;
    always @(posedge clk) begin
        dac_acc <= dac_acc[15:0] + {1'b0, audio_pcm_sample ^ 16'h8000};
    end
    assign audio_bit = dac_acc[16];

endmodule

`default_nettype wire
