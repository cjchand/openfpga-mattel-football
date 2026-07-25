# APF Integration Implementation Plan (Plan 4 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the core actually boot and play on a physical Analogue Pocket: real ROM loaded from the SD card, a real ~70 kHz clock enable driving the CPU, real audio out the speaker, and Pocket controls mapped to the game (spec milestone 5).

**Architecture:** Four small, independently-testable pieces wired together inside the vendored template's `core_top.v`: a clock-enable generator (`ce_gen`) deriving ~70 kHz from the Pocket's 12.288 MHz core clock via an accumulator (the same technique the template already uses for its audio MCLK); a ROM loader (`rom_loader`) that watches the APF bridge for the SD-card-supplied ROM file and serves it to the CPU through the same address-hole translation the simulation harnesses already use; an I²S audio shifter (`audio_i2s`) that turns the CPU's raw 1-bit speaker line into real serial audio, extending the template's existing (currently silent) I²S generator; and the integration itself, which wires `football_system` (Plan 3) into the video/audio/input pins the template already exposes. The final task ends with a human checkpoint: play the real game on a real Pocket.

**Tech Stack:** Verilog-2001 (Quartus-synthesizable — no SystemVerilog constructs), Verilator + C++17 for the three new modules' unit tests, Docker Quartus (Plan 1's toolchain) for the bitstream, a physical Analogue Pocket + SD card for the final checkpoint.

**Spec:** `docs/superpowers/specs/2026-07-25-mattel-football-core-design.md` (milestone 5 — "Core boots on Pocket: playable game, sound, controls").

## Global Constraints

