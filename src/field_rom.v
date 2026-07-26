// Field background bitmap: the playing-field band (green oval background,
// dividers, endzone shading), stored as an indexed bitmap. Generated
// procedurally by tools/gen_bezel_bitmaps.py's build_field_image() --
// geometry and colors measured directly from a photo of the real Mattel
// Electronics Football handheld (11 columns: 9 black field columns plus a
// blue endzone on each side, not 9 with the outer two recolored). Dash
// marks (players/defenders) are NOT part of this bitmap -- they're drawn
// procedurally on top by video_renderer.v, same as the digit segments.
module field_rom (
    input  wire [8:0]  x,        // 0-399
    input  wire [7:0]  field_y,  // 0-123
    output wire [23:0] rgb
);
    reg [7:0]  pixel_idx [0:49599];
    initial $readmemh("field_bitmap.mem", pixel_idx);

    reg [23:0] palette [0:255];
    initial $readmemh("field_palette.mem", palette);

    // Both operands widened to exactly the 16-bit result width (max
    // value 123*400+399 = 49599, fits in 16 bits with no slack bit left
    // over) to avoid a WIDTHEXPAND/WIDTHTRUNC lint warning under -Wall
    // (same pattern as label_rom.v's addr / rom_loader.v's byte_sel_rev).
    wire [15:0] addr = ({8'd0, field_y} * 16'd400) + {7'd0, x};
    assign rgb = palette[pixel_idx[addr]];
endmodule
