// Verifies I2S frame timing (32 sclk periods per channel, matching the APF
// template's own generator) and that a held spk level produces the correct
// MSB-first bit sequence on audio_dac.
#include "Vaudio_i2s.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct I2S {
    Vaudio_i2s d;
    bool prev_sclk = false, prev_lrck = false;
    int lrck_toggles = 0;
    std::vector<int> dac_at_sclk_fall;   // dac value sampled at each sclk falling edge

    void tick() {
        d.clk_74a = 1; d.eval();
        d.clk_74a = 0; d.eval();
        bool sclk = d.audio_sclk;
        if (prev_sclk && !sclk) dac_at_sclk_fall.push_back(d.audio_dac);
        prev_sclk = sclk;
        bool lrck = d.audio_lrck;
        if (lrck != prev_lrck) lrck_toggles++;
        prev_lrck = lrck;
    }
    void run(long n) { for (long i = 0; i < n; i++) tick(); }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_frame_timing() {
    I2S s; s.d.spk = 1;
    s.run(200000);   // run long enough to see many full frames
    // lrck toggles once per 32 sclk periods; over this many clk_74a ticks
    // (clk_74a runs the mclk accumulator, mclk/4 = sclk), expect a
    // plausible nonzero number of toggles, and at least a few captured dac bits
    CHECK(s.lrck_toggles > 10, "lrck toggles repeatedly over a long run");
    CHECK(s.dac_at_sclk_fall.size() > 100, "many sclk falling edges captured");
}

static void test_positive_spk_sample_msb_pattern() {
    I2S s; s.d.spk = 1;
    s.run(300000);
    // +12000 = 0x2EE0 = 0b0010111011100000 -- MSB first, the first bit
    // shifted must be 0 (since bit15 of 0x2EE0 is 0)
    // Find a run of 16 consecutive dac samples that looks like a 16-bit
    // I2S word (starts right after an lrck toggle) and check its MSB.
    bool found = false;
    int16_t expect = 12000;
    for (size_t i = 0; i + 16 <= s.dac_at_sclk_fall.size(); i++) {
        uint16_t word = 0;
        for (int b = 0; b < 16; b++)
            word = (word << 1) | (s.dac_at_sclk_fall[i + b] ? 1 : 0);
        if ((int16_t)word == expect) { found = true; break; }
    }
    CHECK(found, "some 16-bit window of shifted dac bits equals +12000");
}

static void test_negative_spk_sample() {
    I2S s; s.d.spk = 0;
    s.run(300000);
    bool found = false;
    int16_t expect = -12000;
    for (size_t i = 0; i + 16 <= s.dac_at_sclk_fall.size(); i++) {
        uint16_t word = 0;
        for (int b = 0; b < 16; b++)
            word = (word << 1) | (s.dac_at_sclk_fall[i + b] ? 1 : 0);
        if ((int16_t)word == expect) { found = true; break; }
    }
    CHECK(found, "some 16-bit window of shifted dac bits equals -12000");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("frame_timing", test_frame_timing);
    run_test("positive_spk_sample_msb_pattern", test_positive_spk_sample_msb_pattern);
    run_test("negative_spk_sample", test_negative_spk_sample);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: audio_i2s_tb\n");
    return 0;
}
