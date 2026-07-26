// Spot-checks label_rom against known pixel colors from the source overlay
// image, at a handful of coordinates safely inside each label bar's
// background (not inside any character glyph, so exact color is
// unambiguous regardless of quantization dithering at glyph edges).
#include "Vlabel_rom.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); \
                   g_failures++; } } while (0)

static uint32_t sample(Vlabel_rom& d, int x, int band_y) {
    d.x = x; d.band_y = band_y;
    d.eval();
    return d.rgb;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vlabel_rom d;

    // x=50, row 2 of bar 1: inside the gray background field, clear of the
    // ~24px black frame border that runs down both edges of the bar (x<24
    // and x>=376 at the 400-wide canvas) and clear of any glyph -- verified
    // directly against both the source PNG and the generated .mem files
    // before writing this check. Must be the label bar's background gray,
    // not black or green (proves the ROM is being read, not returning
    // garbage/zero).
    uint32_t bg1 = sample(d, 50, 2);
    CHECK(((bg1 >> 16) & 0xFF) > 150 && ((bg1 >> 16) & 0xFF) < 230,
          "bar1 background is light gray (R channel)");
    CHECK(((bg1 >> 16) & 0xFF) == (bg1 & 0xFF), "bar1 background is neutral gray (R==B)");

    // Same x, first row of bar 2 (band_y=29 = overlay y=103), same
    // background-gray expectation.
    uint32_t bg2 = sample(d, 50, 29);
    CHECK(((bg2 >> 16) & 0xFF) > 150 && ((bg2 >> 16) & 0xFF) < 230,
          "bar2 background is light gray (R channel)");

    // Row 15 of bar 1 crosses the "DOWN"/"FIELD POSITION"/"YARDS TO GO"
    // label text -- scanning only the interior gray field (x=30..369,
    // clear of the black frame border on both edges) at least one pixel
    // must differ from the plain background gray (bg1), proving the ROM
    // holds real image content (text), not a solid fill, and that addr
    // math varies across x (a truncation bug that wraps every x to the
    // same word would make the whole row uniform). Restricting to the
    // interior keeps this a text check rather than a trivial
    // border-vs-background check.
    bool row_has_variation = false;
    for (int px = 30; px < 370; px++) {
        if (sample(d, px, 15) != bg1) { row_has_variation = true; break; }
    }
    CHECK(row_has_variation, "bar1 row 15 contains at least one non-background pixel (label text)");

    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: label_rom_tb\n");
    return 0;
}
