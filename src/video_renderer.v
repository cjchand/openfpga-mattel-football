// Layered renderer: LED segments/dashes > label-bar text (label_rom) >
// procedural bezel background. Canvas 400x360 -- reverted from an
// initial 502x360 attempt that left zero horizontal front porch on real
// hardware (whole-screen banding/ghosting); see docs/verification.md.
// Geometry measured from assets/bezel/overlay_400x360.png -- see
// docs/superpowers/specs/2026-07-26-bezel-overlay-design.md "Measured
// geometry" for the (502-wide) source measurements this is proportionally
// rescaled from (y-axis geometry is unchanged; only x-axis positions
// scale with canvas width, by a factor of 400/502).
module video_renderer (
    input  wire [8:0]   x,
    input  wire [8:0]   y,
    input  wire [197:0] levels,
    input  wire         bezel_enable,
    output reg  [23:0]  rgb
);
    localparam [23:0] C_BG     = 24'h000000;
    localparam [23:0] C_GHOST  = 24'h1A0505; // unchanged from today's field-only look
    localparam [23:0] C_DIM    = 24'h801414;
    localparam [23:0] C_BRIGHT = 24'hFF2020;
    localparam [23:0] C_GRAY   = 24'hCBCBCB;
    localparam [23:0] C_GREEN  = 24'h187E32;

    // digit window x-origins (3 windows; window 1 & 3 hold 2 digit cells,
    // window 2 holds 3), and per-cell x-origins within each window,
    // measured/centered per the design spec at 400 width (windows at
    // x[33,131]/[141,258]/[268,366], corner accents x[0,23]/[376,399]).
    function [8:0] digit_x(input [2:0] d);
        case (d)
            3'd0: digit_x = 9'd50;   3'd1: digit_x = 9'd91;    // window 1
            3'd2: digit_x = 9'd152;  3'd3: digit_x = 9'd187;  3'd4: digit_x = 9'd222; // window 2
            3'd5: digit_x = 9'd285;  3'd6: digit_x = 9'd326;   // window 3
            default: digit_x = 9'd0;
        endcase
    endfunction
    localparam [8:0] DIGIT_Y = 9'd51; // digit cells are 24w x 32h, vertically centered in y31-101

    // segment rects within a 24x32 digit cell: {x0, y0, w, h} (unchanged
    // from the original field-only geometry -- only cell position moved)
    function [35:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {9'd4,  9'd0,  9'd16, 9'd4};   // a
            3'd1: seg_rect = {9'd20, 9'd2,  9'd4,  9'd13};  // b
            3'd2: seg_rect = {9'd20, 9'd17, 9'd4,  9'd13};  // c
            3'd3: seg_rect = {9'd4,  9'd28, 9'd16, 9'd4};   // d
            3'd4: seg_rect = {9'd0,  9'd17, 9'd4,  9'd13};  // e
            3'd5: seg_rect = {9'd0,  9'd2,  9'd4,  9'd13};  // f
            3'd6: seg_rect = {9'd4,  9'd14, 9'd16, 9'd4};   // g
            default: seg_rect = 36'd0;
        endcase
    endfunction

    function [23:0] level_color(input [1:0] lvl);
        case (lvl)
            2'd0: level_color = C_GHOST;
            2'd1: level_color = C_DIM;
            default: level_color = C_BRIGHT;
        endcase
    endfunction

    // dash-field column x-origins (9 columns) and row y-origins (3 rows),
    // measured from the field grid band y191-344. Dividers sit at
    // x=1+44*col (2px wide, see the divider loop below); each dash is
    // centered in the 42px gap between consecutive dividers: gap starts at
    // divider_end=3+44*col, dash width 16, so left margin=(42-16)/2=13 ->
    // dash_x=3+44*col+13=16+44*col. (An earlier version omitted this
    // centering margin and drew dashes flush against the left divider --
    // fixed here, see docs/verification.md.)
    function [8:0] dash_x(input [3:0] col);
        dash_x = 9'd16 + 9'd44 * col;
    endfunction
    localparam [8:0] DASH_Y0 = 9'd201;
    localparam [8:0] DASH_Y1 = 9'd267;
    localparam [8:0] DASH_Y2 = 9'd333;
    localparam [8:0] DASH_W  = 9'd16;
    localparam [8:0] DASH_H  = 9'd6;

    wire [23:0] label_rgb;
    // y is only ever 0-359, and the else-branch is only reached for
    // y in [103,131], where y-74 never exceeds 57 (fits in 6 bits). The
    // subtraction itself must stay full-width to avoid a WIDTHEXPAND on
    // its operands, which leaves its top 3 bits provably-but-not-
    // statically unused once sliced down to label_band_y's 6 bits --
    // an intentional truncation (same class of -Wall concern as
    // label_rom.v's addr_wide comment), so silence just that one signal.
    /* verilator lint_off UNUSEDSIGNAL */
    wire [8:0] y_minus_103 = y - 9'd74;
    /* verilator lint_on UNUSEDSIGNAL */
    wire [5:0] label_band_y = (y < 9'd29) ? y[5:0] : y_minus_103[5:0];
    label_rom lrom (.x(x), .band_y(label_band_y), .rgb(label_rgb));

    integer d, s, col;
    reg [8:0] rx0, ry0, rw, rh;
    reg [1:0] lvl;

    always @* begin
        // Default so every path assigns lvl -- avoids a Verilator LATCH
        // warning (lvl is only otherwise (re)assigned inside the segment/
        // dp/dash conditionals below, which don't cover every branch).
        lvl = 2'd0;

        // --- bezel background (bottom layer) ---
        if (!bezel_enable) begin
            rgb = C_BG;
        end else if (y < 9'd29 || (y >= 9'd103 && y < 9'd132)) begin
            rgb = label_rgb; // label bars: bitmap is authoritative for its own rows
        end else if (y >= 9'd29 && y < 9'd103) begin
            // corner accents + 3 digit windows are black; everything else
            // in this band is bezel gray
            if ((x < 9'd24) || (x >= 9'd33 && x < 9'd132) ||
                (x >= 9'd141 && x < 9'd259) || (x >= 9'd268 && x < 9'd367) ||
                (x >= 9'd376))
                rgb = C_BG;
            else
                rgb = C_GRAY;
        end else if (y >= 9'd132 && y < 9'd191) begin
            rgb = C_GREEN;
        end else begin
            // field grid band (y 191-344) and bottom border (y 345-359):
            // black background with 10 gray dividers (2px wide, 44px
            // pitch) bounding 9 columns
            rgb = C_BG;
            for (col = 0; col < 10; col = col + 1)
                if (x >= (9'd1 + 9'd44 * col[3:0]) && x < (9'd3 + 9'd44 * col[3:0]))
                    rgb = C_GRAY;
        end

        // --- digit segments (7 cells x 7 segments): unconditional draw,
        // same as today -- every segment rectangle always shows its
        // ghost/dim/bright color, no "is it lit" gating.
        for (d = 0; d < 7; d = d + 1)
            for (s = 0; s < 7; s = s + 1) begin
                {rx0, ry0, rw, rh} = seg_rect(s[2:0]);
                rx0 = rx0 + digit_x(d[2:0]);
                ry0 = ry0 + DIGIT_Y;
                if (x >= rx0 && x < rx0 + rw && y >= ry0 && y < ry0 + rh) begin
                    lvl = levels[(d*11 + s)*2 +: 2];
                    rgb = level_color(lvl);
                end
            end

        // decimal point: digit 3 only (window 2 cell 1, x=239), line 7,
        // 6x6 just right of and below the digit cell
        if (x >= (digit_x(3'd3) + 9'd25) && x < (digit_x(3'd3) + 9'd31) &&
            y >= (DIGIT_Y + 9'd26) && y < (DIGIT_Y + 9'd32)) begin
            lvl = levels[(3*11 + 7)*2 +: 2];
            rgb = level_color(lvl);
        end

        // dash field: 9 cols x 3 rows, unconditional draw as above
        for (col = 0; col < 9; col = col + 1) begin
            if (x >= dash_x(col[3:0]) && x < dash_x(col[3:0]) + DASH_W) begin
                if (y >= DASH_Y0 && y < DASH_Y0 + DASH_H) begin
                    lvl = levels[(col*11 + 10)*2 +: 2]; // line 10 = top row
                    rgb = level_color(lvl);
                end else if (y >= DASH_Y1 && y < DASH_Y1 + DASH_H) begin
                    lvl = levels[(col*11 + 9)*2 +: 2]; // line 9 = middle row
                    rgb = level_color(lvl);
                end else if (y >= DASH_Y2 && y < DASH_Y2 + DASH_H) begin
                    lvl = levels[(col*11 + 8)*2 +: 2]; // line 8 = bottom row
                    rgb = level_color(lvl);
                end
            end
        end
    end
endmodule