- **Ground truth for every APF interface below is either the vendored template (`src/fpga/core/core_top.v`, `src/fpga/apf/common.v`) or a real, currently-installed, working core's JSON on the user's own Pocket SD card (`/Volumes/Pocket/Cores/*/data.json`, `core.json`) — not documentation guesses.** Where this plan states a signal name, bit position, or JSON field, it was read directly from one of those two sources.
- **Clock domains:** `clk_core_12288` (12.288 MHz, from the template's pre-built `mf_pllbase` — do not regenerate or reconfigure this Quartus IP) is the ONE clock the whole core (CPU, capture, renderer, our new modules) runs on. `ce_gen` produces a single-cycle-wide enable pulse on this clock, averaging 70000 Hz, which becomes `football_system`'s `ce` input (Plan 2/3's CPU and `led_capture` both already expect their `ce` to be the ~70 kHz *instruction* rate, not the raw 280 kHz oscillator — this was flagged explicitly in Plan 3's final review). Video scanout runs every `clk_core_12288` cycle regardless of `ce` (the renderer is combinational; `football_system`'s `levels` bus only updates ~60×/sec via `led_capture`'s window, so continuous scanout at full pixel rate is safe and intentional).
- **Video timing:** the template's PLL is fixed at 12.288 MHz and its existing timing generator hits exactly 204800 pixel-clocks/frame at 60 Hz (`400×512` for the template's 320×240 mode). Plan 3's `video_renderer` is a 400×360 canvas, so this plan retimes to `H_ACTIVE=400, H_TOTAL=512, V_ACTIVE=360, V_TOTAL=400` — same product (204800), same 60.000 Hz, same untouched PLL. `video.json`'s `scaler_modes` becomes `{"width":400,"height":360,"aspect_w":10,"aspect_h":9}` (400:360 reduces exactly to 10:9).
- **ROM data-slot loading** follows the exact pattern read from `/Volumes/Pocket/Cores/ericlewis.DonkeyKong/data.json` (a real, working, single-fixed-file arcade ROM core): `{"id":0,"name":"ROM","required":true,"parameters":0,"filename":"mfootb.bin","address":"0x10000000"}`. APF places the file's bytes, byte-for-byte, contiguous, starting at bridge address `0x10000000` — no hole re-shuffling on load; the hole only matters when the CPU's own 10-bit address is translated to a dense storage index at *read* time (same translation Plan 2/3's sim harnesses perform, just done once per read instead of once at file-load time). The file the human places on the SD card is the *same 896-byte `mfootb.bin`* already used in `sim/roms/` — copy it to `Assets/ex_platform/common/mfootb.bin` (the `Assets/<platform_id>/common/<filename>` layout is confirmed from the same real core's installed Assets folder; `ex_platform` is the template's placeholder platform id — renaming platform/branding identity is Plan 5's job, per the design spec).
- **Input mapping** (from `core_top.v`'s own port comment, the platform's standard key bitmap — true for every core, not something `input.json` needs to redeclare to have basic function): `cont1_key[0]`=dpad_up, `[1]`=dpad_down, `[2]`=dpad_left, `[3]`=dpad_right, `[4]`=face_a, `[5]`=face_b, `[6]`=face_x, `[7]`=face_y, `[14]`=face_select, `[15]`=face_start. Per the design spec's Controls table: D-pad Up/Down/Right → Up/Down/Forward, A → Kick, Start → Score, Select → Status. `input.json` label customization (so the Pocket's remapping UI shows "Forward"/"Kick" instead of generic names) and the "original button layout" toggle are Plan 5 work; this plan wires the bits directly and leaves `input.json` as its current valid empty stub.
- **Difficulty (PRO 1/PRO 2)** is DIN bit 0 (Plan 2 convention: `din[0]`, default 1 = PRO 1). Hardcode `din = 4'b0001` in this plan (settings-menu wiring is Plan 5).
- Every commit message ends with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`; Makefile recipes use tabs; Verilator runs with `-Wall`, pristine output, scoped lint pragmas only.
- Do not modify `src/fpga/core/mf_pllbase*` (pre-built Quartus IP) or `src/fpga/apf/*` (framework internals) — this plan only edits `core_top.v` and the `dist/Cores/.../*.json` manifests, plus adds new files under `src/`.

## File Structure

- `src/ce_gen.v` — accumulator-based ~70 kHz clock-enable generator (Task 1)
- `src/rom_loader.v` — bridge-write BRAM with CPU-side address-hole translation (Task 2)
- `src/audio_i2s.v` — real I²S serialization of the CPU's speaker line (Task 3)
- `sim/ce_gen_tb.cpp`, `sim/rom_loader_tb.cpp`, `sim/audio_i2s_tb.cpp` — unit testbenches (Tasks 1–3)
- `src/fpga/core/core_top.v` — modified in place: instantiate all of the above plus `football_system` (Task 4)
- `dist/Cores/Developer.Core Template/data.json`, `video.json` — modified (Task 4)
- `Makefile` — `SIM_TESTS` updates (Tasks 1–3)

---

### Task 1: `ce_gen` — ~70 kHz clock enable from the 12.288 MHz core clock

**Files:**
- Create: `src/ce_gen.v`
- Create: `sim/ce_gen_tb.cpp`
- Modify: `Makefile` (`SIM_TESTS := b6100_cpu led_capture video_renderer ce_gen`)

**Interfaces:**
- Produces: `module ce_gen #(parameter CLK_HZ = 12288000, parameter CE_HZ = 70000) (input clk, input rst_n, output reg ce)`. `ce` pulses high for exactly one `clk` cycle at a time, at a long-run-average rate of `CE_HZ`. Task 4 wires this module's `ce` output directly to `football_system`'s `ce` input.

- [ ] **Step 1: Write the failing testbench**

Create `sim/ce_gen_tb.cpp`:

```cpp
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
```

- [ ] **Step 2: Update Makefile and run to verify failure**

Change `SIM_TESTS := b6100_cpu led_capture video_renderer` to `SIM_TESTS := b6100_cpu led_capture video_renderer ce_gen`.
Run: `make sim` — Expected: FAIL, `src/ce_gen.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/ce_gen.v`:

```verilog
// Derives a clock enable averaging CE_HZ from a clk running at CLK_HZ, using
// the same fractional-accumulator technique the APF template itself uses
// for its 48kHz audio MCLK (see core_top.v's audgen_accum). With the
// defaults (12.288MHz core clock, 70kHz instruction rate matching Plan 2's
// CPU and Plan 3's led_capture window), the ratio 70000/12288000 divides
// the accumulator's overflow period exactly every 12288000 clocks, so the
// long-run average has zero error (only sub-cycle jitter, +-1 clk, which is
// invisible at these rates).
module ce_gen #(
    parameter CLK_HZ = 12288000,
    parameter CE_HZ  = 70000
) (
    input  wire clk,
    input  wire rst_n,
    output reg  ce
);
    // width must hold CLK_HZ itself (the largest value the accumulator
    // compares against)
    localparam ACC_W = $clog2(CLK_HZ + CE_HZ + 1);

    reg [ACC_W-1:0] accum;

    always @(posedge clk) begin
        if (!rst_n) begin
            accum <= {ACC_W{1'b0}};
            ce <= 1'b0;
        end else begin
            accum <= accum + CE_HZ[ACC_W-1:0];
            if (accum + CE_HZ[ACC_W-1:0] >= CLK_HZ[ACC_W-1:0]) begin
                accum <= accum + CE_HZ[ACC_W-1:0] - CLK_HZ[ACC_W-1:0];
                ce <= 1'b1;
            end else begin
                ce <= 1'b0;
            end
        end
    end
endmodule
```

- [ ] **Step 4: Run to verify pass**

Run: `make sim` — Expected: `PASS: ce_gen_tb` alongside all prior passes, `-Wall` clean. (`test_average_rate` runs 12.288M simulated cycles; this takes a few seconds under Verilator, not minutes — if it hangs, something is wrong with the accumulator width, not the test.)

- [ ] **Step 5: Commit**

```bash
git add src/ce_gen.v sim/ce_gen_tb.cpp Makefile
git commit -m "feat: ce_gen — accumulator-based ~70kHz clock enable from 12.288MHz core clock

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: `rom_loader` — APF bridge-loaded ROM with address-hole translation

**Files:**
- Create: `src/rom_loader.v`
- Create: `sim/rom_loader_tb.cpp`
- Modify: `Makefile` (`SIM_TESTS := ... rom_loader`)

**Interfaces:**
- Consumes: nothing from earlier tasks (standalone).
- Produces: `module rom_loader #(parameter SLOT_BASE = 32'h10000000) (input clk, input bridge_wr, input [31:0] bridge_addr, input [31:0] bridge_wr_data, input [9:0] rom_addr, output wire [7:0] rom_data)`. Task 4 wires `rom_addr`/`rom_data` directly to `football_system`'s identically-named ports (Plan 3's `football_system` already exposes `rom_addr`/`rom_data` for exactly this purpose).

- [ ] **Step 1: Write the failing testbench**

Create `sim/rom_loader_tb.cpp`:

```cpp
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

    // load a 896-byte image via 224 word writes, little-endian within each word
    void load(const uint8_t* rom) {
        for (int w = 0; w < 224; w++) {
            uint32_t word = rom[w*4] | (rom[w*4+1] << 8) | (rom[w*4+2] << 16) | (rom[w*4+3] << 24);
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
```

- [ ] **Step 2: Update Makefile and run to verify failure**

`SIM_TESTS := b6100_cpu led_capture video_renderer ce_gen rom_loader`
Run: `make sim` — Expected: FAIL, `src/rom_loader.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/rom_loader.v`:

```verilog
// APF bridge-loaded ROM for the B6100 core. The SD-card-supplied file
// (sim/roms/mfootb.bin, copied to Assets/<platform>/common/mfootb.bin on
// the Pocket) is 896 contiguous bytes; APF writes it verbatim, one 32-bit
// word at a time, starting at bridge address SLOT_BASE (matches data.json's
// "address": "0x10000000"). The CPU's own 10-bit address space has a hole
// (0x300-0x37F unmapped -- see b6100_cpu.v / the B6100's real ROM map), so
// reads translate CPU address -> dense file-offset index; writes need no
// such translation since the file itself has no hole.
module rom_loader #(
    parameter [31:0] SLOT_BASE = 32'h10000000
) (
    input  wire        clk,
    input  wire        bridge_wr,
    input  wire [31:0] bridge_addr,
    input  wire [31:0] bridge_wr_data,
    input  wire [9:0]  rom_addr,
    output wire [7:0]  rom_data
);
    // 896 bytes = 224 32-bit words, addressed 0..223 (8 bits)
    reg [31:0] mem [0:223];

    wire in_slot = (bridge_addr[31:24] == SLOT_BASE[31:24]);
    wire [7:0] wr_word_idx = bridge_addr[9:2];

    always @(posedge clk)
        if (bridge_wr && in_slot)
            mem[wr_word_idx] <= bridge_wr_data;

    // hole translate: CPU addresses 0x380-0x3FF are file offsets 0x300-0x37F
    // (the 0x300-0x37F CPU range is simply never generated by the CPU, per
    // the B6100's own program counter behavior, so no defensive clamping
    // is needed beyond this arithmetic)
    wire [9:0] dense_addr = (rom_addr >= 10'h380) ? (rom_addr - 10'h80) : rom_addr;
    wire [7:0] rd_word_idx = dense_addr[9:2];
    wire [1:0] byte_sel = dense_addr[1:0];

    assign rom_data = mem[rd_word_idx][byte_sel*8 +: 8];
endmodule
```

- [ ] **Step 4: Run to verify pass**

Run: `make sim` — Expected: `PASS: rom_loader_tb` alongside all prior passes, `-Wall` clean.

- [ ] **Step 5: Commit**

```bash
git add src/rom_loader.v sim/rom_loader_tb.cpp Makefile
git commit -m "feat: rom_loader — APF bridge-loaded ROM with CPU address-hole translation

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: `audio_i2s` — real speaker audio over I²S

**Files:**
- Create: `src/audio_i2s.v`
- Create: `sim/audio_i2s_tb.cpp`
- Modify: `Makefile` (`SIM_TESTS := ... audio_i2s`)

**Interfaces:**
- Consumes: a synchronized 1-bit speaker signal (Task 4 handles the clock-domain crossing from the CPU's speaker output into this module's `mclk` domain — this module assumes its `spk` input is already stable/synchronized).
- Produces: `module audio_i2s (input clk_74a, input spk, output audio_mclk, output audio_sclk, output reg audio_lrck, output reg audio_dac)`. Task 4 wires `audio_mclk`/`audio_dac`/`audio_lrck` directly to `core_top`'s identically-purposed output ports (which currently drive a silence generator to be replaced).

Background: the template's existing (silent) generator derives a 12.288 MHz-ish MCLK from `clk_74a` via an accumulator, divides by 4 for a 3.072 MHz SCLK, and counts 32 SCLK periods per audio channel (a standard 16-bit-in-32-bit-slot I²S frame, switching L/R every 32 counts). This task reuses that exact timing and adds real bit-shifting: at the start of each 32-count half-frame, load a signed 16-bit sample derived from `spk` (`spk ? +12000 : -12000` — loud enough to be clearly audible, no DC-blocking/volume-scale filter in this pass, which is an explicitly deferred polish item), then shift its bits out MSB-first on the following 16 SCLK edges, silence for the remaining 16 (matching the "16 active bits then 16 dummy bits" comment already in the template).

- [ ] **Step 1: Write the failing testbench**

Create `sim/audio_i2s_tb.cpp`:

```cpp
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
```

- [ ] **Step 2: Update Makefile and run to verify failure**

`SIM_TESTS := b6100_cpu led_capture video_renderer ce_gen rom_loader audio_i2s`
Run: `make sim` — Expected: FAIL, `src/audio_i2s.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/audio_i2s.v`:

```verilog
// Real I2S audio from the CPU's 1-bit speaker line, reusing the APF
// template's own MCLK/SCLK/LRCK timing (core_top.v's audgen_* generator,
// which normally drives silence) and adding actual sample shifting.
// No DC-blocking filter or volume scaling in this pass -- deferred polish
// (see design spec's audio section); this targets clear audibility only.
module audio_i2s (
    input  wire clk_74a,
    input  wire spk,          // assumed already synchronized to this domain
    output wire audio_mclk,
    output wire audio_sclk,
    output reg  audio_lrck,
    output reg  audio_dac
);
    // MCLK ~= 12.288MHz via fractional accumulator (identical constants to
    // the template's own silence generator)
    reg  [21:0] audgen_accum;
    reg         audgen_mclk_r;
    localparam [20:0] CYCLE_48KHZ = 21'd122880 * 2;
    always @(posedge clk_74a) begin
        audgen_accum <= audgen_accum + CYCLE_48KHZ;
        if (audgen_accum >= 21'd742500) begin
            audgen_mclk_r <= ~audgen_mclk_r;
            audgen_accum <= audgen_accum - 21'd742500 + CYCLE_48KHZ;
        end
    end
    assign audio_mclk = audgen_mclk_r;

    // SCLK = MCLK/4
    reg [1:0] mclk_divider;
    always @(posedge audgen_mclk_r) mclk_divider <= mclk_divider + 1'b1;
    assign audio_sclk = mclk_divider[1];

    // sample: +-12000 depending on the (already-synchronized) speaker level
    wire signed [15:0] sample = spk ? 16'sd12000 : -16'sd12000;

    reg [4:0]  lrck_cnt;
    reg [15:0] shift;

    always @(negedge audio_sclk) begin
        audio_dac <= shift[15];
        shift <= {shift[14:0], 1'b0};
        lrck_cnt <= lrck_cnt + 1'b1;
        if (lrck_cnt == 5'd31) begin
            audio_lrck <= ~audio_lrck;
        end
        if (lrck_cnt == 5'd0)
            shift <= sample;
    end
endmodule
```

- [ ] **Step 4: Run to verify pass**

Run: `make sim` — Expected: `PASS: audio_i2s_tb` alongside all prior passes, `-Wall` clean. (If the MSB-pattern tests can't find a matching window, the most likely cause is the `lrck_cnt==0` reload racing the `shift[15]` output on the same edge — trace one lrck period by hand against the code before changing constants.)

- [ ] **Step 5: Commit**

```bash
git add src/audio_i2s.v sim/audio_i2s_tb.cpp Makefile
git commit -m "feat: audio_i2s — real I2S audio from the CPU speaker line

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Wire it all into `core_top.v`; build, load, and boot on a real Pocket

**Files:**
- Modify: `src/fpga/core/core_top.v`
- Modify: `dist/Cores/Developer.Core Template/data.json`
- Modify: `dist/Cores/Developer.Core Template/video.json`
- Modify: `docs/verification.md` (record the working on-Pocket setup)

**Interfaces:**
- Consumes: `ce_gen` (Task 1), `rom_loader` (Task 2), `audio_i2s` (Task 3), `football_system` (Plan 3, ports: `clk, rst_n, ce, rom_addr, rom_data, kb, din, score_btn, px_x, px_y, px_rgb, spk, window_tick`), `synch_2` (already vendored at `src/fpga/apf/common.v`, ports `(i, o, clk, rise, fall)` — used here exactly as `core_top.v` already uses `synch_3` for `pll_core_locked`).
- Produces: a bootable core. Nothing downstream in this project consumes this task's interface — it is the integration point.

- [ ] **Step 1: Update `data.json`**

Edit `dist/Cores/Developer.Core Template/data.json` to:

```json
{
    "data": {
        "magic": "APF_VER_1",
        "data_slots": [
            {
                "id": 0,
                "name": "ROM",
                "required": true,
                "parameters": 0,
                "filename": "mfootb.bin",
                "address": "0x10000000"
            }
        ]
    }
}
```

- [ ] **Step 2: Update `video.json`**

Edit `dist/Cores/Developer.Core Template/video.json` to:

```json
{
    "video": {
        "magic": "APF_VER_1",
        "scaler_modes": [
            {
                "width": 400,
                "height": 360,
                "aspect_w": 10,
                "aspect_h": 9,
                "rotation": 0,
                "mirror": 0
            }
        ]
    }
}
```

- [ ] **Step 3: Replace the video generator's timing constants in `core_top.v`**

In `src/fpga/core/core_top.v`, find the `localparam` block:

```verilog
    localparam  VID_V_BPORCH = 'd10;
    localparam  VID_V_ACTIVE = 'd240;
    localparam  VID_V_TOTAL = 'd512;
    localparam  VID_H_BPORCH = 'd10;
    localparam  VID_H_ACTIVE = 'd320;
    localparam  VID_H_TOTAL = 'd400;
```

Replace with:

```verilog
    // 400x360 to match video_renderer's canvas (Plan 3). Same product
    // (H_TOTAL*V_TOTAL=204800) as the template's original 320x240 mode, so
    // the fixed 12.288MHz PLL still yields exactly 60.000Hz with no PLL
    // changes: 204800 * 60 = 12,288,000.
    localparam  VID_V_BPORCH = 'd10;
    localparam  VID_V_ACTIVE = 'd360;
    localparam  VID_V_TOTAL = 'd400;
    localparam  VID_H_BPORCH = 'd10;
    localparam  VID_H_ACTIVE = 'd400;
    localparam  VID_H_TOTAL = 'd512;
```

- [ ] **Step 4: Replace the flat-color video generator body with the real renderer**

Find, inside the same `always @(posedge clk_core_12288 or negedge reset_n)` block:

```verilog
        // inactive screen areas are black
        vidout_rgb <= 24'h0;
        // generate active video
        if(x_count >= VID_H_BPORCH && x_count < VID_H_ACTIVE+VID_H_BPORCH) begin

            if(y_count >= VID_V_BPORCH && y_count < VID_V_ACTIVE+VID_V_BPORCH) begin
                // data enable. this is the active region of the line
                vidout_de <= 1;
                
                vidout_rgb[23:16] <= 8'd60;
                vidout_rgb[15:8]  <= 8'd60;
                vidout_rgb[7:0]   <= 8'd60;
                
            end 
        end
```

Replace with:

```verilog
        // inactive screen areas are black
        vidout_rgb <= 24'h0;
        // generate active video: football_system's renderer is pure
        // combinational (x,y)->RGB, so it's already valid this same cycle
        // for the CURRENT visible_x/visible_y before we register the result
        if(x_count >= VID_H_BPORCH && x_count < VID_H_ACTIVE+VID_H_BPORCH) begin

            if(y_count >= VID_V_BPORCH && y_count < VID_V_ACTIVE+VID_V_BPORCH) begin
                // data enable. this is the active region of the line
                vidout_de <= 1;
                vidout_rgb <= football_rgb;
            end 
        end
```

Delete the now-unused `square_x`/`square_y` declarations if present (`reg [9:0] square_x = 'd135; reg [9:0] square_y = 'd95;`) — they were template placeholders never referenced elsewhere.

- [ ] **Step 5: Delete the silence generator and wire in real audio**

Find and delete this entire block (the template's silence generator, now superseded):

```verilog
//
// audio i2s silence generator
// see other examples for actual audio generation
//

assign audio_mclk = audgen_mclk;
assign audio_dac = audgen_dac;
assign audio_lrck = audgen_lrck;

// generate MCLK = 12.288mhz with fractional accumulator
    reg         [21:0]  audgen_accum;
    reg                 audgen_mclk;
    parameter   [20:0]  CYCLE_48KHZ = 21'd122880 * 2;
always @(posedge clk_74a) begin
    audgen_accum <= audgen_accum + CYCLE_48KHZ;
    if(audgen_accum >= 21'd742500) begin
        audgen_mclk <= ~audgen_mclk;
        audgen_accum <= audgen_accum - 21'd742500 + CYCLE_48KHZ;
    end
end

// generate SCLK = 3.072mhz by dividing MCLK by 4
    reg [1:0]   aud_mclk_divider;
    wire        audgen_sclk = aud_mclk_divider[1] /* synthesis keep*/;
    reg         audgen_lrck_1;
