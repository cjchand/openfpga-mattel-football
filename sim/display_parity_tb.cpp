// Dumps our led_capture brightness levels, one CSV row per display window,
// in the same layout tools/golden/dump_display.lua produces from MAME. The
// two are compared by tools/golden/display_diff.py; see `make display-parity`.
//
// Correctness for this core's display is defined by MAME's mfootb
// (src/mame/handheld/hh_rw5000.cpp), not by our own RTL agreeing with a C++
// twin of itself -- so this harness deliberately has no golden model of its
// own. It just reports what the hardware model produced.
#include "Vfootball_system.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>

// levels is 198 bits -> Verilator exposes it as a VlWide<7> of uint32_t.
static int level_of(const Vfootball_system& d, int cell) {
    return (d.dbg_levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    if (argc != 7) {
        std::fprintf(stderr,
            "usage: display_parity <rom.bin> <n_windows> <kb_hex> <din_hex> <settle> <out.csv>\n");
        return 2;
    }
    uint8_t rom[1024] = {0};
    {
        FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
        // Same split load as football_system_tb: 0x000-0x2FF then 0x380-0x3FF.
        if (std::fread(rom, 1, 0x300, f) != 0x300 ||
            std::fread(rom + 0x380, 1, 0x80, f) != 0x80) {
            std::fprintf(stderr, "ROM must be 896 bytes\n"); return 2;
        }
        std::fclose(f);
    }
    int n_windows = std::atoi(argv[2]);
    unsigned kb = std::strtoul(argv[3], nullptr, 16) & 0xf;
    unsigned din = std::strtoul(argv[4], nullptr, 16) & 0xf;
    long settle = std::atol(argv[5]);
    const char* out_path = argv[6];
    if (n_windows <= 0) { std::fprintf(stderr, "n_windows must be > 0\n"); return 2; }

    FILE* out = std::fopen(out_path, "w");
    if (!out) { std::perror(out_path); return 2; }
    std::fprintf(out, "frame");
    for (int y = 0; y < 9; y++)
        for (int x = 0; x < 11; x++) std::fprintf(out, ",%d.%d", y, x);
    std::fprintf(out, "\n");

    Vfootball_system d;
    d.ce = 1; d.kb = 0; d.din = din & 0x1; d.score_btn = 0;
    d.px_x = 0; d.px_y = 0; d.bezel_enable = 1;
    d.rst_n = 0; d.clk = 0; d.eval();
    d.clk = 1; d.eval(); d.clk = 0; d.eval();
    d.rst_n = 1;

    long tick = 0;
    int frames = 0;
    while (frames < n_windows) {
        // Momentary inputs latch late in MAME (its ioport fields only reach
        // the CPU at the first emulated frame boundary), so hold them at idle
        // for `settle` ticks first -- same model as the `golden` target's
        // GOLDEN_SETTLE, derived in docs/verification.md.
        // score_btn is not a separate control: DIN bit 1 IS the Score button,
        // and mfootb_state::update_display() ORs its decimal point into every
        // strobe column ("4th digit DP is from the SCORE button"), bypassing
        // the CPU's seg outputs entirely. Driving din without score_btn leaves
        // line 7 dark on our side while MAME lights it -- which is how the
        // parity test caught this. Whatever wires DIN for the APF top must
        // wire score_btn from the same bit.
        if (tick == settle) { d.kb = kb; d.din = din; d.score_btn = (din >> 1) & 1; }
        d.rom_data = rom[d.rom_addr & 0x3ff];
        d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        tick++;

        if (d.window_tick) {
            frames++;
            std::fprintf(out, "%d", frames);
            for (int cell = 0; cell < 99; cell++)
                std::fprintf(out, ",%d", level_of(d, cell));
            std::fprintf(out, "\n");
        }
    }
    // A capture that silently produced nothing must not look like a pass --
    // this instrument's failure mode would otherwise be an empty file that
    // the differ reads as "no mismatches".
    if (std::ferror(out) || std::fclose(out) != 0) {
        std::fprintf(stderr, "failed writing '%s'\n", out_path);
        return 2;
    }
    std::printf("display_parity: wrote %d windows to %s\n", frames, out_path);
    return 0;
}
