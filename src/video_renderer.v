// Layered renderer: LED segments/dashes (procedural) > label-bar text
// (label_rom) > field background (field_rom, dividers/endzone shading) >
// procedural letterbox/digit-window bezel. Canvas 400x360 with letterbox
// padding baked in: the overlay art is scaled uniformly (not stretched) to
// fill the 400px width, leaving black bars above/below the content -- see
// docs/verification.md for why (an earlier version relied on the Pocket's
// scaler to stretch a differently-shaped buffer, which visibly distorted
// the digit displays).
// Label geometry measured from assets/bezel/overlay_400x360.png -- see
// docs/superpowers/specs/2026-07-26-bezel-overlay-design.md "Measured
// geometry" for the derivation. Field geometry is procedural (see
// tools/gen_bezel_bitmaps.py), matched to a photo of the real device:
// field_rom's bitmap holds 11 columns (endzone + 9 field columns +
// endzone), not 9 with the outer two recolored.
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
    localparam [23:0] C_GREEN  = 24'h0E8A03; // matches field_rom's GREEN_RGB (tools/gen_bezel_bitmaps.py)

    // digit window x-origins (3 windows; window 1 & 3 hold 2 digit cells,
    // window 2 holds 3). The scoreboard read too wide next to a photo of
    // the real device -- its dark score sections end about 1/3 into the
    // outermost (endzone-adjacent) field columns, not flush with the
    // field's own edges. Windows 1 and 3 (and their digit cells) shifted
    // 8px inward (+8/-8) so the whole score block narrows from 400px to
    // 320px (windows [41,140)/[141,259)/[260,359), corner accents just
    // 1px at x=0/x=399 -- see the mask below), centered with a 40px gray
    // margin on each side. Window 2 sits exactly where it always did, so
    // it didn't need to move.
    //
    // Digit *pitch* within a window: a close-up photo of the real device's
    // 9-position LED strip (bug report, 2026-07-26) shows all 9 physical
    // dome positions evenly spaced at one constant pitch -- only 7 of the
    // 9 are wired as actual digits, with the 3rd and 7th positions left
    // unpopulated as the gaps between the 3 windows. It's one continuous
    // comb, not three independently-spaced clusters. Window 2's pitch
    // (35px, digits already validated against the photo) is the anchor;
    // window 1 and window 3's digits now use that same 35px pitch instead
    // of being stretched to evenly fill their own window's box width,
    // leaving extra gray margin on the side nearest each window's own
    // blank comb slot (adjacent to window 2).
    function [8:0] digit_x(input [2:0] d);
        case (d)
            3'd0: digit_x = 9'd47;   3'd1: digit_x = 9'd82;    // window 1
            3'd2: digit_x = 9'd152;  3'd3: digit_x = 9'd187;  3'd4: digit_x = 9'd222; // window 2
            3'd5: digit_x = 9'd292;  3'd6: digit_x = 9'd327;   // window 3
            default: digit_x = 9'd0;
        endcase
    endfunction
    // digit cells are 8w x 11h, horizontally centered in the original
    // 24-wide slot (offset 8 = (24-8)/2, baked into seg_rect's x0 values
    // below) and vertically centered in the window band y[64,94) (30 rows
    // -- bumped from 24, 25% taller alongside the rest of the scoreboard,
    // see the label_rgb/field_rgb layer comments below for the full
    // breakdown): (30-11)/2=9.5 -> DIGIT_Y=64+9=73 (10px margin below vs.
    // 9 above, off by 1 since 19 doesn't split evenly). Cell size itself
    // is unchanged -- only its margin within the band grew.
    localparam [8:0] DIGIT_Y = 9'd73;

    // segment rects within the 8x11 digit cell (24-wide slot with the 8px
    // centering offset baked in), redrawn from scratch at this size rather
    // than scaling the previous 18x24 rects (which would round every
    // segment down to a barely-there 1px sliver): 1px-thick segments,
    // proportioned like a standard 7-segment glyph. {x0, y0, w, h}.
    function [35:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {9'd9,  9'd0,  9'd6, 9'd1};   // a
            3'd1: seg_rect = {9'd15, 9'd1,  9'd1, 9'd4};   // b
            3'd2: seg_rect = {9'd15, 9'd6,  9'd1, 9'd4};   // c
            3'd3: seg_rect = {9'd9,  9'd10, 9'd6, 9'd1};   // d
            3'd4: seg_rect = {9'd8,  9'd6,  9'd1, 9'd4};   // e
            3'd5: seg_rect = {9'd8,  9'd1,  9'd1, 9'd4};   // f
            3'd6: seg_rect = {9'd9,  9'd5,  9'd6, 9'd1};   // g
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

    // dash-field column x-origins (9 columns), recalibrated for
    // field_rom's 11-column layout (endzone + 9 field columns + endzone,
    // see tools/gen_bezel_bitmaps.py's FIELD_X0/COL_PITCH/DIV_W): each
    // field column is a 38px pitch (2px divider + 36px cell) starting at
    // FIELD_X0=29. Dash is centered in the 36px cell: margin=(36-16)/2=10
    // -> dash_x = 29+38*col+2+10 = 41+38*col.
    function [8:0] dash_x(input [3:0] col);
        dash_x = 9'd41 + 9'd38 * col;
    endfunction
    // Row y-origins within the field band y[136,260) (124 rows, see
    // FIELD_Y0 below), inside the bitmap's centered 108px-tall strip
    // (field_y 8-115, see tools/gen_bezel_bitmaps.py's STRIP_Y0/STRIP_Y1).
    // Two hash-mark rows (also drawn by field_rom, at field_y 44 and 80)
    // split the strip into 3 equal 36px thirds; each dash row is centered
    // in its third: third 0 center = 8+18=26, third 1 = 44+18=62, third 2
    // = 80+18=98, minus half the 6px dash height (3) for the top-left
    // origin -> field_y 23/59/95 -> canvas y 136+23=159, 136+59=195,
    // 136+95=231.
    localparam [8:0] DASH_Y0 = 9'd159;
    localparam [8:0] DASH_Y1 = 9'd195;
    localparam [8:0] DASH_Y2 = 9'd231;
    localparam [8:0] DASH_W  = 9'd16;
    localparam [8:0] DASH_H  = 9'd6;

    // Label bars: y[54,64) = bar1 (band_y 0-9), y[94,104) = bar2 (band_y
    // 10-19) -- both 10px now (see tools/gen_bezel_bitmaps.py's BAR_OUT_H),
    // 25% taller than the initial 8px pass, as part of the scoreboard
    // resize below. Explicit-width intermediates + explicit truncation to
    // avoid Verilator -Wall WIDTHEXPAND/WIDTHTRUNC (same pattern as
    // label_rom.v's/field_rom.v's addr_wide, rom_loader.v's byte_sel_rev).
    // Each subtraction's high bits are provably unused once sliced down to
    // the target width (an intentional truncation, not a lint bug) --
    // scoped lint_off/on rather than widening the slice, same rationale as
    // label_rom.v's/field_rom.v's addr comments.
    /* verilator lint_off UNUSEDSIGNAL */
    wire [23:0] label_rgb;
    wire [8:0]  y_minus_54 = y - 9'd54;
    wire [8:0]  y_minus_84 = y - 9'd84; // 94-84=10, matches bar2's band_y start
    wire [4:0]  label_band_y = (y < 9'd64) ? y_minus_54[4:0] : y_minus_84[4:0];
    label_rom lrom (.x(x), .band_y(label_band_y), .rgb(label_rgb));

    // Field background bitmap: y[136,260) -> field_y 0-123. FIELD_Y0=136
    // gives the bitmap's black/blue strip (field_y 8-115, 108px tall) a
    // 40px green margin on each side (transition 32px + field_rom's own
    // 8px internal margin = 40 top and bottom) -- matching a photo of the
    // real device (green margin there is ~39% of the field's own height).
    // The strip didn't change size and the scoreboard only grew slightly,
    // so keeping the margins fixed leaves 68px of spare canvas at the very
    // bottom -- filled with a black letterbox (see below) rather than
    // growing anything else.
    wire [23:0] field_rgb;
    wire [8:0]  y_minus_136 = y - 9'd136;
    wire [7:0]  field_y = y_minus_136[7:0];
    field_rom from (.x(x), .field_y(field_y), .rgb(field_rgb));
    /* verilator lint_on UNUSEDSIGNAL */

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
        end else if (y < 9'd54) begin
            rgb = C_BG; // top letterbox bar
        end else if (y < 9'd64) begin
            // corner accents trimmed to 1px (x<1, x>=399) -- matches the
            // digit-window band's corner width exactly (below), so the
            // black corner column is a consistent width top-to-bottom
            // instead of pinching narrower at the digit-window row.
            // Everything past that 1px is label_rgb's own gray background,
            // safe against a stray non-gray source pixel because
            // tools/gen_bezel_bitmaps.py now paints bar1/bar2's crop gray
            // (not black) through x<28/x>=372 at generation time (the
            // source crop for bar2 had stray green bled in through
            // x=25/x=374), matching the digit-window band's C_GRAY there.
            rgb = (x < 9'd1 || x >= 9'd399) ? C_BG : label_rgb; // label bar 1
        end else if (y < 9'd94) begin
            // corner accents (1px) + 3 digit windows are black; everything
            // else in this band -- including the much wider 40px margin
            // outside each window now that the windows moved inward, see
            // digit_x's comment -- is bezel gray.
            if ((x < 9'd1) || (x >= 9'd41 && x < 9'd140) ||
                (x >= 9'd141 && x < 9'd259) || (x >= 9'd260 && x < 9'd359) ||
                (x >= 9'd399))
                rgb = C_BG;
            else
                rgb = C_GRAY;
        end else if (y < 9'd104) begin
            rgb = (x < 9'd1 || x >= 9'd399) ? C_BG : label_rgb; // label bar 2
        end else if (y < 9'd136) begin
            rgb = C_GREEN; // carries the field's green up to the score area, no black bar
        end else if (y < 9'd260) begin
            rgb = field_rgb; // field bitmap: dividers, endzone shading
        end else if (y < 9'd292) begin
            rgb = C_GREEN; // green margin below the field, matching the top margin
        end else begin
            rgb = C_BG; // bottom letterbox bar -- reintroduced now that the green margins shrank
        end

        // --- digit segments (7 cells x 7 segments): unconditional draw,
        // same as today -- every segment rectangle always shows its
        // ghost/dim/bright color, no "is it lit" gating. Any lit (nonzero)
        // segment is forced to BRIGHT: led_capture.v measures the same
        // duty-cycle-based dim/bright split for digit segments as it does
        // for the dash field's player/defender LEDs, and the real ROM
        // apparently multiplexes the status digits at a lower duty cycle
        // than the ball carrier -- faithful to hardware, but the user
        // wants the score display to read at full brightness regardless,
        // matching the ball carrier's intensity. Ghost (unlit, level 0)
        // is untouched, so the always-visible segment outline look is
        // unaffected; the dash field's own dim/bright distinction (below)
        // is intentionally left alone.
        for (d = 0; d < 7; d = d + 1)
            for (s = 0; s < 7; s = s + 1) begin
                {rx0, ry0, rw, rh} = seg_rect(s[2:0]);
                rx0 = rx0 + digit_x(d[2:0]);
                ry0 = ry0 + DIGIT_Y;
                if (x >= rx0 && x < rx0 + rw && y >= ry0 && y < ry0 + rh) begin
                    lvl = levels[(d*11 + s)*2 +: 2];
                    if (lvl != 2'd0) lvl = 2'd2;
                    rgb = level_color(lvl);
                end
            end

        // decimal point: digit 3 only (window 2 cell 1), line 7, 2x2 just
        // right of and below the (now much smaller) digit cell -- right
        // edge of the cell is at +8(centering offset)+8(cell width)=16,
        // +1px gap = 17; y offset near segment d's row (10).
        if (x >= (digit_x(3'd3) + 9'd17) && x < (digit_x(3'd3) + 9'd19) &&
            y >= (DIGIT_Y + 9'd9) && y < (DIGIT_Y + 9'd11)) begin
            lvl = levels[(3*11 + 7)*2 +: 2];
            if (lvl != 2'd0) lvl = 2'd2; // force bright when lit, same as the digit segments above
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
