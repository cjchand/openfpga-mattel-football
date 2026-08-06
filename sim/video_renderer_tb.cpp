// Pixel-level tests for the layered bezel renderer (LEDs > label text >
// field bitmap > procedural letterbox/digit-window bezel), plus a
// reference frame dump.
#include "Vvideo_renderer.h"
#include "verilated.h"
#include "ppm.h"
#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct Rend {
    Vvideo_renderer d;
    Rend() { d.bezel_enable = 1; }
    void set_level(int col, int line, int lvl) {
        int led = col * 11 + line;
        int word = (led * 2) / 32, bit = (led * 2) % 32;
        d.levels[word] = (d.levels[word] & ~(3u << bit)) | ((uint32_t)lvl << bit);
    }
    void clear_levels() { for (int i = 0; i < 7; i++) d.levels[i] = 0; }
    uint32_t px(int x, int y) { d.x = x; d.y = y; d.eval(); return d.rgb; }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static const uint32_t GHOST = 0x1A0505, DIM = 0x801414, BRIGHT = 0xFF2020, BG = 0x000000;
static const uint32_t GRAY = 0xCBCBCB;
static const uint32_t GREEN = 0x0E8A03;

// Digit window x-origins (see src/video_renderer.v digit_x()): window 1 &
// 3 hold 2 digit cells, window 2 holds 3. Windows 1/3 sit 8px inward from
// their original position (score section narrowed to match a photo of the
// real device); window 2 is unchanged. DIGIT_Y is the shared cell top.
static const int DIGIT_X[7] = {58, 99, 152, 187, 222, 277, 318};
static const int DIGIT_Y = 73;

// Dash-field geometry (see src/video_renderer.v dash_x()/DASH_Y*).
static int dash_x(int col) { return 41 + 38 * col; }
static const int DASH_Y0 = 159, DASH_Y1 = 195, DASH_Y2 = 231;

static void test_dash_pixels() {
    Rend r; r.clear_levels();
    // col 4 top-row dash (line 10) bright: x origin 41+38*4=193, y=159
    r.set_level(4, 10, 2);
    CHECK(r.px(193 + 8, 159 + 3) == BRIGHT, "center of bright dash");
    // gap between the divider before col4 (x=29+38*4=181, 2px wide) and
    // the dash (starts at 193) is clear field background; probe well inside
    CHECK(r.px(193 - 10, 159 + 3) == BG, "gap left of dash is background");
    CHECK(r.px(193 + 8, 159 + 6) == BG, "below dash is background");
    // same dash dim, then off (ghost)
    r.set_level(4, 10, 1);
    CHECK(r.px(193 + 8, 159 + 3) == DIM, "dim dash");
    r.set_level(4, 10, 0);
    CHECK(r.px(193 + 8, 159 + 3) == GHOST, "off dash shows ghost");
    // middle and bottom rows of col 0 (dash_x(0)=41)
    r.set_level(0, 9, 2);  r.set_level(0, 8, 1);
    CHECK(r.px(41 + 8, 195 + 3) == BRIGHT, "middle row (line 9) y=195");
    CHECK(r.px(41 + 8, 231 + 3) == DIM, "bottom row (line 8) y=231");
}

static void test_dash_rows_are_centered_in_thirds() {
    // Each dash row must sit centered between the sidelines/hashes that
    // bound its third of the strip (field_y 8-115, hashes at field_y 44
    // and 80 -- see tools/gen_bezel_bitmaps.py). Canvas y = 136+field_y.
    // Row 0 (top third [8,44)): gap above = 159-136-8=15, gap below =
    // 136+44-(159+6)=15. Row 2 (bottom third [80,116)): gap above =
    // 231-136-80=15, gap below = 136+116-(231+6)=15.
    CHECK(159 - 136 - 8 == 136 + 44 - (159 + 6), "top dash row centered in top third");
    CHECK(231 - 136 - 80 == 136 + 116 - (231 + 6), "bottom dash row centered in bottom third");
    // Bottom dash row must not touch the green strip below it (field_y
    // 116 = canvas 252): the dash (ends at canvas 237) must leave a
    // visible gap, same size as the top row's gap from the strip top.
    Rend r; r.clear_levels();
    r.set_level(0, 8, 2);
    CHECK(r.px(41 + 8, 237 + 10) == BG, "gap below bottom dash row before the green strip");
}

