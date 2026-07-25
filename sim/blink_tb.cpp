// Toolchain smoke test: drives the throwaway blink counter and checks its LED
// output. Deleted in Plan 2 when the first real module replaces it.
#include "Vblink.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vblink& dut) {
    dut.clk = 1; dut.eval();
    dut.clk = 0; dut.eval();
}

int main(int argc, char** argv) {
    VerilatedContext ctx;
    ctx.commandArgs(argc, argv);
    Vblink dut;

    // Hold reset for one cycle
    dut.rst_n = 0; dut.clk = 0; dut.eval();
    tick(dut);
    dut.rst_n = 1;

    if (dut.led != 0) { std::printf("FAIL: led not 0 after reset\n"); return 1; }

    // Counter is 4 bits wide, led = bit 3: high after 8 posedges
    for (int i = 0; i < 8; i++) tick(dut);
    if (dut.led != 1) { std::printf("FAIL: led not 1 after 8 clocks\n"); return 1; }

    // ...and low again 8 clocks later (wraps)
    for (int i = 0; i < 8; i++) tick(dut);
    if (dut.led != 0) { std::printf("FAIL: led did not wrap\n"); return 1; }

    std::printf("PASS: blink_tb\n");
    return 0;
}
