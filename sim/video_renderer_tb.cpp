// Pixel-level tests for the field-only renderer, plus a reference frame dump.
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

static void test_dash_pixels() {
    Rend r; r.clear_levels();
    // col 4 top-row dash (line 10) bright: x origin 30+38*4=182, y 160
    r.set_level(4, 10, 2);
    CHECK(r.px(182 + 10, 160 + 3) == BRIGHT, "center of bright dash");
    CHECK(r.px(182 - 1, 160 + 3) == BG, "left of dash is background");
    CHECK(r.px(182 + 10, 160 + 6) == BG, "below dash is background");
    // same dash dim, then off (ghost)
    r.set_level(4, 10, 1);
    CHECK(r.px(182 + 10, 160 + 3) == DIM, "dim dash");
    r.set_level(4, 10, 0);
    CHECK(r.px(182 + 10, 160 + 3) == GHOST, "off dash shows ghost");
    // middle and bottom rows of col 0
    r.set_level(0, 9, 2);  r.set_level(0, 8, 1);
    CHECK(r.px(30 + 10, 220 + 3) == BRIGHT, "middle row (line 9) y=220");
    CHECK(r.px(30 + 10, 280 + 3) == DIM, "bottom row (line 8) y=280");
}

static void test_digit_segments() {
    Rend r; r.clear_levels();
    // digit 2 (x0=136, y0=40): light segment a (line 0) and g (line 6)
    r.set_level(2, 0, 2);
    r.set_level(2, 6, 1);
    CHECK(r.px(136 + 12, 40 + 2) == BRIGHT, "segment a center");
    CHECK(r.px(136 + 12, 40 + 16) == DIM, "segment g center");
    CHECK(r.px(136 + 22, 40 + 8) == GHOST, "unlit segment b shows ghost");
    CHECK(r.px(136 + 12, 40 + 8) == BG, "digit interior is background");
}

static void test_decimal_point() {
    Rend r; r.clear_levels();
    r.set_level(3, 7, 2);
    CHECK(r.px(193 + 3, 66 + 3) == BRIGHT, "dp of digit 3");
    r.set_level(3, 7, 0);
    CHECK(r.px(193 + 3, 66 + 3) == GHOST, "dp ghost when off");
    // dp exists ONLY on digit 3: col 2 line 7 must render nothing anywhere near
    r.clear_levels(); r.set_level(2, 7, 2);
    CHECK(r.px(193 + 3, 66 + 3) == GHOST || r.px(193 + 3, 66 + 3) == BG,
          "col2 line7 does not light digit3's dp");
    // Also scan a region around digit 2's own cell (x0=136, y0=40) for any
    // stray non-background, non-ghost pixel -- catches a mis-indexed dp
    // read that renders near digit 2 instead of (or in addition to)
    // digit 3, which the single dp-pixel check above wouldn't see.
    for (int y = 40; y < 80; y += 4)
        for (int x = 136; x < 168; x += 4) {
            uint32_t p = r.px(x, y);
            CHECK(p == GHOST || p == BG,
                  "col2 line7 does not stray-light any pixel near digit2's cell");
        }
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
    write_ppm("sim/renderer_reference.ppm", 400, 360, buf.data());
    std::printf("wrote sim/renderer_reference.ppm\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("dash_pixels", test_dash_pixels);
    run_test("digit_segments", test_digit_segments);
    run_test("decimal_point", test_decimal_point);
    run_test("reference_frame", test_reference_frame);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: video_renderer_tb\n");
    return 0;
}