always @(posedge audgen_mclk) begin
    aud_mclk_divider <= aud_mclk_divider + 1'b1;
end

// shift out audio data as I2S 
// 32 total bits per channel, but only 16 active bits at the start and then 16 dummy bits
//
    reg     [4:0]   audgen_lrck_cnt;    
    reg             audgen_lrck;
    reg             audgen_dac;
always @(negedge audgen_sclk) begin
    audgen_dac <= 1'b0;
    // 48khz * 64
    audgen_lrck_cnt <= audgen_lrck_cnt + 1'b1;
    if(audgen_lrck_cnt == 31) begin
        // switch channels
        audgen_lrck <= ~audgen_lrck;
        
    end 
end
```

Note: audio_lrck's target signal is `output wire audio_lrck` at the module's top port list, but `audio_i2s.v` declares its own `output reg audio_lrck` — Step 6 wires `audio_i2s`'s output directly to `core_top`'s `audio_lrck`/`audio_dac`/`audio_mclk` ports via a straight `assign`, shown next.

- [ ] **Step 6: Instantiate `football_system`, `ce_gen`, `rom_loader`, `audio_i2s`, and the input/CDC wiring**

Add this block where the deleted audio generator was (or anywhere after the `mf_pllbase` instantiation at the bottom of the file — it must come after `clk_core_12288`/`reset_n` are declared, which they already are earlier in the file):

```verilog
////////////////////////////////////////////////////////////////////////////////////////
// Mattel Football core: CPU + display + audio + ROM loading

    wire        game_ce;
