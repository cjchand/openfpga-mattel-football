// Verifies bridge-write loading and CPU-side address-hole translation.
// The B6100 ROM's own address space has a hole (0x300-0x37F unmapped;
// 896 bytes live at 0x000-0x2FF and 0x380-0x3FF), but the loaded FILE is a
// dense, contiguous 896 bytes -- so writes are linear (file offset i ->
// bridge_addr SLOT_BASE+i) while CPU reads must translate through the hole.
#include "Vrom_loader.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static const uint32_t SLOT_BASE = 0x10000000;

struct Loader {
    Vrom_loader d;
    // DEVIATION from task-2-brief.md's verbatim testbench: added this
    // constructor. Without an initial eval() before any clock toggling,
    // Verilator's first-ever eval() call implicitly runs static/initial/
    // settle using whatever inputs are already staged (write_word() sets
    // bridge_wr=1 before calling tick() for word 0), which silently
    // "absorbs" that very first posedge into settle instead of counting
    // it as an edge -- losing the write to mem[0] only (all later words
    // work fine since didInit is already true by then). This is a known
    // Verilator gotcha, not a rom_loader.v defect: confirmed by isolating
    // the write in a standalone repro and observing it succeeds once an
    // initial eval() with clk still 0 runs before the clock is toggled.
    Loader() { d.eval(); }
    void tick() { d.clk = 1; d.eval(); d.clk = 0; d.eval(); }

    // simulate the APF bridge writing one 32-bit word at file offset
    // word_index*4 (word-aligned, as the platform's bridge bus always is)
    void write_word(int word_index, uint32_t data) {
        d.bridge_addr = SLOT_BASE + word_index * 4;
        d.bridge_wr_data = data;
        d.bridge_wr = 1;
        tick();
        d.bridge_wr = 0;
    }

    // load a 896-byte image via 224 word writes. Big-endian within each
    // word (file byte 0 -> bits[31:24]) -- confirmed against real APF
    // hardware via a debug readback during Plan 4 bring-up; the previous
    // little-endian assumption here was untestable without hardware and
    // turned out wrong (rom_loader.v's byte_sel extraction was fixed to
    // match this once the mismatch was found).
    void load(const uint8_t* rom) {
        for (int w = 0; w < 224; w++) {
            uint32_t word = (rom[w*4] << 24) | (rom[w*4+1] << 16) | (rom[w*4+2] << 8) | rom[w*4+3];
            write_word(w, word);
        }
    }

    uint8_t read(uint16_t rom_addr) {
        d.rom_addr = rom_addr;
        d.eval();
        return d.rom_data;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_load_and_hole_translate() {
    Loader l;
    uint8_t rom[896];
    for (int i = 0; i < 896; i++) rom[i] = (uint8_t)(i ^ 0xA5);   // distinctive pattern
    l.load(rom);

    // first segment: rom_addr == file offset directly (no hole below 0x300)
    CHECK(l.read(0x000) == rom[0], "addr 0x000 = file offset 0");
    CHECK(l.read(0x2FF) == rom[0x2FF], "addr 0x2FF = file offset 0x2FF (last of first segment)");

    // second segment: rom_addr 0x380 = file offset 0x300 (right after the hole)
    CHECK(l.read(0x380) == rom[0x300], "addr 0x380 = file offset 0x300 (hole translate)");
    CHECK(l.read(0x381) == rom[0x301], "addr 0x381 = file offset 0x301");
    CHECK(l.read(0x3FF) == rom[0x37F], "addr 0x3FF = file offset 0x37F (last byte)");
}

static void test_bridge_writes_outside_slot_ignored() {
    Loader l;
    uint8_t rom[896] = {0};
    l.load(rom);
    // a write to a different address region must not corrupt the ROM
    l.d.bridge_addr = 0x20000000; l.d.bridge_wr_data = 0xFFFFFFFF; l.d.bridge_wr = 1;
    l.tick();
    l.d.bridge_wr = 0;
    CHECK(l.read(0x000) == 0, "write outside SLOT_BASE range does not touch ROM");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("load_and_hole_translate", test_load_and_hole_translate);
    run_test("bridge_writes_outside_slot_ignored", test_bridge_writes_outside_slot_ignored);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: rom_loader_tb\n");
    return 0;
}
