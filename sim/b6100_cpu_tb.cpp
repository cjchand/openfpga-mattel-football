// Per-opcode unit tests for the B6100 CPU.
// ROM is served by this harness through the module's rom_addr/rom_data ports.
// Programs are laid out along the LFSR fetch order (the PC low 6 bits are an
// LFSR, not a counter) via Cpu::place().
#include "Vb6100_cpu.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <vector>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct Cpu {
    Vb6100_cpu d;
    uint8_t rom[1024] = {0};

    // replica of the rw5000 PC LFSR (rw5000base.cpp increment_pc)
    static uint16_t lfsr_next(uint16_t pc) {
        int feed = ((pc & 0x3e) == 0) ? 1 : 0;
        feed ^= (pc >> 1 ^ pc) & 1;
        return (pc & ~0x3f) | ((pc >> 1) & 0x1f) | (feed << 5);
    }

    // n-th fetch address starting from reset (n=0 -> 0x000)
    static uint16_t addr_at(int n) {
        uint16_t pc = 0;
        for (int i = 0; i < n; i++) pc = lfsr_next(pc);
        return pc;
    }

    // lay a program along the LFSR fetch order from address 0
    void place(std::initializer_list<uint8_t> prog) {
        uint16_t pc = 0;
        for (uint8_t b : prog) { rom[pc] = b; pc = lfsr_next(pc); }
    }

    void feed() { d.rom_data = rom[d.rom_addr & 0x3ff]; d.eval(); }

    void reset() {
        d.ce = 1; d.kb = 0; d.din = 1; // DIN bit0 = difficulty PRO 1 (MAME default)
        d.rst_n = 0; d.clk = 0; d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        d.rst_n = 1; feed();
    }

    void step() { feed(); d.clk = 1; d.eval(); d.clk = 0; d.eval(); feed(); }
    void steps(int n) { while (n--) step(); }
};

static void run_test(const char* name, void (*fn)(void)) {
    g_current = name;
    fn();
}

// --- Task 1 tests ---------------------------------------------------------

static void test_reset_state() {
    Cpu c; c.reset();
    CHECK(c.d.dbg_pc == 0, "PC=0 after reset");
    CHECK(c.d.dbg_a == 0 && c.d.dbg_c == 0, "A=0 C=0 after reset");
    CHECK(c.d.dbg_s == 0 && c.d.dbg_skip == 0, "S=0 skip=0 after reset");
    CHECK(c.d.str == 0 && c.d.seg == 0 && c.d.spk == 0, "outputs cleared");
}

static void test_lfsr_sequence() {
    // hardcoded spot-check of the first 8 fetch addresses, then vs. replica
    static const uint16_t first8[8] = {0x00,0x20,0x10,0x08,0x04,0x02,0x21,0x30};
    Cpu c; c.reset(); // ROM all NOPs
    for (int i = 0; i < 8; i++) {
        CHECK(c.d.dbg_pc == first8[i], "fetch address matches hardcoded LFSR seq");
        CHECK(c.d.dbg_pc == Cpu::addr_at(i), "fetch address matches replica");
        c.step();
    }
    for (int i = 8; i < 64; i++) { // full period, PU untouched
        CHECK(c.d.dbg_pc == Cpu::addr_at(i), "LFSR full period");
        CHECK((c.d.dbg_pc & 0x3c0) == 0, "PU unchanged by increment");
        c.step();
    }
}

static void test_lax_comp() {
    Cpu c;
    c.place({0x45, 0x78});   // LAX 5 -> A=~5=0xA ; COMP -> A=5
    c.reset();
    c.step();
    CHECK(c.d.dbg_a == 0xA, "LAX loads ~imm (b5000op op_lax)");
    c.step();
    CHECK(c.d.dbg_a == 0x5, "COMP complements A (op_comp)");
}

static void test_illegal_is_nop() {
    Cpu c;
    c.place({0x0e, 0x75});   // both unmapped on B6100
    c.reset();
    c.steps(2);
    CHECK(c.d.dbg_a == 0 && c.d.dbg_c == 0, "illegal ops change nothing");
    CHECK(c.d.dbg_illegal == 1, "illegal flag latched for smoke tests");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("reset_state", test_reset_state);
    run_test("lfsr_sequence", test_lfsr_sequence);
    run_test("lax_comp", test_lax_comp);
    run_test("illegal_is_nop", test_illegal_is_nop);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: b6100_tb\n");
    return 0;
}