ce_gen cg1 (
    .clk        ( clk_core_12288 ),
    .rst_n      ( reset_n ),
    .ce         ( game_ce )
);

    wire [9:0]  football_rom_addr;
    wire [7:0]  football_rom_data;
rom_loader #( .SLOT_BASE(32'h10000000) ) rl1 (
    .clk            ( clk_74a ),
    .bridge_wr      ( bridge_wr ),
    .bridge_addr    ( bridge_addr ),
    .bridge_wr_data ( bridge_wr_data ),
    .rom_addr       ( football_rom_addr ),
    .rom_data       ( football_rom_data )
);

    // kb[3:0] bit order per MAME's mfootb IN.0 port (hh_rw5000.cpp):
    // bit0=Down, bit1=Forward, bit2=Up, bit3=Kick. din[3:0] per IN.1:
    // bit0=Difficulty(1=PRO1), bit1=Score, bit2=Status, bit3=FactoryTest.
    // Pocket mapping per the design spec's Controls table: D-pad Up/Down/
    // Right -> Up/Down/Forward, A -> Kick, Start -> Score, Select -> Status.
    wire [3:0] football_kb  = { cont1_key[4], cont1_key[0], cont1_key[3], cont1_key[1] }; // {Kick,Up,Forward,Down}
    wire [3:0] football_din = { 1'b0, cont1_key[14], cont1_key[15], 1'b1 }; // {FactoryTest=0,Status,Score,Difficulty=PRO1}

    wire [23:0] football_rgb;
    wire        football_spk;
