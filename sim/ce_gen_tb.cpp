// Verifies ce_gen's long-run average rate and single-cycle pulse width.
#include "Vce_gen.h"
#include "verilated.h"
#include <cstdio>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct Gen {
    Vce_gen d;
    void reset() {
        d.rst_n = 0; d.clk = 0; d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        d.rst_n = 1; d.eval();
    }
    bool tick() {
        d.clk = 1; d.eval();
        bool ce = d.ce;
        d.clk = 0; d.eval();
        return ce;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_average_rate() {
    Gen g; g.reset();
    // Over exactly CLK_HZ clocks (one second of clk time), the pulse count
    // must equal CE_HZ exactly -- the accumulator's ratio is exact
    // (70000/12288000), so there is no rounding error over a full second.
    long pulses = 0;
    for (long i = 0; i < 12288000; i++) if (g.tick()) pulses++;
    CHECK(pulses == 70000, "exactly 70000 pulses per 12288000 clocks");
}

static void test_single_cycle_width() {
    Gen g; g.reset();
    // ce must never stay high two clocks in a row.
    bool prev = false;
    for (long i = 0; i < 200000; i++) {
        bool ce = g.tick();
        CHECK(!(ce && prev), "ce never high on two consecutive clocks");
        prev = ce;
    }
}

static void test_reset_clears_pulse() {
    Gen g; g.reset();
    CHECK(g.d.ce == 0, "ce low immediately after reset");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("average_rate", test_average_rate);
    run_test("single_cycle_width", test_single_cycle_width);
    run_test("reset_clears_pulse", test_reset_clears_pulse);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: ce_gen_tb\n");
    return 0;
}