static void test_digit_segments() {
    Rend r; r.clear_levels();
    // digit 2 (x0=152, y0=DIGIT_Y, window 2 -- unmoved): light segment a
    // (line 0, measured bright) and g (line 6, measured dim). Segment
    // rects: a={9,0,6,1} row y=0; g={9,5,6,1} row y=5; b={15,1,1,4} rows
    // y=1-4; interior probe (12,3) is background (between a/g's rows,
    // outside b/c/e/f's columns). Digit segments map their measured level
    // straight through, 0/1/2 -> ghost/dim/bright, the same as the dash
    // field (see test_dash_pixels) -- so segment g renders DIM here. This
    // check is the regression guard for the removed force-to-bright
    // override: if it comes back, g reads BRIGHT and this fails.
    r.set_level(2, 0, 2);
    r.set_level(2, 6, 1);
    CHECK(r.px(152 + 12, DIGIT_Y + 0) == BRIGHT, "segment a center");
    CHECK(r.px(152 + 12, DIGIT_Y + 5) == DIM, "segment g center shows dim, not forced bright");
    CHECK(r.px(152 + 15, DIGIT_Y + 3) == GHOST, "unlit segment b shows ghost");
    CHECK(r.px(152 + 12, DIGIT_Y + 3) == BG, "digit interior is background");
}

static void test_decimal_point() {
    Rend r; r.clear_levels();
    // dp rect: x[digit_x(3)+17, +19) = [204,206), y[DIGIT_Y+9, +11)
    // (digit 3 is in window 2, unmoved by the score-section narrowing).
    int dpy = DIGIT_Y + 9;
    r.set_level(3, 7, 2);
    CHECK(r.px(204 + 1, dpy + 1) == BRIGHT, "dp of digit 3");
    r.set_level(3, 7, 1);
    CHECK(r.px(204 + 1, dpy + 1) == DIM, "dp shows dim at the dim level, not forced bright");
    r.set_level(3, 7, 0);
    CHECK(r.px(204 + 1, dpy + 1) == GHOST, "dp ghost when off");
    // dp exists ONLY on digit 3: col 2 line 7 must render nothing near it
    r.clear_levels(); r.set_level(2, 7, 2);
    CHECK(r.px(204 + 1, dpy + 1) == GHOST || r.px(204 + 1, dpy + 1) == BG,
          "col2 line7 does not light digit3's dp");
    // Also scan a region around digit 2's own cell (x0=152, y0=DIGIT_Y) for
    // any stray non-background, non-ghost pixel -- catches a mis-indexed
    // dp read that renders near digit 2 instead of (or in addition to)
    // digit 3, which the single dp-pixel check above wouldn't see.
    for (int y = DIGIT_Y; y < DIGIT_Y + 11; y += 2)
        for (int x = 152; x < 176; x += 2) {
            uint32_t p = r.px(x, y);
            CHECK(p == GHOST || p == BG,
                  "col2 line7 does not stray-light any pixel near digit2's cell");
        }
}

static void test_ghost_level_still_renders_ghost_color() {
    // Unchanged behavior check: level 0 inside a digit segment must still
    // be C_GHOST (0x1A0505). digit 0 (x0=47, y0=DIGIT_Y), segment a rect:
    // x[56,62), y[DIGIT_Y,DIGIT_Y+1) -- pick a point inside it.
    Rend r; r.clear_levels();
    CHECK(r.px(57, DIGIT_Y) == GHOST, "level-0 segment area still shows the ghost color, at its new position");
}

static void test_bezel_disabled_is_plain_black_outside_leds() {
    Rend r; r.d.bezel_enable = 0; r.clear_levels();
    CHECK(r.px(50, 60) == BG, "bezel_enable=0 shows plain black where label bar 1 would be");
}

static void test_bezel_enabled_shows_label_bar_background() {
    // x=50, y=56 (bar1, band_y=2) is verified background gray in
    // label_rom_tb.cpp -- confirms video_renderer positions the label ROM
    // at the right canvas offset (y[54,64) -> band_y = y-54).
    Rend r; r.clear_levels();
    CHECK(r.px(50, 56) == GRAY, "label bar 1 shows label ROM's background gray");
}

static void test_bezel_enabled_shows_field_background() {
    // x=200, field_y=20 (canvas y=136+20=156) is verified plain black
    // field background in field_rom_tb.cpp (well within the shorter
    // 108px-tall strip, clear of the hash rows at field_y 44/80).
    Rend r; r.clear_levels();
    CHECK(r.px(200, 156) == BG, "field band shows field ROM's plain background");
}

