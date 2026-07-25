// Field-only renderer: pure combinational (x,y) -> RGB over a 400x360 canvas.
// Geometry mirrors the arrangement in MAME's mfootb.lay: 7 seven-seg digits
// (strobes 0-6, grouped 2-3-2), digit-3 decimal point, and a 9x3 dash field
// (lines 10/9/8 = top/middle/bottom rows).
module video_renderer (
    input  wire [8:0]   x,
    input  wire [8:0]   y,
    input  wire [197:0] levels,
    output reg  [23:0]  rgb
);
    localparam [23:0] C_BG     = 24'h000000;
    localparam [23:0] C_GHOST  = 24'h1A0505;
    localparam [23:0] C_DIM    = 24'h801414;
    localparam [23:0] C_BRIGHT = 24'hFF2020;

    // digit cell x origins (strobes 0-6), cell 24x32 at y=40
    function [8:0] digit_x(input [2:0] d);
        case (d)
            3'd0: digit_x = 9'd40;   3'd1: digit_x = 9'd72;
            3'd2: digit_x = 9'd136;  3'd3: digit_x = 9'd168;
            3'd4: digit_x = 9'd200;  3'd5: digit_x = 9'd264;
            3'd6: digit_x = 9'd296;  default: digit_x = 9'd0;
        endcase
    endfunction

    // segment rects within a digit cell: {x0, y0, w, h}
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

    integer d, s, col, row;
    reg [8:0] rx0, ry0, rw, rh;
    reg [1:0] lvl;
    reg [8:0] dashy;

    always @* begin
        rgb = C_BG;
        lvl = 2'd0;
        dashy = 9'd0;

        // digits: 7 cells x 7 segments
        for (d = 0; d < 7; d = d + 1)
            for (s = 0; s < 7; s = s + 1) begin
                {rx0, ry0, rw, rh} = seg_rect(s[2:0]);
                rx0 = rx0 + digit_x(d[2:0]);
                ry0 = ry0 + 9'd40;
                if (x >= rx0 && x < rx0 + rw && y >= ry0 && y < ry0 + rh) begin
                    lvl = levels[(d*11 + s)*2 +: 2];
                    rgb = level_color(lvl);
                end
            end

        // decimal point: digit 3 only, line 7, 6x6 at (193,66)
        if (x >= 9'd193 && x < 9'd199 && y >= 9'd66 && y < 9'd72) begin
            lvl = levels[(3*11 + 7)*2 +: 2];
            rgb = level_color(lvl);
        end

        // dash field: 9 cols x 3 rows, dash 20x6
        for (col = 0; col < 9; col = col + 1)
            for (row = 0; row < 3; row = row + 1) begin
                // row 0 = line 10 (top, y=160), 1 = line 9 (y=220), 2 = line 8 (y=280)
                dashy = (row == 0) ? 9'd160 : (row == 1) ? 9'd220 : 9'd280;
                rx0 = 9'd30 + 9'd38 * col[8:0];
                if (x >= rx0 && x < rx0 + 9'd20 && y >= dashy && y < dashy + 9'd6) begin
                    lvl = levels[(col*11 + (10 - row))*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
    end
endmodule
