// Spot-checks field_rom against tools/gen_bezel_bitmaps.py's procedural
// field geometry: plain black field-column background, bright-blue
// endzone shading in addition to (not replacing) the 9 field columns, and
// real divider content (not a blank fill).
#include "Vfield_rom.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); \
                   g_failures++; } } while (0)

static uint32_t sample(Vfield_rom& d, int x, int field_y) {
    d.x = x; d.field_y = field_y;
    d.eval();
    return d.rgb;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfield_rom d;

    // x=200 (column 4's cell, well clear of both endzones and any
    // divider), a handful of rows: the plain field-column background is
    // pure black.
    uint32_t bg = sample(d, 200, 50);
    CHECK(bg == 0x000000, "plain field background is black");

    // Endzone shading (0x0D62BC, ENDZONE_RGB from
    // tools/gen_bezel_bitmaps.py) must appear somewhere in each endzone
    // column's x-range across a scan of rows -- a scan (rather than one
    // fixed row) avoids a false failure from landing on a divider within
    // the strip's y-range.
    bool left_endzone_found = false, right_endzone_found = false;
    for (int fy = 0; fy < 124; fy++) {
        if (sample(d, 20, fy) == 0x0D62BC) left_endzone_found = true;
        if (sample(d, 380, fy) == 0x0D62BC) right_endzone_found = true;
    }
    CHECK(left_endzone_found, "left endzone column (x=20) shows endzone shading somewhere");
    CHECK(right_endzone_found, "right endzone column (x=380) shows endzone shading somewhere");

    // The endzones are IN ADDITION to the 9 field columns: x=200 (a field
    // column) must never show endzone shading, at any row.
    bool field_col_shows_endzone = false;
    for (int fy = 0; fy < 124; fy++) {
        if (sample(d, 200, fy) == 0x0D62BC) field_col_shows_endzone = true;
    }
    CHECK(!field_col_shows_endzone, "field column (x=200) never shows endzone shading");

    // A row crossing the field must contain non-black pixels (dividers)
    // -- proves the ROM holds real content, not a blank/uninitialized
    // fill, and that addr math varies across x.
    bool row_has_content = false;
    for (int px = 0; px < 400; px++) {
        if (sample(d, px, 50) != 0x000000) { row_has_content = true; break; }
    }
    CHECK(row_has_content, "field_y=50 row contains non-background pixels (dividers/endzones)");

    // The field bitmap must not be cut off: field_y=120, within the 8px
    // bottom margin below the strip's bottom edge (field_y=116), must
    // still be valid content (green margin), not garbage/out-of-bounds.
    uint32_t bottom_margin = sample(d, 200, 120);
    CHECK(bottom_margin == 0x0E8A03, "bottom margin row (field_y=120) shows green, not cut off");

    // Hash-mark ticks at field_y=44 and field_y=80 (splitting the 108px
    // strip into thirds, see tools/gen_bezel_bitmaps.py's HASH_Y1/HASH_Y2)
    // must straddle each INTERNAL divider -- probe just outside column 4's
    // divider (x=29+38*4=181, so x=178 is within the tick's reach but
    // outside the 2px divider itself) and confirm it's divider-colored
    // there, not plain field black.
    CHECK(sample(d, 178, 44) == 0xCBCBCB, "hash tick at field_y=44 straddles the divider");
    CHECK(sample(d, 178, 80) == 0xCBCBCB, "hash tick at field_y=80 straddles the divider");
    CHECK(sample(d, 178, 62) == 0x000000, "no hash tick between the two hash rows");

    // Hash ticks must NOT reach into the endzones: the boundary dividers
    // (x=29 left, x=371 right, see FIELD_X0/COL_PITCH) get no tick at all,
    // so a few px into either endzone (x=15 left, x=385 right) at the hash
    // rows must show plain endzone blue, not a gray tick.
    CHECK(sample(d, 15, 44) == 0x0D62BC, "no hash tick bleeding into the left endzone");
    CHECK(sample(d, 385, 44) == 0x0D62BC, "no hash tick bleeding into the right endzone");

    // Solid border (0xCBCBCB, BORDER_RGB) frames the whole strip, inside
    // the green margin -- probe within the 4px-thick border above the
    // strip's top edge (field_y[4,8)) directly over a field column
    // (x=200, clear of any divider/hash tick).
    CHECK(sample(d, 200, 6) == 0xCBCBCB, "border shows above the strip's top edge");
    CHECK(sample(d, 200, 1) == 0x0E8A03, "still green further out, beyond the border");

    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: field_rom_tb\n");
    return 0;
}