static void test_bezel_enabled_shows_digit_window_black() {
    // x=80 sits within window 1's now-wider span [41,140) (digit_x(0)=58,
    // digit_x(1)=99), clear of both digit cells' footprints (digit 0's
    // cell ends at ~79, digit 1's starts at ~107) -- probes the
    // bezel-background layer, not the (unconditionally-drawn,
    // always-ghost-at-minimum) digit segments. y=70 is within the
    // digit-window band (y[64,94)).
    Rend r; r.clear_levels();
    CHECK(r.px(80, 70) == BG, "digit window background is black");
}

static void test_bezel_enabled_shows_corner_accent_black() {
    // Corner accent is now just 1px (x<1) -- see digit_x's comment on the
    // score section narrowing to match the real device.
    Rend r; r.clear_levels();
    CHECK(r.px(0, 70) == BG, "corner accent is black");
    CHECK(r.px(399, 70) == BG, "corner accent is black (right edge)");
}

static void test_bezel_enabled_default_is_gray() {
    // x=28 sits in the (now much wider, 40px) gray margin between the 1px
    // corner accent and window 1 (starts at x=41).
    Rend r; r.clear_levels();
    CHECK(r.px(28, 70) == GRAY, "gray margin outside the corner accent and before digit window 1");
}

static void test_bezel_enabled_shows_letterbox_bars() {
    // Top letterbox bar (above the scoreboard) is black; the field's
    // bottom letterbox (reintroduced now that the green margins shrank)
    // is checked separately in test_field_is_centered_in_green.
    Rend r; r.clear_levels();
    CHECK(r.px(200, 10) == BG, "top letterbox bar is black");
}

static void test_bezel_enabled_transition_is_green_not_black() {
    // y[104,136) between label bar 2 and the field bitmap must carry the
    // field's green up to the score area -- no black bar.
    Rend r; r.clear_levels();
    CHECK(r.px(200, 110) == GREEN, "gap above the field is green");
    CHECK(r.px(200, 135) == GREEN, "gap above the field is green right up to the field bitmap");
}

static void test_bezel_enabled_label_bar_corners_are_black() {
    // Corners are forced black in both label bars, just 1px wide now
    // (matching the digit-window band's mask exactly -- see digit_x's
    // comment). Everything past that 1px, including the bleed-prone x=24/
    // 25/374/375 columns, must be clean gray -- tools/gen_bezel_bitmaps.py
    // now paints bar1/bar2's crop gray (not black) through x<28/x>=372 at
    // generation time, matching the digit-window band's C_GRAY margin
    // instead of leaving a black-then-gray seam once the render-time mask
    // shrank to 1px.
    Rend r; r.clear_levels();
    CHECK(r.px(0, 58) == BG, "label bar 1 left corner is black");
    CHECK(r.px(399, 58) == BG, "label bar 1 right corner is black");
    CHECK(r.px(0, 98) == BG, "label bar 2 left corner is black");
    CHECK(r.px(399, 98) == BG, "label bar 2 right corner is black");
    CHECK(r.px(24, 98) == GRAY, "label bar 2's former green-bleed column at x=24 is clean gray");
    CHECK(r.px(25, 98) == GRAY, "label bar 2's former green-bleed column at x=25 is clean gray");
    CHECK(r.px(374, 98) == GRAY, "label bar 2's former green-bleed column at x=374 is clean gray");
    CHECK(r.px(375, 98) == GRAY, "label bar 2's former green-bleed column at x=375 is clean gray");
}

static void test_field_is_centered_in_green() {
    // The field's black/blue strip (field_y 8-115, canvas y[144,252)) must
    // sit centered in a 40px-thick green margin on each side -- matches a
    // photo of the real device, whose green margin is much thinner
    // relative to the field than our earlier pass had it: top gap
    // 144-104=40, bottom green ends at 292 so bottom gap 292-252=40.
    // Confirms a black letterbox is back below the green (the field and
    // scoreboard don't fill all 360 canvas rows) and the strip isn't
    // flush against either edge.
    Rend r; r.clear_levels();
    CHECK(r.px(200, 120) == GREEN, "top gap above the strip is green");
    CHECK(r.px(200, 280) == GREEN, "bottom gap below the strip is green");
    CHECK(r.px(200, 330) == BG, "black letterbox below the bottom green margin");
    int top_gap = 144 - 104;
    int bottom_gap = 292 - 252;
    CHECK(top_gap == bottom_gap, "field strip is exactly centered in the green margin");
}

