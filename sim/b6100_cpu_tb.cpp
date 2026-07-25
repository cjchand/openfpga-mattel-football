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

// --- Task 2 tests ---------------------------------------------------------

static void test_lb_and_ram_rw() {
    Cpu c;
    // LB 0,bank1 (0x3d: bl=0, bu=1); LAX 2 (A=0xD); EXC 0,0 (0x58: swap A/RAM)
    // then LDA 0 (0x50) reloads it
    c.place({0x3d, 0x42, 0x58, 0x50});
    c.reset(); c.steps(4);
    CHECK(c.d.dbg_a == 0xD, "EXC wrote A to RAM[0x10], LDA read it back");
    CHECK(c.d.dbg_b == 0x10, "B = {BU=1, BL=0}");
}

static void test_lb_successive_ignored() {
    Cpu c;
    // LB 7,0 (0x20) then LB 0,3 (0x3f): second LB must be ignored (op_lb quirk)
    c.place({0x20, 0x3f});
    c.reset(); c.steps(2);
    CHECK(c.d.dbg_bl == 7 && c.d.dbg_bu == 0, "successive LB ignored");
}

static void test_atb_and_delay() {
    Cpu c;
    // LAX 12 (0x43 -> A=0xC); NOP; ATB (0x77): BL=A with 1-instr ram_addr delay
    c.place({0x43, 0x00, 0x77, 0x00});
    c.reset(); c.steps(3);
    CHECK(c.d.dbg_bl == 0xC, "ATB loaded BL from A");
    CHECK(c.d.dbg_b == 0x00, "ram_addr still old BL right after ATB (bl_delay)");
    c.step();
    CHECK(c.d.dbg_b == 0x0C, "ram_addr caught up next instruction");
}

static void test_excp_excm_skip() {
    Cpu c;
    // LB 7,0 (0x20 -> BL=7); EXC 0,+1 (0x54): BL 7->8, (8&7)==0 -> skip next
    c.place({0x20, 0x54, 0x43, 0x00});   // LAX 12 must be skipped
    c.reset(); c.steps(3);
    CHECK(c.d.dbg_bl == 8, "EXCP incremented BL");
    CHECK(c.d.dbg_a == 0, "next op skipped (A unchanged)");
    Cpu m;
    // LB 0,0 (0x3c -> BL=0); EXC 0,-1 (0x5c): BL 0->0xF -> skip next
    m.place({0x3c, 0x5c, 0x43, 0x00});
    m.reset(); m.steps(3);
    CHECK(m.d.dbg_bl == 0xF, "EXCM decremented BL with wrap");
    CHECK(m.d.dbg_a == 0, "next op skipped after EXCM overflow");
}

static void test_sm_rsm_tm_tam() {
    Cpu c;
    // LB 0,0; SM 2 (0x12); TM 2 (0x0a): bit set -> no skip; LAX 1 executes;
    // RSM 2 (0x16); TM 2 (0x0a): bit clear -> skip; LAX 2 skipped
    c.place({0x3c, 0x12, 0x0a, 0x4e, 0x16, 0x0a, 0x4d, 0x00});
    c.reset(); c.steps(4);
    CHECK(c.d.dbg_a == 0x1, "TM on set bit did not skip (LAX 1 ran)");
    c.steps(4);
    CHECK(c.d.dbg_a == 0x1, "TM on clear bit skipped LAX 2");
    Cpu t;
    // LB 0,0; LAX 15 -> A=0; TAM (0x7c): RAM[0]==0==A -> skip next LAX
    t.place({0x3c, 0x4f, 0x7c, 0x45, 0x00});
    t.reset(); t.steps(4);
    CHECK(t.d.dbg_a == 0x0, "TAM equal skipped following LAX");
}

static void test_lda_bu_xor() {
    Cpu c;
    // LB 0,bank1 (0x3d); LDA 1 (0x51): A=RAM, BU ^= 1 -> BU=0
    c.place({0x3d, 0x51, 0x00});
    c.reset(); c.steps(2);
    CHECK(c.d.dbg_bu == 0, "LDA XORed BU");
    c.step();
    CHECK(c.d.dbg_b == 0x00, "BU change applies (delay covers only 0<->nonzero edge timing)");
}

// --- Task 3 tests ---------------------------------------------------------

static void test_adx_skip_semantics() {
    Cpu c;
    // LAX 15 -> A=0; ADX 12 (0x63): A += ~0x3=0xC -> 0xC, no ovf -> skip next
    c.place({0x4f, 0x63, 0x41, 0x00});
    c.reset(); c.steps(3);
    CHECK(c.d.dbg_a == 0xC, "ADX added ~imm");
    CHECK(c.d.dbg_a != 0xE, "LAX after non-overflow ADX was skipped");
    Cpu o;
    // LAX 10 -> A=5; ADX (0x64): A += ~0x4=0xB -> 0x10 ovf -> A=0, NO skip
    o.place({0x4a, 0x64, 0x41, 0x00});
    o.reset(); o.steps(3);
    CHECK(o.d.dbg_a == 0xE, "overflowing ADX did not skip (LAX 1 ran)");
}

