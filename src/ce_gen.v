// Derives a clock enable averaging CE_HZ from a clk running at CLK_HZ, using
// the same fractional-accumulator technique the APF template itself uses
// for its 48kHz audio MCLK (see core_top.v's audgen_accum). With the
// defaults (12.288MHz core clock, 70kHz instruction rate matching Plan 2's
// CPU and Plan 3's led_capture window), the ratio 70000/12288000 divides
// the accumulator's overflow period exactly every 12288000 clocks, so the
// long-run average has zero error (only sub-cycle jitter, +-1 clk, which is
// invisible at these rates).
module ce_gen #(
    parameter CLK_HZ = 12288000,
    parameter CE_HZ  = 70000
) (
    input  wire clk,
    input  wire rst_n,
    output reg  ce
);
    // width must hold CLK_HZ itself (the largest value the accumulator
    // compares against)
    localparam ACC_W = $clog2(CLK_HZ + CE_HZ + 1);

    reg [ACC_W-1:0] accum;

    always @(posedge clk) begin
        if (!rst_n) begin
            accum <= {ACC_W{1'b0}};
            ce <= 1'b0;
        end else begin
            accum <= accum + CE_HZ[ACC_W-1:0];
            if (accum + CE_HZ[ACC_W-1:0] >= CLK_HZ[ACC_W-1:0]) begin
                accum <= accum + CE_HZ[ACC_W-1:0] - CLK_HZ[ACC_W-1:0];
                ce <= 1'b1;
            end else begin
                ce <= 1'b0;
            end
        end
    end
endmodule
