// Unit tests for led_capture: duty-cycle windows -> 2-bit brightness levels.
#include "Vled_capture.h"
#include "verilated.h"
#include <cstdio>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static const int WINDOW = 1167;

struct Cap {
    Vled_capture d;
    void reset() {
        d.ce = 1; d.str = 0; d.seg = 0; d.dp_in = 0;
        d.rst_n = 0; d.clk = 0; d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        d.rst_n = 1; d.eval();
    }
    void tick() { d.clk = 1; d.eval(); d.clk = 0; d.eval(); }
    // drive one full window: LED pattern (str,seg,dp) active for `on` ticks, idle rest
    void window(int on, uint16_t str, uint16_t seg, int dp = 0) {
        for (int i = 0; i < WINDOW; i++) {
            bool active = i < on;
            d.str = active ? str : 0;
            d.seg = active ? seg : 0;
            d.dp_in = active ? dp : 0;
            tick();
        }
    }
    int level(int col, int line) {
        int led = col * 11 + line;
        // levels is 198 bits -> Verilator exposes as VlWide<7>
        return (d.levels[(led * 2) / 32] >> ((led * 2) % 32)) & 3;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_thresholds() {
    Cap c; c.reset();
    // col 3, seg line 0 (segment a): 300/1167 = 25.7% duty -> bright (>20%)
    c.window(300, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 2, "25.7% duty -> bright");
    // 100/1167 = 8.6% -> dim (>2%, <=20%)
    c.window(100, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 1, "8.6% duty -> dim");
    // 20/1167 = 1.7% -> off (<=2%)
    c.window(20, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 0, "1.7% duty -> off");
    // exact boundary: 24 ticks (>2% of 1167=23.34) -> dim; 234 (>20%=233.4) -> bright
    c.window(24, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 1, "24 ticks -> dim boundary");
    c.window(234, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 2, "234 ticks -> bright boundary");
}

static void test_line_mapping() {
    Cap c; c.reset();
    // seg bit 7 -> line 8 (BOTTOM dash row); bit 9 -> line 10 (TOP row);
    // dp_in -> line 7; all on col 0, full duty
    c.window(WINDOW, 1 << 0, 1 << 7);
    CHECK(c.level(0, 8) == 2 && c.level(0, 10) == 0, "seg[7] is line 8 (bottom)");
    c.window(WINDOW, 1 << 0, 1 << 9);
    CHECK(c.level(0, 10) == 2 && c.level(0, 8) == 0, "seg[9] is line 10 (top)");
    c.window(WINDOW, 1 << 0, 0, /*dp=*/1);
    CHECK(c.level(0, 7) == 2, "dp_in is line 7");
}

static void test_coincidence_and_hold() {
    Cap c; c.reset();
    // str and seg both on but never simultaneously -> count 0 (ATBZ-style)
    for (int i = 0; i < WINDOW; i++) {
        bool odd = i & 1;
        c.d.str = odd ? (1 << 2) : 0;
        c.d.seg = odd ? 0 : (1 << 4);
        c.tick();
    }
    CHECK(c.level(2, 4) == 0, "no str&seg coincidence -> off");
    // levels HOLD between window boundaries: light fully, then observe mid-window
    c.window(WINDOW, 1 << 2, 1 << 4);
    CHECK(c.level(2, 4) == 2, "fully lit -> bright");
    for (int i = 0; i < WINDOW / 2; i++) { c.d.str = 0; c.d.seg = 0; c.tick(); }
    CHECK(c.level(2, 4) == 2, "levels hold steady mid-window");
}

static void test_ce_gating() {
    Cap c; c.reset();
    // with ce low, nothing accumulates and the window doesn't advance
    c.d.ce = 0; c.d.str = 1 << 1; c.d.seg = 1 << 1;
    for (int i = 0; i < 3 * WINDOW; i++) c.tick();
    c.d.ce = 1;
    c.window(WINDOW, 0, 0);   // one real idle window to latch
    CHECK(c.level(1, 1) == 0, "ce=0 ticks never counted");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("thresholds", test_thresholds);
    run_test("line_mapping", test_line_mapping);
    run_test("coincidence_and_hold", test_coincidence_and_hold);
    run_test("ce_gating", test_ce_gating);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: led_capture_tb\n");
    return 0;
}