static void test_sc_rsc_tc() {
    Cpu c;
    // SC (0x0c); TC (0x01) -> skip; LAX 1 skipped; RSC (0x0d); TC -> no skip; LAX 2 runs
    c.place({0x0c, 0x01, 0x4e, 0x0d, 0x01, 0x4d, 0x00});
    c.reset(); c.steps(3);
    CHECK(c.d.dbg_c == 1, "SC set carry");
    CHECK(c.d.dbg_a == 0, "TC-with-carry skipped LAX");
    c.steps(3);
    CHECK(c.d.dbg_c == 0, "RSC cleared carry");
    CHECK(c.d.dbg_a == 0x2, "TC-without-carry did not skip");
}

static void test_add_variants() {
    // ADD 0x70..0x73: bit1 clear -> add carry & update C; bit0 set -> skip on no ovf
    Cpu c;
    // LB 0,0; LAX 12 (A=3); EXC0 (RAM[0]=3, A=old RAM=0); LAX 5 (A=0xA);
    // SC; ADD c,s (0x71): A=0xA+3+1=0xE, C=0, no ovf -> skip; LAX 1 skipped
    c.place({0x3c, 0x4c, 0x58, 0x45, 0x0c, 0x71, 0x4e, 0x00});
    c.reset(); c.steps(6);
    CHECK(c.d.dbg_a == 0xE, "ADD included carry");
    CHECK(c.d.dbg_c == 0, "ADD updated carry from bit4");
    c.step();
    CHECK(c.d.dbg_a == 0xE, "ADD skip-on-no-overflow skipped LAX");
    Cpu n;
    // Same setup; ADD nc,ns (0x72): no carry-in, C untouched, no skip
    n.place({0x3c, 0x4c, 0x58, 0x45, 0x0c, 0x72, 0x4e, 0x00});
    n.reset(); n.steps(7);
    CHECK(n.d.dbg_c == 1, "ADD 0x72 left carry untouched");
    CHECK(n.d.dbg_a == 0x1, "ADD 0x72 did not skip (LAX 1 ran)");
}

// --- Task 4 tests ---------------------------------------------------------

static void test_tl_sets_pu_and_s() {
    Cpu c;
    // TL 2 (0x32) at addr 0: PU=2, PL = lfsr_next(0)=0x20 -> pc=0x0A0
    c.place({0x32});
    c.reset(); c.step();
    CHECK(c.d.dbg_pc == 0x0a0, "TL set PU, PL from incremented pc");
    CHECK((c.d.dbg_s >> 6) == 2, "TL copied new PU into S upper");
}

static void test_tra_call_and_ret() {
    Cpu c;
    // addr0: TRA call to PL=0x05 (op 0x80|0x05=0x85, bit6 clear -> call)
    // slot (addr 0x20) skipped; call goes to page 15 (sr_page), PL=0x05
    c.place({0x85, 0x00});
    c.rom[0x3c5] = 0x4e;              // page15:0x05 -> LAX 1 (nibble 0xE -> A=~0xE=1)
    // after LAX at 0x3C5, next fetch = lfsr walk; place RET there
    c.rom[(0x3c0) | (Cpu::lfsr_next(0x3c5) & 0x3f)] = 0x18;   // RET
    c.reset();
    c.step();                          // TRA step1 (skip armed)
    c.step();                          // slot skipped, step2 jumps
    CHECK(c.d.dbg_pc == 0x3c5, "call went to subroutine page 15, PL from TRA");
    CHECK((c.d.dbg_s & 0x3f) == 0x20, "return slot low6 pushed into S");
    c.step();                          // LAX 1 (opcode nibble 0xE) -> A=0x1
    CHECK(c.d.dbg_a == 0x1, "subroutine body executed");
    c.step();                          // RET step1
    c.step();                          // slot skipped, step2: pc = S
    CHECK(c.d.dbg_pc == (c.d.dbg_s), "RET returned to S");
}

static void test_long_jump_tl_tra() {
    Cpu c;
    // TRA jump (bit6 set): 0xC0|0x11 -> PL=0x11; slot holds TL 3 (0x33,
    // unskippable): PU=3 -> final pc = 0x0D1 (SR clear so step2 leaves PU)
    c.place({0xd1, 0x33});
    c.reset();
    c.step();                          // TRA step1
    c.step();                          // TL executes, then step2 sets PL
    CHECK(c.d.dbg_pc == 0x0d1, "TL+TRA long jump landed on page 3, PL 0x11");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("reset_state", test_reset_state);
    run_test("lfsr_sequence", test_lfsr_sequence);
    run_test("lax_comp", test_lax_comp);
    run_test("illegal_is_nop", test_illegal_is_nop);
    run_test("lb_and_ram_rw", test_lb_and_ram_rw);
    run_test("lb_successive_ignored", test_lb_successive_ignored);
    run_test("atb_and_delay", test_atb_and_delay);
    run_test("excp_excm_skip", test_excp_excm_skip);
    run_test("sm_rsm_tm_tam", test_sm_rsm_tm_tam);
    run_test("lda_bu_xor", test_lda_bu_xor);
    run_test("adx_skip_semantics", test_adx_skip_semantics);
    run_test("sc_rsc_tc", test_sc_rsc_tc);
    run_test("add_variants", test_add_variants);
    run_test("tl_sets_pu_and_s", test_tl_sets_pu_and_s);
    run_test("tra_call_and_ret", test_tra_call_and_ret);
    run_test("long_jump_tl_tra", test_long_jump_tl_tra);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: b6100_tb\n");
    return 0;
}