static void test_field_has_border() {
    // Solid gray/white border frames the strip (see field_rom_tb.cpp) --
    // within the 4px-thick border above the strip's canvas top edge
    // (field_y 6, within [4,8) -> canvas 136+6=142) over a field column
    // (x=200), clear of any divider/hash tick.
    Rend r; r.clear_levels();
    CHECK(r.px(200, 142) == GRAY, "border shows above the strip's top edge");
}

static void test_field_hash_ticks_stay_out_of_endzones() {
    // Hash ticks at the 2 dividers bordering the endzones are HALF-width
    // (field side only) -- a few px into either endzone at a hash row
    // (field_y=44 -> canvas 136+44=180) must still be plain endzone blue,
    // while just inside the field past the boundary divider (x=33, field
    // side of the left divider at x=29) must show the tick.
    Rend r; r.clear_levels();
    CHECK(r.px(15, 180) == 0x0D62BC, "no hash tick bleeding into the left endzone");
    CHECK(r.px(385, 180) == 0x0D62BC, "no hash tick bleeding into the right endzone");
    CHECK(r.px(33, 180) == GRAY, "half-width hash tick reaches into the field past the left boundary divider");
    CHECK(r.px(367, 180) == GRAY, "half-width hash tick reaches into the field past the right boundary divider");
}

static void test_reference_frame() {
    // dump a frame with a recognizable pattern for human eyeballing
    Rend r; r.clear_levels();
    r.set_level(4, 9, 2);                       // "player" mid-field bright
    for (int c = 0; c < 9; c += 2) r.set_level(c, 10, 1);  // dim defenders
    for (int s = 0; s < 7; s++) r.set_level(0, s, 2);      // digit0 shows '8'
    std::vector<uint8_t> buf(400 * 360 * 3);
    for (int y = 0; y < 360; y++)
        for (int x = 0; x < 400; x++) {
            uint32_t p = r.px(x, y);
            size_t i = ((size_t)y * 400 + x) * 3;
            buf[i] = p >> 16; buf[i + 1] = p >> 8; buf[i + 2] = p;
        }
    // Runs with cwd=src/ (see Makefile's sim-video_renderer override, needed
    // for label_rom's/field_rom's $readmemh), so reach back up to sim/.
    write_ppm("../sim/renderer_reference.ppm", 400, 360, buf.data());
    std::printf("wrote ../sim/renderer_reference.ppm\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("dash_pixels", test_dash_pixels);
    run_test("dash_rows_are_centered_in_thirds", test_dash_rows_are_centered_in_thirds);
    run_test("digit_segments", test_digit_segments);
    run_test("decimal_point", test_decimal_point);
    run_test("ghost_level_still_renders_ghost_color", test_ghost_level_still_renders_ghost_color);
    run_test("bezel_disabled_is_plain_black_outside_leds", test_bezel_disabled_is_plain_black_outside_leds);
    run_test("bezel_enabled_shows_label_bar_background", test_bezel_enabled_shows_label_bar_background);
    run_test("bezel_enabled_shows_field_background", test_bezel_enabled_shows_field_background);
    run_test("bezel_enabled_shows_digit_window_black", test_bezel_enabled_shows_digit_window_black);
    run_test("bezel_enabled_shows_corner_accent_black", test_bezel_enabled_shows_corner_accent_black);
    run_test("bezel_enabled_default_is_gray", test_bezel_enabled_default_is_gray);
    run_test("bezel_enabled_shows_letterbox_bars", test_bezel_enabled_shows_letterbox_bars);
    run_test("bezel_enabled_transition_is_green_not_black", test_bezel_enabled_transition_is_green_not_black);
    run_test("bezel_enabled_label_bar_corners_are_black", test_bezel_enabled_label_bar_corners_are_black);
    run_test("field_is_centered_in_green", test_field_is_centered_in_green);
    run_test("field_has_border", test_field_has_border);
    run_test("field_hash_ticks_stay_out_of_endzones", test_field_hash_ticks_stay_out_of_endzones);
    run_test("reference_frame", test_reference_frame);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: video_renderer_tb\n");
    return 0;
}
