// Real I2S audio from the CPU's 1-bit speaker line, reusing the APF
// template's own MCLK/SCLK/LRCK timing (core_top.v's audgen_* generator,
// which normally drives silence) and adding actual sample shifting.
// No DC-blocking filter or volume scaling in this pass -- deferred polish
// (see design spec's audio section); this targets clear audibility only.
module audio_i2s (
    input  wire clk_74a,
    input  wire spk,          // assumed already synchronized to this domain
    output wire audio_mclk,
    output wire audio_sclk,
    output reg  audio_lrck,
    output reg  audio_dac
);
    // MCLK ~= 12.288MHz via fractional accumulator (identical constants to
    // the template's own silence generator)
    reg  [21:0] audgen_accum;
    reg         audgen_mclk_r;
    // NOTE (deviation from brief): widened these constants from 21'd to
    // 22'd to match audgen_accum's declared width ([21:0], 22 bits). The
    // brief's literal snippet mixed a 22-bit accumulator with 21-bit
    // literals, which Verilator's -Wall flags as WIDTHEXPAND (promoted to
    // an error). Values/intent unchanged -- 742500 and 245760 fit in both
    // widths identically; this only silences the width-mismatch warning.
    localparam [21:0] CYCLE_48KHZ = 22'd122880 * 2;
    always @(posedge clk_74a) begin
        audgen_accum <= audgen_accum + CYCLE_48KHZ;
        if (audgen_accum >= 22'd742500) begin
            audgen_mclk_r <= ~audgen_mclk_r;
            audgen_accum <= audgen_accum - 22'd742500 + CYCLE_48KHZ;
        end
    end
    assign audio_mclk = audgen_mclk_r;

    // SCLK = MCLK/4
    reg [1:0] mclk_divider;
    always @(posedge audgen_mclk_r) mclk_divider <= mclk_divider + 1'b1;
    assign audio_sclk = mclk_divider[1];

    // sample: +-12000 depending on the (already-synchronized) speaker level
    wire signed [15:0] sample = spk ? 16'sd12000 : -16'sd12000;

    reg [4:0]  lrck_cnt;
    reg [15:0] shift;

    always @(negedge audio_sclk) begin
        audio_dac <= shift[15];
        shift <= {shift[14:0], 1'b0};
        lrck_cnt <= lrck_cnt + 1'b1;
        if (lrck_cnt == 5'd31) begin
            audio_lrck <= ~audio_lrck;
        end
        if (lrck_cnt == 5'd0)
            shift <= sample;
    end
endmodule
