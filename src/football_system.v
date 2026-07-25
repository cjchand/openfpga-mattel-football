// Whole-system wrapper for simulation (and later, the core of the APF top):
// CPU + LED capture + renderer. ROM stays external (harness serves it now,
// APF-loaded BRAM in Plan 4).
module football_system (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        ce,
    output wire [9:0]  rom_addr,
    input  wire [7:0]  rom_data,
    input  wire [3:0]  kb,
    input  wire [3:0]  din,
    input  wire        score_btn,   // drives the display's decimal point line
    input  wire [8:0]  px_x,
    input  wire [8:0]  px_y,
    output wire [23:0] px_rgb,
    output wire        spk,
    output wire        window_tick
);
    wire [8:0]   str;
    wire [9:0]   seg;
    wire [197:0] levels;

    /* verilator lint_off PINCONNECTEMPTY */
    b6100_cpu cpu (
        .clk(clk), .rst_n(rst_n), .ce(ce),
        .rom_addr(rom_addr), .rom_data(rom_data),
        .kb(kb), .din(din),
        .str(str), .seg(seg), .spk(spk),
        .dbg_pc(), .dbg_a(), .dbg_bl(), .dbg_bu(), .dbg_b(),
        .dbg_c(), .dbg_s(), .dbg_skip(), .dbg_illegal()
    );
    /* verilator lint_on PINCONNECTEMPTY */

    led_capture cap (
        .clk(clk), .rst_n(rst_n), .ce(ce),
        .str(str), .seg(seg), .dp_in(score_btn),
        .levels(levels), .window_tick(window_tick)
    );

    video_renderer rend (
        .x(px_x), .y(px_y), .levels(levels), .rgb(px_rgb)
    );
endmodule
