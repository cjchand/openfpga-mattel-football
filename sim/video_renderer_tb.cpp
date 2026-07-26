// Pixel-level tests for the layered bezel renderer (LEDs > label text >
// procedural bezel background), plus a reference frame dump.
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
static const uint32_t GRAY = 0xCBCBCB, GREEN = 0x187E32;

// Digit window x-origins (see src/video_renderer.v digit_x()): window 1 &
// 3 hold 2 digit cells, window 2 holds 3. DIGIT_Y is the shared cell top.
// Values are for the 400-wide canvas (reverted from an initial 502-wide
// attempt -- see docs/verification.md).
static const int DIGIT_X[7] = {50, 91, 152, 187, 222, 285, 326};
static const int DIGIT_Y = 51;

// Dash-field geometry (see src/video_renderer.v dash_x()/DASH_Y*).
static int dash_x(int col) { return 16 + 44 * col; }
static const int DASH_Y0 = 201, DASH_Y1 = 267, DASH_Y2 = 333;

static void test_dash_pixels() {
    Rend r; r.clear_levels();
    // col 4 top-row dash (line 10) bright: x origin 16+44*4=192, y=201
    r.set_level(4, 10, 2);
    CHECK(r.px(192 + 8, 201 + 3) == BRIGHT, "center of bright dash");
    // gap between the divider before col4 (x=1+44*4=177, 2px wide) and the
    // dash (starts at 192) is clear background; probe well inside that gap
    CHECK(r.px(192 - 10, 201 + 3) == BG, "gap left of dash is background");
    CHECK(r.px(192 + 8, 201 + 6) == BG, "below dash is background");
    // same dash dim, then off (ghost)
    r.set_level(4, 10, 1);
    CHECK(r.px(192 + 8, 201 + 3) == DIM, "dim dash");
    r.set_level(4, 10, 0);
    CHECK(r.px(192 + 8, 201 + 3) == GHOST, "off dash shows ghost");
    // middle and bottom rows of col 0 (dash_x(0)=16)
    r.set_level(0, 9, 2);  r.set_level(0, 8, 1);
    CHECK(r.px(16 + 8, 267 + 3) == BRIGHT, "middle row (line 9) y=267");
    CHECK(r.px(16 + 8, 333 + 3) == DIM, "bottom row (line 8) y=333");
}

static void test_digit_segments() {
    Rend r; r.clear_levels();
    // digit 2 (x0=152, y0=51): light segment a (line 0) and g (line 6)
    r.set_level(2, 0, 2);
    r.set_level(2, 6, 1);
    CHECK(r.px(152 + 12, 51 + 2) == BRIGHT, "segment a center");
    CHECK(r.px(152 + 12, 51 + 16) == DIM, "segment g center");
    CHECK(r.px(152 + 22, 51 + 8) == GHOST, "unlit segment b shows ghost");
    CHECK(r.px(152 + 12, 51 + 8) == BG, "digit interior is background");
}

static void test_decimal_point() {
    Rend r; r.clear_levels();
    // dp rect: x[digit_x(3)+25, +31) = [212,218), y[DIGIT_Y+26,+32) = [77,83)
    r.set_level(3, 7, 2);
    CHECK(r.px(212 + 3, 77 + 3) == BRIGHT, "dp of digit 3");
    r.set_level(3, 7, 0);
    CHECK(r.px(212 + 3, 77 + 3) == GHOST, "dp ghost when off");
    // dp exists ONLY on digit 3: col 2 line 7 must render nothing near it
    r.clear_levels(); r.set_level(2, 7, 2);
    CHECK(r.px(212 + 3, 77 + 3) == GHOST || r.px(212 + 3, 77 + 3) == BG,
          "col2 line7 does not light digit3's dp");
    // Also scan a region around digit 2's own cell (x0=152, y0=51) for any
    // stray non-background, non-ghost pixel -- catches a mis-indexed dp
    // read that renders near digit 2 instead of (or in addition to)
    // digit 3, which the single dp-pixel check above wouldn't see.
    for (int y = 51; y < 91; y += 4)
        for (int x = 152; x < 184; x += 4) {
            uint32_t p = r.px(x, y);
            CHECK(p == GHOST || p == BG,
                  "col2 line7 does not stray-light any pixel near digit2's cell");
        }
}

static void test_ghost_level_still_renders_ghost_color() {
    // Unchanged behavior check: level 0 inside a digit segment must still
    // be C_GHOST (0x1A0505), exactly as before this feature -- only the
    // segment's *position* moved, per the user's last-minute scope change
    // (keep the existing ghost look, don't touch LED drawing logic).
    // digit 0 (x0=50, y0=51), segment a rect: x[54,70), y[51,55) -- pick a
    // point well inside it.
    Rend r; r.clear_levels();
    CHECK(r.px(58, 52) == GHOST, "level-0 segment area still shows the ghost color, at its new position");
}

static void test_bezel_disabled_is_plain_black_outside_leds() {
    Rend r; r.d.bezel_enable = 0; r.clear_levels();
    CHECK(r.px(250, 15) == BG, "bezel_enable=0 shows plain black where the label bar would be");
}

static void test_bezel_enabled_shows_green_field() {
    Rend r; r.clear_levels();
    CHECK(r.px(250, 160) == GREEN, "bezel_enable=1 shows field green at y=160");
}

static void test_bezel_enabled_shows_digit_window_black() {
    Rend r; r.clear_levels();
    CHECK(r.px(100, 50) == BG, "digit window background is black");
}

static void test_bezel_enabled_shows_corner_accent_black() {
    Rend r; r.clear_levels();
    CHECK(r.px(10, 50) == BG, "corner accent is black");
}

static void test_bezel_enabled_default_is_gray() {
    Rend r; r.clear_levels();
    // corner accent is x<24, digit window 1 starts at x=33 -- probe the gap
    CHECK(r.px(28, 50) == GRAY, "gap between corner accent and digit window is bezel gray");
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
    // for label_rom's $readmemh), so reach back up to the repo's sim/ dir.
    write_ppm("../sim/renderer_reference.ppm", 400, 360, buf.data());
    std::printf("wrote ../sim/renderer_reference.ppm\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("dash_pixels", test_dash_pixels);
    run_test("digit_segments", test_digit_segments);
    run_test("decimal_point", test_decimal_point);
    run_test("ghost_level_still_renders_ghost_color", test_ghost_level_still_renders_ghost_color);
    run_test("bezel_disabled_is_plain_black_outside_leds", test_bezel_disabled_is_plain_black_outside_leds);
    run_test("bezel_enabled_shows_green_field", test_bezel_enabled_shows_green_field);
    run_test("bezel_enabled_shows_digit_window_black", test_bezel_enabled_shows_digit_window_black);
    run_test("bezel_enabled_shows_corner_accent_black", test_bezel_enabled_shows_corner_accent_black);
    run_test("bezel_enabled_default_is_gray", test_bezel_enabled_default_is_gray);
    run_test("reference_frame", test_reference_frame);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: video_renderer_tb\n");
    return 0;
}
