// Integrates each LED's on-time over a ~60 Hz window of ce ticks and emits a
// 2-bit brightness level per LED, mirroring MAME's pwm_display with
// set_bri_levels(0.02, 0.2): duty >2% = dim, >20% = bright. Levels update
// once per window and hold in between, so within-window strobing does not
// reach the video output (the CPU's ATBZ clears seg the same instruction it
// raises str, so we accumulate str&seg coincidence per ce tick — never
// edge-sample). Frame-to-frame duty changes DO pass through, smoothed: that
// is the real handheld's LED flicker and is deliberate.
//
// Reference: MAME's mfootb (src/mame/handheld/hh_rw5000.cpp, a Rockwell
// B6100) configures PWM_DISPLAY set_size(9, 11) and set_bri_levels(0.02,
// 0.2), and inherits pwm_display_device's default set_interpolation(0.5).
// pwm.cpp's frame_tick does, per cell:
//     bri = bri * (1 - f) + (duty within this frame) * f     [f = 0.5]
// then classifies the SMOOTHED value against the levels. The smooth[]
// stage below is that IIR; without it a cell's level tracks each raw
// window and jitters more than hardware does.
//
// Not modelled: pwm.cpp also clamps bri to a cutoff of 4 * 0.2 = 0.8 before
// storing it. With 9 columns multiplexed no cell comes close to 80% duty,
// so the clamp is unreachable here.
module led_capture #(
    parameter WINDOW = 1167   // ce ticks per window: 280kHz/4 instr rate / 60Hz
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         ce,
    input  wire [8:0]   str,
    input  wire [9:0]   seg,
    input  wire         dp_in,      // score button: decimal point line
    output reg  [197:0] levels,     // 99 LEDs x 2 bits, led = col*11 + line
    output reg          window_tick
);
    // thresholds: strictly-greater-than 2% / 20% of WINDOW
    localparam integer DIM_MIN    = (WINDOW * 2) / 100 + 1;   // 24 for 1167
    localparam integer BRIGHT_MIN = WINDOW / 5 + 1;           // 234 for 1167
    localparam [10:0]  WIN_LAST   = WINDOW - 1;

    // line order per MAME mfootb driver remap:
    // {seg[9:7] = top/mid/bottom dash rows (lines 10/9/8), dp (line 7),
    //  seg[6:0] = 7seg a-g (lines 6..0)}
    wire [10:0] line_active = {seg[9:7], dp_in, seg[6:0]};

    reg [10:0] cnt [0:98];
    // Exponentially-smoothed duty estimate, same 0..WINDOW scale as cnt and
    // the thresholds, so DIM_MIN/BRIGHT_MIN apply to it unchanged.
    reg [10:0] smooth [0:98];
    reg [10:0] window_pos;
    reg [15:0] duty;
    integer c, l;

    always @(posedge clk) begin
        if (!rst_n) begin
            for (c = 0; c < 99; c = c + 1) begin
                cnt[c]    <= 11'd0;
                smooth[c] <= 11'd0;
            end
            window_pos <= 11'd0;
            levels <= 198'd0;
            window_tick <= 1'b0;
        end else begin
            window_tick <= 1'b0;
            if (ce) begin
                for (c = 0; c < 9; c = c + 1)
                    for (l = 0; l < 11; l = l + 1)
                        if (str[c] && line_active[l])
                            cnt[c*11 + l] <= cnt[c*11 + l] + 11'd1;

                if (window_pos == WIN_LAST) begin
                    window_pos <= 11'd0;
                    window_tick <= 1'b1;
                    for (c = 0; c < 99; c = c + 1) begin
                        /* verilator lint_off WIDTHEXPAND */
                        /* verilator lint_off BLKSEQ */
                        // alpha = 1/2 (MAME's set_interpolation default).
                        // The +1 rounding offset (divisor - 1) makes
                        // smooth == cnt an exact fixed point, so a steady
                        // cell settles on its true duty instead of
                        // creeping one count short of a threshold, which
                        // plain truncation would do. A decaying cell
                        // bottoms out at 1 rather than 0 for the same
                        // reason -- far below DIM_MIN, so it reads off.
                        duty = (smooth[c] + cnt[c] + 16'd1) >> 1;
                        /* verilator lint_on BLKSEQ */
                        smooth[c] <= duty[10:0];
                        if (duty >= BRIGHT_MIN)
                            levels[c*2 +: 2] <= 2'd2;
                        else if (duty >= DIM_MIN)
                            levels[c*2 +: 2] <= 2'd1;
                        else
                            levels[c*2 +: 2] <= 2'd0;
                        /* verilator lint_on WIDTHEXPAND */
                        cnt[c] <= 11'd0;
                    end
                end else
                    window_pos <= window_pos + 11'd1;
            end
        end
    end
endmodule
