// Free-running trace generator. Logs state at the same observation point as
// MAME's debugger instruction hook: before each NON-skipped instruction
// executes. Line format must stay byte-identical to tools/golden/*.tr lines:
//   T,<pc %03X>,<a %X>,<c %X>,<b %02X>,<s %03X>
//
// Alignment note: MAME's debugger halts the CPU at the reset vector *before*
// executing the first instruction, then processes -debugscript ("trace",
// "go"). Its trace hook only fires from the instruction fetched *after* that
// initial halted one, so MAME's golden trace never contains a line for the
// very first (reset-time) instruction. To stay byte-identical we skip our
// own observation of that same first instruction (i == 0) below.
//
// Settle-delay note: mfootb's momentary buttons (IN.0 kb bits, and the DIN
// "Score" bit) are ordinary MAME ioport digital fields. MAME only samples
// digital fields into their *live* value once per (emulated) video frame,
// via ioport_manager::frame_update(), which for this screenless driver runs
// off video's default 60Hz screenless timer -- NOT every CPU cycle. A field
// held from machine start (our tools/golden/hold_input.lua) therefore does
// not become visible to the CPU's read_kb()/read_din() until the first such
// frame boundary elapses; before that, MAME genuinely reads those bits as 0.
// CONFNAME/DIP-style bits (e.g. the Difficulty bit, DIN bit 0) are baked
// into the port's default live value at machine reset and need no such
// settling. To stay byte-identical with MAME's golden trace, the optional
// <settle_cycles> argument holds kb at 0 and din at (din & 0x1) for the
// first <settle_cycles> clock cycles, then switches to the full requested
// kb/din from there on -- mirroring MAME's own first-frame input latency.
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
    if (argc != 6 && argc != 7) {
        std::fprintf(stderr,
            "usage: b6100_trace <rom.bin> <n_instr> <kb_hex> <din_hex> <out.csv> "
            "[settle_cycles]\n");
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
    long settle = (argc == 7) ? std::strtol(argv[6], nullptr, 0) : 0;
    unsigned din_pre = din & 0x1;  // Difficulty bit only; no frame settle needed
    FILE* out = std::fopen(argv[5], "w");
    if (!out) { std::perror(argv[5]); return 2; }

    Vb6100_cpu d;
    d.ce = 1; d.kb = (settle > 0) ? 0 : kb; d.din = (settle > 0) ? din_pre : din;
    d.rst_n = 0; d.clk = 0; d.eval();
    d.clk = 1; d.eval(); d.clk = 0; d.eval();
    d.rst_n = 1;

    for (long i = 0; i < n; i++) {
        d.kb  = (i < settle) ? 0       : kb;
        d.din = (i < settle) ? din_pre : din;
        d.rom_data = rom[d.rom_addr & 0x3ff];
        d.eval();
        bool skipped = d.dbg_skip && !op_is_tl(d.rom_data);
        if (!skipped && i > 0)
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
