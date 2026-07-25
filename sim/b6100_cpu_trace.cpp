// Free-running trace generator. Logs state at the same observation point as
// MAME's debugger instruction hook: before each NON-skipped instruction
// executes. Line format must stay byte-identical to tools/golden/*.tr lines:
//   T,<pc %03X>,<a %X>,<c %X>,<b %02X>,<s %03X>
#include "Vb6100_cpu.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>

static bool op_is_tl(uint8_t o) {
    return ((o & 0xf8) == 0x30) || ((o & 0xfc) == 0x38);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    if (argc != 6) {
        std::fprintf(stderr,
            "usage: b6100_trace <rom.bin> <n_instr> <kb_hex> <din_hex> <out.csv>\n");
        return 2;
    }
    uint8_t rom[1024] = {0};
    {   // mfootb.bin is a contiguous 896-byte dump; addresses 0x000-0x2FF
        // then 0x380-0x3FF (the B6100 ROM hole, per MAME's ROM_CONTINUE)
        FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
        if (std::fread(rom, 1, 0x300, f) != 0x300 ||
            std::fread(rom + 0x380, 1, 0x80, f) != 0x80) {
            std::fprintf(stderr, "ROM must be exactly 896 bytes\n"); return 2;
        }
        std::fclose(f);
    }
    long n = std::strtol(argv[2], nullptr, 0);
    unsigned kb = std::strtoul(argv[3], nullptr, 16) & 0xf;
    unsigned din = std::strtoul(argv[4], nullptr, 16) & 0xf;
    FILE* out = std::fopen(argv[5], "w");
    if (!out) { std::perror(argv[5]); return 2; }

    Vb6100_cpu d;
    d.ce = 1; d.kb = kb; d.din = din;
    d.rst_n = 0; d.clk = 0; d.eval();
    d.clk = 1; d.eval(); d.clk = 0; d.eval();
    d.rst_n = 1;

    for (long i = 0; i < n; i++) {
        d.rom_data = rom[d.rom_addr & 0x3ff];
        d.eval();
        bool skipped = d.dbg_skip && !op_is_tl(d.rom_data);
        if (!skipped)
            std::fprintf(out, "T,%03X,%X,%X,%02X,%03X\n",
                         d.dbg_pc, d.dbg_a, d.dbg_c, d.dbg_b, d.dbg_s);
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
    }
    std::fclose(out);
    if (d.dbg_illegal) {
        std::fprintf(stderr, "WARNING: illegal opcode executed during run\n");
        return 1;
    }
    return 0;
}