football_system fb1 (
    .clk        ( clk_core_12288 ),
    .rst_n      ( reset_n ),
    .ce         ( game_ce ),
    .rom_addr   ( football_rom_addr ),
    .rom_data   ( football_rom_data ),
    .kb         ( football_kb ),
    .din        ( football_din ),
    .score_btn  ( cont1_key[15] ),  // Start = Score (same physical button also feeds din[1] above)
    .px_x       ( visible_x[8:0] ),
    .px_y       ( visible_y[8:0] ),
    .px_rgb     ( football_rgb ),
    .spk        ( football_spk ),
    .window_tick(  )
);

    wire audio_mclk_wire;
audio_i2s ai1 (
    .clk_74a    ( clk_74a ),
    .spk        ( spk_sync ),
    .audio_mclk ( audio_mclk_wire ),
    .audio_sclk (  ),
    .audio_lrck ( audio_lrck ),
    .audio_dac  ( audio_dac )
);
assign audio_mclk = audio_mclk_wire;

    wire spk_sync;
synch_2 spk_sync_inst ( football_spk, spk_sync, audio_mclk_wire, , );
```

Note: `football_kb`/`football_din`'s bit order above is grounded directly in MAME's `hh_rw5000.cpp` `PORT_START("IN.0")`/`PORT_START("IN.1")` definitions for `mfootb` — not a guess. Still, spot-check it against `sim/b6100_cpu_tb.cpp`'s existing `test_tkb_and_read`/`test_tdin_mapping` tests before the hardware bring-up session (cheap, zero-risk verification of a detail that's otherwise only debuggable by trial and error on real hardware), and correct the concatenation order here if they disagree.

- [ ] **Step 7: Register all project source files with Quartus, then build**

`src/fpga/ap_core.qsf` currently lists only two Verilog files explicitly (`grep VERILOG_FILE src/fpga/ap_core.qsf` shows exactly `core/core_top.v` and `core/core_bridge_cmd.v` — confirmed by reading the file directly, not assumed). None of Plans 2–3's modules, nor this plan's three new ones, are registered — Quartus will not find them without explicit entries. Add these lines to `src/fpga/ap_core.qsf` immediately after the existing `VERILOG_FILE core/core_bridge_cmd.v` line (paths are relative to `src/fpga/`, so project sources under `src/` are one level up):

```
set_global_assignment -name VERILOG_FILE ../b6100_cpu.v
set_global_assignment -name VERILOG_FILE ../led_capture.v
set_global_assignment -name VERILOG_FILE ../video_renderer.v
set_global_assignment -name VERILOG_FILE ../football_system.v
set_global_assignment -name VERILOG_FILE ../ce_gen.v
set_global_assignment -name VERILOG_FILE ../rom_loader.v
set_global_assignment -name VERILOG_FILE ../audio_i2s.v
```

Run: `make bitstream`
Expected: Quartus completes with `Quartus Prime Assembler was successful`. If it still errors, the most likely remaining causes are (a) a typo'd relative path above, or (b) the `kb`/`din` bit-order verification from Step 6 was skipped and a width/type mismatch surfaced instead — Quartus errors are usually specific enough to point at the exact line; read them before guessing.

- [ ] **Step 8: Package and stage**

Run: `make package` (Plan 1's target — stages the new `.rbf_r` into `dist/Cores/Developer.Core Template/bitstream.rbf_r`).

- [ ] **Step 9: ROM placement checkpoint — CHECK WITH THE HUMAN**

Ask the human partner to copy `sim/roms/mfootb.bin` to the Pocket SD card at `Assets/ex_platform/common/mfootb.bin` (creating the `Assets/ex_platform/common/` directories if they don't exist), and to copy the updated `dist/` contents (`Cores/`, `Platforms/`) onto the card as in Plan 1. **Stop here — do not proceed without confirmation the file is in place**, since a missing/misnamed ROM file will produce a boot error indistinguishable from an actual RTL bug.

- [ ] **Step 10: On-Pocket boot checkpoint — CHECK WITH THE HUMAN**

Ask the human partner to boot the core on their Pocket and report back on: (a) does it load without a "General Error" or "Error in core setup" (per Plan 1's known folder-naming gotcha — the core folder is still `Developer.Core Template`, only its *contents* changed); (b) does the screen show the game (field/digits), not a blank or garbage image; (c) is there audible sound when the CPU beeps; (d) do the D-pad and A/Start/Select buttons control the game as expected. **Do not proceed to commit until the human confirms all four.** If anything fails, this is a hardware-only debugging session — apply `superpowers:systematic-debugging` rather than guessing; the likely suspects in rough order are: (1) the `.qsf` file-list omission from Step 7, (2) the `kb`/`din` bit mapping, (3) the video timing constants, (4) the ROM data-slot `address`/`filename` mismatch.

- [ ] **Step 11: Document and commit**

Append a "Hardware bring-up (Plan 4)" section to `docs/verification.md` recording: the final working `kb`/`din` bit mapping (corrected per Step 6's note if needed), any `.qsf` file-list additions from Step 7, and the human's confirmation from Step 10. Then:

```bash
git add src/fpga/core/core_top.v "dist/Cores/Developer.Core Template/data.json" "dist/Cores/Developer.Core Template/video.json" docs/verification.md src/fpga/ap_core.qsf
git commit -m "feat: wire CPU+display+audio+ROM loading into core_top; boots and plays on Pocket hardware

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Done criteria (Milestone 5)

- `make sim` passes with three new unit testbenches (`ce_gen`, `rom_loader`, `audio_i2s`), pristine `-Wall`.
- `make bitstream` / `make package` produce a bitstream with the real CPU, display, audio, and ROM loading wired in.
- The human partner has booted the core on a physical Pocket with the real ROM on the SD card and confirmed: it loads without error, the display shows the game, sound is audible, and D-pad/A/Start/Select control it.

## Out of scope (later plans)

- `input.json` button labels, the "original button layout" settings toggle, difficulty/factory-test settings menu entries, bezel artwork, presentation toggle, platform/branding rename away from `ex_platform`/`Developer.Core Template`, save-state support (explicitly out of v1 per the design spec), release packaging — all Plan 5.
