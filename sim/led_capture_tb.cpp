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
    // Hold one duty long enough for the alpha=1/2 IIR to reach its fixed
    // point. Worst case (0 -> full duty) converges in ~11 windows since the
    // error halves each time and WINDOW < 2048; 16 is comfortable margin.
    // Tests that assert a STEADY duty's level want this; tests that assert
    // the filter's own dynamics call window() directly and count.
    void settle(int on, uint16_t str, uint16_t seg, int dp = 0) {
        for (int i = 0; i < 16; i++) window(on, str, seg, dp);
    }
    // Windows of `on` duty until this cell reaches `want`, capped so a
    // never-arriving level returns the cap rather than hanging.
    int windows_until(int col, int line, int want, int on, uint16_t str,
                      uint16_t seg, int cap = 64) {
        for (int n = 1; n <= cap; n++) {
            window(on, str, seg);
            if (level(col, line) == want) return n;
        }
        return cap;
    }
    int level(int col, int line) {
        int led = col * 11 + line;
        // levels is 198 bits -> Verilator exposes as VlWide<7>
        return (d.levels[(led * 2) / 32] >> ((led * 2) % 32)) & 3;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

// Steady-state classification. Each duty is held until the IIR settles, so
// these assert the thresholds themselves; the filter's dynamics are
// test_interpolation's job.
static void test_thresholds() {
    Cap c; c.reset();
    // col 3, seg line 0 (segment a): 300/1167 = 25.7% duty -> bright (>20%)
    c.settle(300, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 2, "25.7% duty -> bright");
    // 100/1167 = 8.6% -> dim (>2%, <=20%)
    c.settle(100, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 1, "8.6% duty -> dim");
    // 20/1167 = 1.7% -> off (<=2%)
    c.settle(20, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 0, "1.7% duty -> off");
    // exact boundary: 24 ticks (>2% of 1167=23.34) -> dim; 234 (>20%=233.4) -> bright
    c.settle(24, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 1, "24 ticks -> dim boundary");
    c.settle(234, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 2, "234 ticks -> bright boundary");
}

// The smoothed value's fixed point must be the duty itself, not one count
// short of it: with plain truncation instead of the +1 rounding offset, a
// cell held at exactly the threshold settles at 23 / 233 and reads one
// level too low forever. These two are that guard.
static void test_steady_state_is_exact() {
    Cap c; c.reset();
    c.settle(24, 1 << 5, 1 << 2);
    CHECK(c.level(5, 2) == 1, "duty held at exactly DIM_MIN stays dim");
    c.reset();
    c.settle(234, 1 << 5, 1 << 2);
    CHECK(c.level(5, 2) == 2, "duty held at exactly BRIGHT_MIN stays bright");
}

// MAME's pwm_display_device smooths the duty estimate at alpha = 1/2 before
// classifying (pwm.cpp: bri = bri*(1-f) + duty*f, f = 0.5 by default). The
// window counts below are that filter's exact step response for the
// threshold duties, so they pin alpha down rather than just asserting "it
// eventually gets there": with no smoothing at all the rise takes 1 window,
// and at alpha = 1/8 it takes far more than 8.
static void test_interpolation() {
    Cap c; c.reset();
    // Rise, 0 -> duty 234 (exactly BRIGHT_MIN). smooth[] walks
    // 117,176,205,220,227,231,233,234 -- bright only on the 8th window.
    c.window(234, 1 << 4, 1 << 3);
    CHECK(c.level(4, 3) == 1, "one window at BRIGHT_MIN duty is only dim, not bright");
    c.reset();
    CHECK(c.windows_until(4, 3, 2, 234, 1 << 4, 1 << 3) == 8,
          "alpha=1/2 reaches bright in exactly 8 windows");
    // Fall, 234 -> idle. smooth[] walks 117,59,30,15 -- off on the 4th.
    CHECK(c.level(4, 3) == 2, "still bright before the step down");
    CHECK(c.windows_until(4, 3, 0, 0, 0, 0) == 4,
          "alpha=1/2 decays to off in exactly 4 windows");
}

static void test_line_mapping() {
    Cap c; c.reset();
    // seg bit 7 -> line 8 (BOTTOM dash row); bit 9 -> line 10 (TOP row);
    // dp_in -> line 7; all on col 0, full duty. settle() rather than a
    // single window so the previously-lit line has decayed away too --
    // the IIR carries a cell's level across the switch.
    c.settle(WINDOW, 1 << 0, 1 << 7);
    CHECK(c.level(0, 8) == 2 && c.level(0, 10) == 0, "seg[7] is line 8 (bottom)");
    c.settle(WINDOW, 1 << 0, 1 << 9);
    CHECK(c.level(0, 10) == 2 && c.level(0, 8) == 0, "seg[9] is line 10 (top)");
    c.settle(WINDOW, 1 << 0, 0, /*dp=*/1);
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
    run_test("steady_state_is_exact", test_steady_state_is_exact);
    run_test("interpolation", test_interpolation);
    run_test("line_mapping", test_line_mapping);
    run_test("coincidence_and_hold", test_coincidence_and_hold);
    run_test("ce_gating", test_ce_gating);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: led_capture_tb\n");
    return 0;
}
