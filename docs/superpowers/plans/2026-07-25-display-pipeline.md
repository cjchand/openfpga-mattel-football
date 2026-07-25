# Display Pipeline (Simulation) Implementation Plan (Plan 3 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the CPU's raw strobe/segment outputs into rendered video frames — verified entirely in simulation with automated pixel assertions plus human-eyeballed frame dumps of real gameplay (spec milestone 4).

**Architecture:** Two new Verilog modules joined to the CPU by a small system wrapper. `led_capture` integrates each LED's on-time over a 60 Hz window of `ce` ticks and outputs a 2-bit brightness level per LED (off/dim/bright — mirroring MAME's 0.02/0.2 `bri_levels`, so the bright-player/dim-defender distinction emerges from measured duty cycle). `video_renderer` is pure combinational geometry: (x,y) in, RGB out, drawing the field-only presentation (7 seven-seg digits + 9×3 dash field + digit-3 decimal point) on a 400×360 canvas. `football_system` wires CPU + capture + renderer for whole-system simulation; a C++ harness dumps PPM frames and a speaker WAV from real ROM execution.

**Tech Stack:** Verilog-2001, Verilator + C++17, PPM image output (no libraries), WAV output (hand-rolled 44-byte header).

**Spec:** `docs/superpowers/specs/2026-07-25-mattel-football-core-design.md` (milestone 4). Two deliberate narrowings vs the spec's module sketch, both noted for the record: (1) `led_matrix` and `led_intensity` collapse into one module `led_capture` — the "currently lit" grid is a single combinational remap not worth a module boundary; (2) `audio.v` (48 kHz I²S) is deferred to Plan 4 where real clocks exist — this plan captures the raw `spk` bitstream to WAV for ear verification instead.

## Global Constraints

- Display topology (from MAME `hh_rw5000.cpp` mfootb driver + `src/mame/layout/mfootb.lay`): 9 strobes × 11 lines. Line mapping from CPU outputs: lines 6:0 = `seg[6:0]` (7-seg a–g, bit order a=0…g=6); line 7 = decimal point, driven by the SCORE BUTTON (external input, not the CPU); lines 8/9/10 = `seg[7]`/`seg[8]`/`seg[9]` = BOTTOM/MIDDLE/TOP dash rows (the driver's `(m_seg << 1 & 0x700) | dp | (m_seg & 0x7f)` remap; `.lay` elements `N.10` are the top row).
- Physical arrangement (from `mfootb.lay`): 7 seven-seg digits on strobes 0–6 (grouped 2-3-2: DOWN = digits 0-1, FIELD POSITION = 2-4, YARDS TO GO = 5-6); only digit 3 has a decimal point; dashes exist on all 9 strobes × 3 rows.
- Brightness thresholds mirror MAME `set_bri_levels(0.02, 0.2)`: duty > 2% → dim (level 1), duty > 20% → bright (level 2), else off (level 0).
- Window length: `WINDOW = 1167` ce ticks (280 kHz / 4 / 60 Hz ≈ one video frame of instructions).
- ATBZ clears `seg` in the same instruction it raises `str` (final-review note from Plan 2) — capture must ACCUMULATE per-ce coincidence of str+seg over the window, never sample at strobe edges.
- Levels bus: `[197:0]`, 99 LEDs × 2 bits, index `led = col*11 + line`, slice `levels[led*2 +: 2]`.
- Canvas 400×360, RGB888. Colors: off-LED ghost `24'h1A0505`, dim `24'h801414`, bright `24'hFF2020`, background `24'h000000`.
- `make sim` contract: `SIM_TESTS` gains `led_capture` and `video_renderer` (pattern: `sim/<name>_tb.cpp` + `src/<name>.v`); Verilator `-Wall`, pristine output.
- The game ROM stays at `sim/roms/mfootb.bin` (user-supplied, gitignored); frame/wav outputs are build products, gitignored.
- Every commit message ends with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`; Makefile recipes use tabs.

## File Structure

- `src/led_capture.v` — duty-cycle accumulator → per-LED levels (Task 1)
- `src/video_renderer.v` — combinational (x,y)→RGB geometry (Task 2)
- `src/football_system.v` — CPU + capture + renderer wrapper (Task 3)
- `sim/led_capture_tb.cpp`, `sim/video_renderer_tb.cpp` — unit testbenches (Tasks 1–2)
- `sim/ppm.h` — 10-line PPM writer shared by renderer TB and system harness (Task 2)
- `sim/football_system_tb.cpp` — whole-system frame/WAV dump harness (Task 3)
- `Makefile` — SIM_TESTS update (Tasks 1–2), `frames` target (Task 3)

---

### Task 1: `led_capture` — duty-cycle integration to brightness levels

**Files:**
- Create: `src/led_capture.v`
- Create: `sim/led_capture_tb.cpp`
- Modify: `Makefile` (`SIM_TESTS := b6100_cpu led_capture`)

**Interfaces:**
- Consumes: the CPU's output signal shapes (`str[8:0]`, `seg[9:0]`, 1 instruction per `ce`).
- Produces: `module led_capture #(parameter WINDOW = 1167) (input clk, input rst_n, input ce, input [8:0] str, input [9:0] seg, input dp_in, output reg [197:0] levels, output reg window_tick)`. `levels` updates once per WINDOW ce ticks (holding steady between updates); `window_tick` pulses for one clk when it does — Task 3's harness uses it to pace frame dumps. Line mapping inside: `line_active = {seg[9:7], dp_in, seg[6:0]}` (bit 10 = top dash row).

- [ ] **Step 1: Write the failing testbench**

Create `sim/led_capture_tb.cpp`:

```cpp
// Unit tests for led_capture: duty-cycle windows -> 2-bit brightness levels.
#include "Vled_capture.h"
#include "verilated.h"
#include <cstdio>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static const int WINDOW = 1167;

struct Cap {
    Vled_capture d;
    void reset() {
        d.ce = 1; d.str = 0; d.seg = 0; d.dp_in = 0;
        d.rst_n = 0; d.clk = 0; d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        d.rst_n = 1; d.eval();
    }
    void tick() { d.clk = 1; d.eval(); d.clk = 0; d.eval(); }
    // drive one full window: LED pattern (str,seg,dp) active for `on` ticks, idle rest
    void window(int on, uint16_t str, uint16_t seg, int dp = 0) {
        for (int i = 0; i < WINDOW; i++) {
            bool active = i < on;
            d.str = active ? str : 0;
            d.seg = active ? seg : 0;
            d.dp_in = active ? dp : 0;
            tick();
        }
    }
    int level(int col, int line) {
        int led = col * 11 + line;
        // levels is 198 bits -> Verilator exposes as VlWide<7>
        return (d.levels[(led * 2) / 32] >> ((led * 2) % 32)) & 3;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_thresholds() {
    Cap c; c.reset();
    // col 3, seg line 0 (segment a): 300/1167 = 25.7% duty -> bright (>20%)
    c.window(300, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 2, "25.7% duty -> bright");
    // 100/1167 = 8.6% -> dim (>2%, <=20%)
    c.window(100, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 1, "8.6% duty -> dim");
    // 20/1167 = 1.7% -> off (<=2%)
    c.window(20, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 0, "1.7% duty -> off");
    // exact boundary: 24 ticks (>2% of 1167=23.34) -> dim; 234 (>20%=233.4) -> bright
    c.window(24, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 1, "24 ticks -> dim boundary");
    c.window(234, 1 << 3, 1 << 0);
    CHECK(c.level(3, 0) == 2, "234 ticks -> bright boundary");
}

static void test_line_mapping() {
    Cap c; c.reset();
    // seg bit 7 -> line 8 (BOTTOM dash row); bit 9 -> line 10 (TOP row);
    // dp_in -> line 7; all on col 0, full duty
    c.window(WINDOW, 1 << 0, 1 << 7);
    CHECK(c.level(0, 8) == 2 && c.level(0, 10) == 0, "seg[7] is line 8 (bottom)");
    c.window(WINDOW, 1 << 0, 1 << 9);
    CHECK(c.level(0, 10) == 2 && c.level(0, 8) == 0, "seg[9] is line 10 (top)");
    c.window(WINDOW, 1 << 0, 0, /*dp=*/1);
    CHECK(c.level(0, 7) == 2, "dp_in is line 7");
}

static void test_coincidence_and_hold() {
    Cap c; c.reset();
    // str and seg both on but never simultaneously -> count 0 (ATBZ-style)
    for (int i = 0; i < WINDOW; i++) {
        bool odd = i & 1;
        c.d.str = odd ? (1 << 2) : 0;
        c.d.seg = odd ? 0 : (1 << 4);
        c.tick();
    }
    CHECK(c.level(2, 4) == 0, "no str&seg coincidence -> off");
    // levels HOLD between window boundaries: light fully, then observe mid-window
    c.window(WINDOW, 1 << 2, 1 << 4);
    CHECK(c.level(2, 4) == 2, "fully lit -> bright");
    for (int i = 0; i < WINDOW / 2; i++) { c.d.str = 0; c.d.seg = 0; c.tick(); }
    CHECK(c.level(2, 4) == 2, "levels hold steady mid-window");
}

static void test_ce_gating() {
    Cap c; c.reset();
    // with ce low, nothing accumulates and the window doesn't advance
    c.d.ce = 0; c.d.str = 1 << 1; c.d.seg = 1 << 1;
    for (int i = 0; i < 3 * WINDOW; i++) c.tick();
    c.d.ce = 1;
    c.window(WINDOW, 0, 0);   // one real idle window to latch
    CHECK(c.level(1, 1) == 0, "ce=0 ticks never counted");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("thresholds", test_thresholds);
    run_test("line_mapping", test_line_mapping);
    run_test("coincidence_and_hold", test_coincidence_and_hold);
    run_test("ce_gating", test_ce_gating);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: led_capture_tb\n");
    return 0;
}
```

- [ ] **Step 2: Update Makefile and run to verify failure**

Change `SIM_TESTS := b6100_cpu` to `SIM_TESTS := b6100_cpu led_capture`.
Run: `make sim` — Expected: FAIL, `src/led_capture.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/led_capture.v`:

```verilog
// Integrates each LED's on-time over a ~60 Hz window of ce ticks and emits a
// 2-bit brightness level per LED, mirroring MAME's pwm_display with
// set_bri_levels(0.02, 0.2): duty >2% = dim, >20% = bright. Levels update
// once per window and hold in between, so downstream video is flicker-free
// regardless of the game's multiplex pattern (the CPU's ATBZ clears seg the
// same instruction it raises str, so we accumulate str&seg coincidence per
// ce tick — never edge-sample).
module led_capture #(
    parameter WINDOW = 1167   // ce ticks per window: 280kHz/4 instr rate / 60Hz
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         ce,
    input  wire [8:0]   str,
    input  wire [9:0]   seg,
    input  wire         dp_in,      // score button: decimal point line
    output reg  [197:0] levels,     // 99 LEDs x 2 bits, led = col*11 + line
    output reg          window_tick
);
    // thresholds: strictly-greater-than 2% / 20% of WINDOW
    localparam integer DIM_MIN    = (WINDOW * 2) / 100 + 1;   // 24 for 1167
    localparam integer BRIGHT_MIN = WINDOW / 5 + 1;           // 234 for 1167
    localparam [10:0]  WIN_LAST   = WINDOW - 1;

    // line order per MAME mfootb driver remap:
    // {seg[9:7] = top/mid/bottom dash rows (lines 10/9/8), dp (line 7),
    //  seg[6:0] = 7seg a-g (lines 6..0)}
    wire [10:0] line_active = {seg[9:7], dp_in, seg[6:0]};

    reg [10:0] cnt [0:98];
    reg [10:0] window_pos;
    integer c, l;

    always @(posedge clk) begin
        if (!rst_n) begin
            for (c = 0; c < 99; c = c + 1) cnt[c] <= 11'd0;
            window_pos <= 11'd0;
            levels <= 198'd0;
            window_tick <= 1'b0;
        end else begin
            window_tick <= 1'b0;
            if (ce) begin
                for (c = 0; c < 9; c = c + 1)
                    for (l = 0; l < 11; l = l + 1)
                        if (str[c] && line_active[l])
                            cnt[c*11 + l] <= cnt[c*11 + l] + 11'd1;

                if (window_pos == WIN_LAST) begin
                    window_pos <= 11'd0;
                    window_tick <= 1'b1;
                    for (c = 0; c < 99; c = c + 1) begin
                        /* verilator lint_off WIDTHEXPAND */
                        if (cnt[c] >= BRIGHT_MIN)
                            levels[c*2 +: 2] <= 2'd2;
                        else if (cnt[c] >= DIM_MIN)
                            levels[c*2 +: 2] <= 2'd1;
                        else
                            levels[c*2 +: 2] <= 2'd0;
                        /* verilator lint_on WIDTHEXPAND */
                        cnt[c] <= 11'd0;
                    end
                end else
                    window_pos <= window_pos + 11'd1;
            end
        end
    end
endmodule
```

Note the interaction the tests pin down: when the window closes, the final-tick increment and the counter clear collide — the nonblocking clear wins for `cnt`, but the level comparison reads the PRE-increment count. That loses at most 1 tick per window per LED; the boundary tests (24/234) are written against this exact behavior (they light the LED in the FIRST `on` ticks of the window, so the final tick is idle and no collision occurs). If a boundary test fails by one count, re-read this paragraph before touching thresholds.

- [ ] **Step 4: Run to verify pass**

Run: `make sim` — Expected: `PASS: led_capture_tb` alongside existing passes, `-Wall` clean, pristine.

- [ ] **Step 5: Commit**

```bash
git add src/led_capture.v sim/led_capture_tb.cpp Makefile
git commit -m "feat: led_capture — duty-cycle windows to 3-level LED brightness

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: `video_renderer` — combinational field-only geometry

**Files:**
- Create: `src/video_renderer.v`
- Create: `sim/ppm.h`
- Create: `sim/video_renderer_tb.cpp`
- Modify: `Makefile` (`SIM_TESTS := b6100_cpu led_capture video_renderer`)

**Interfaces:**
- Consumes: the `levels[197:0]` bus format from Task 1 (led = col*11 + line).
- Produces: `module video_renderer (input [8:0] x, input [8:0] y, input [197:0] levels, output reg [23:0] rgb)` — pure combinational; valid for x in [0,400), y in [0,360). Task 3 instantiates it inside `football_system`.
- Produces: `sim/ppm.h` with `void write_ppm(const char* path, int w, int h, const uint8_t* rgb)` (rgb = w*h*3 bytes, row-major).

Geometry (field-only style; integer constants, all in pixels on the 400×360 canvas):
- **Digits:** 7 cells, 24 wide × 32 tall, top edge y=40. Cell x origins: `{40, 72, 136, 168, 200, 264, 296}` for digits 0..6 (strobes 0..6) — grouped 2-3-2 like the real panel. Segment rectangles within a cell (x0,y0,w,h): a=(4,0,16,4), b=(20,2,4,13), c=(20,17,4,13), d=(4,28,16,4), e=(0,17,4,13), f=(0,2,4,13), g=(4,14,16,4). Line index = segment: 0=a,1=b,2=c,3=d,4=e,5=f,6=g.
- **Decimal point:** digit 3 only: 6×6 square at absolute (193, 66) (line 7 of col 3).
- **Dash field:** 9 columns × 3 rows. Dash 20 wide × 6 tall. Column x origin = `30 + 38*col` (cols 0..8). Row y origins: top (line 10) = 160, middle (line 9) = 220, bottom (line 8) = 280.
- **Colors** (Global Constraints): level 0 → ghost `24'h1A0505` inside any LED rectangle, dim `24'h801414`, bright `24'hFF2020`; everywhere else background `24'h000000`.

- [ ] **Step 1: Write the PPM helper and failing testbench**

Create `sim/ppm.h`:

```cpp
#pragma once
#include <cstdio>
#include <cstdint>

// Minimal binary PPM (P6) writer. rgb is w*h*3 bytes, row-major, RGB order.
inline void write_ppm(const char* path, int w, int h, const uint8_t* rgb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, (size_t)w * h * 3, f);
    std::fclose(f);
}
```

Create `sim/video_renderer_tb.cpp`:

```cpp
// Pixel-level tests for the field-only renderer, plus a reference frame dump.
#include "Vvideo_renderer.h"
#include "verilated.h"
#include "ppm.h"
#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct Rend {
    Vvideo_renderer d;
    void set_level(int col, int line, int lvl) {
        int led = col * 11 + line;
        int word = (led * 2) / 32, bit = (led * 2) % 32;
        d.levels[word] = (d.levels[word] & ~(3u << bit)) | ((uint32_t)lvl << bit);
    }
    void clear_levels() { for (int i = 0; i < 7; i++) d.levels[i] = 0; }
    uint32_t px(int x, int y) { d.x = x; d.y = y; d.eval(); return d.rgb; }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static const uint32_t GHOST = 0x1A0505, DIM = 0x801414, BRIGHT = 0xFF2020, BG = 0x000000;

static void test_dash_pixels() {
    Rend r; r.clear_levels();
    // col 4 top-row dash (line 10) bright: x origin 30+38*4=182, y 160
    r.set_level(4, 10, 2);
    CHECK(r.px(182 + 10, 160 + 3) == BRIGHT, "center of bright dash");
    CHECK(r.px(182 - 1, 160 + 3) == BG, "left of dash is background");
    CHECK(r.px(182 + 10, 160 + 6) == BG, "below dash is background");
    // same dash dim, then off (ghost)
    r.set_level(4, 10, 1);
    CHECK(r.px(182 + 10, 160 + 3) == DIM, "dim dash");
    r.set_level(4, 10, 0);
    CHECK(r.px(182 + 10, 160 + 3) == GHOST, "off dash shows ghost");
    // middle and bottom rows of col 0
    r.set_level(0, 9, 2);  r.set_level(0, 8, 1);
    CHECK(r.px(30 + 10, 220 + 3) == BRIGHT, "middle row (line 9) y=220");
    CHECK(r.px(30 + 10, 280 + 3) == DIM, "bottom row (line 8) y=280");
}

static void test_digit_segments() {
    Rend r; r.clear_levels();
    // digit 2 (x0=136, y0=40): light segment a (line 0) and g (line 6)
    r.set_level(2, 0, 2);
    r.set_level(2, 6, 1);
    CHECK(r.px(136 + 12, 40 + 2) == BRIGHT, "segment a center");
    CHECK(r.px(136 + 12, 40 + 16) == DIM, "segment g center");
    CHECK(r.px(136 + 22, 40 + 8) == GHOST, "unlit segment b shows ghost");
    CHECK(r.px(136 + 12, 40 + 8) == BG, "digit interior is background");
}

static void test_decimal_point() {
    Rend r; r.clear_levels();
    r.set_level(3, 7, 2);
    CHECK(r.px(193 + 3, 66 + 3) == BRIGHT, "dp of digit 3");
    r.set_level(3, 7, 0);
    CHECK(r.px(193 + 3, 66 + 3) == GHOST, "dp ghost when off");
    // dp exists ONLY on digit 3: col 2 line 7 must render nothing anywhere near
    r.clear_levels(); r.set_level(2, 7, 2);
    CHECK(r.px(193 + 3, 66 + 3) == GHOST || r.px(193 + 3, 66 + 3) == BG,
          "col2 line7 does not light digit3's dp");
}

static void test_reference_frame() {
    // dump a frame with a recognizable pattern for human eyeballing
    Rend r; r.clear_levels();
    r.set_level(4, 9, 2);                       // "player" mid-field bright
    for (int c = 0; c < 9; c += 2) r.set_level(c, 10, 1);  // dim defenders
    for (int s = 0; s < 7; s++) r.set_level(0, s, 2);      // digit0 shows '8'
    std::vector<uint8_t> buf(400 * 360 * 3);
    for (int y = 0; y < 360; y++)
        for (int x = 0; x < 400; x++) {
            uint32_t p = r.px(x, y);
            size_t i = ((size_t)y * 400 + x) * 3;
            buf[i] = p >> 16; buf[i + 1] = p >> 8; buf[i + 2] = p;
        }
    write_ppm("sim/renderer_reference.ppm", 400, 360, buf.data());
    std::printf("wrote sim/renderer_reference.ppm\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("dash_pixels", test_dash_pixels);
    run_test("digit_segments", test_digit_segments);
    run_test("decimal_point", test_decimal_point);
    run_test("reference_frame", test_reference_frame);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: video_renderer_tb\n");
    return 0;
}
```

- [ ] **Step 2: Update Makefile and run to verify failure**

`SIM_TESTS := b6100_cpu led_capture video_renderer`. The TB includes `ppm.h` from `sim/` — Verilator compiles the TB from that directory context; if the include isn't found, add `-CFLAGS -I../` is NOT needed (the tb and header share `sim/`; `#include "ppm.h"` resolves relative to the including file).
Run: `make sim` — Expected: FAIL, `src/video_renderer.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/video_renderer.v`:

```verilog
// Field-only renderer: pure combinational (x,y) -> RGB over a 400x360 canvas.
// Geometry mirrors the arrangement in MAME's mfootb.lay: 7 seven-seg digits
// (strobes 0-6, grouped 2-3-2), digit-3 decimal point, and a 9x3 dash field
// (lines 10/9/8 = top/middle/bottom rows).
module video_renderer (
    input  wire [8:0]   x,
    input  wire [8:0]   y,
    input  wire [197:0] levels,
    output reg  [23:0]  rgb
);
    localparam [23:0] C_BG     = 24'h000000;
    localparam [23:0] C_GHOST  = 24'h1A0505;
    localparam [23:0] C_DIM    = 24'h801414;
    localparam [23:0] C_BRIGHT = 24'hFF2020;

    // digit cell x origins (strobes 0-6), cell 24x32 at y=40
    function [8:0] digit_x(input [2:0] d);
        case (d)
            3'd0: digit_x = 9'd40;   3'd1: digit_x = 9'd72;
            3'd2: digit_x = 9'd136;  3'd3: digit_x = 9'd168;
            3'd4: digit_x = 9'd200;  3'd5: digit_x = 9'd264;
            3'd6: digit_x = 9'd296;  default: digit_x = 9'd0;
        endcase
    endfunction

    // segment rects within a digit cell: {x0, y0, w, h}
    function [35:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {9'd4,  9'd0,  9'd16, 9'd4};   // a
            3'd1: seg_rect = {9'd20, 9'd2,  9'd4,  9'd13};  // b
            3'd2: seg_rect = {9'd20, 9'd17, 9'd4,  9'd13};  // c
            3'd3: seg_rect = {9'd4,  9'd28, 9'd16, 9'd4};   // d
            3'd4: seg_rect = {9'd0,  9'd17, 9'd4,  9'd13};  // e
            3'd5: seg_rect = {9'd0,  9'd2,  9'd4,  9'd13};  // f
            3'd6: seg_rect = {9'd4,  9'd14, 9'd16, 9'd4};   // g
            default: seg_rect = 36'd0;
        endcase
    endfunction

    function [23:0] level_color(input [1:0] lvl);
        case (lvl)
            2'd0: level_color = C_GHOST;
            2'd1: level_color = C_DIM;
            default: level_color = C_BRIGHT;
        endcase
    endfunction

    integer d, s, col, row;
    reg [8:0] rx0, ry0, rw, rh;
    reg [1:0] lvl;
    reg [8:0] dashy;

    always @* begin
        rgb = C_BG;

        // digits: 7 cells x 7 segments
        for (d = 0; d < 7; d = d + 1)
            for (s = 0; s < 7; s = s + 1) begin
                {rx0, ry0, rw, rh} = seg_rect(s[2:0]);
                rx0 = rx0 + digit_x(d[2:0]);
                ry0 = ry0 + 9'd40;
                if (x >= rx0 && x < rx0 + rw && y >= ry0 && y < ry0 + rh) begin
                    lvl = levels[(d*11 + s)*2 +: 2];
                    rgb = level_color(lvl);
                end
            end

        // decimal point: digit 3 only, line 7, 6x6 at (193,66)
        if (x >= 9'd193 && x < 9'd199 && y >= 9'd66 && y < 9'd72) begin
            lvl = levels[(3*11 + 7)*2 +: 2];
            rgb = level_color(lvl);
        end

        // dash field: 9 cols x 3 rows, dash 20x6
        for (col = 0; col < 9; col = col + 1)
            for (row = 0; row < 3; row = row + 1) begin
                // row 0 = line 10 (top, y=160), 1 = line 9 (y=220), 2 = line 8 (y=280)
                dashy = (row == 0) ? 9'd160 : (row == 1) ? 9'd220 : 9'd280;
                rx0 = 9'd30 + 9'd38 * col[8:0];
                if (x >= rx0 && x < rx0 + 9'd20 && y >= dashy && y < dashy + 9'd6) begin
                    lvl = levels[(col*11 + (10 - row))*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
    end
endmodule
```

- [ ] **Step 4: Run to verify pass; eyeball the reference frame**

Run: `make sim` — Expected: all testbenches pass, pristine. Open `sim/renderer_reference.ppm` (Preview opens PPM on macOS) — expect a bright mid-field dash, alternating dim dashes on the top row, and an '8' on the leftmost digit. Confirm `sim/renderer_reference.ppm` is covered by an existing gitignore rule or add `sim/*.ppm`.

- [ ] **Step 5: Commit**

```bash
git add src/video_renderer.v sim/ppm.h sim/video_renderer_tb.cpp Makefile .gitignore
git commit -m "feat: video_renderer — field-only combinational geometry with pixel tests

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: `football_system` — whole-system frames from the real ROM

**Files:**
- Create: `src/football_system.v`
- Create: `sim/football_system_tb.cpp`
- Modify: `Makefile` (`frames` target), `.gitignore` (`sim/frames/`, `sim/*.wav`)

**Interfaces:**
- Consumes: `b6100_cpu` (ports per Plan 2), `led_capture` and `video_renderer` (Tasks 1–2), ROM at `sim/roms/mfootb.bin`, ROM-hole load convention (0x300 bytes at 0, 0x80 at 0x380).
- Produces: `module football_system` wiring all three, with the CPU's ROM ports and inputs exposed, plus the renderer's x/y/rgb and `window_tick`; harness `sim/obj_dir_frames/football_frames <rom.bin> <n_windows> <kb_hex> <din_hex> <settle> <outdir>` writing `frame_NNN.ppm` per window plus `spk.wav` (8-bit unsigned, 70000 Hz sample rate — the raw instruction-rate speaker line).

- [ ] **Step 1: Write the system wrapper**

Create `src/football_system.v`:

```verilog
// Whole-system wrapper for simulation (and later, the core of the APF top):
// CPU + LED capture + renderer. ROM stays external (harness serves it now,
// APF-loaded BRAM in Plan 4).
module football_system (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        ce,
    output wire [9:0]  rom_addr,
    input  wire [7:0]  rom_data,
    input  wire [3:0]  kb,
    input  wire [3:0]  din,
    input  wire        score_btn,   // drives the display's decimal point line
    input  wire [8:0]  px_x,
    input  wire [8:0]  px_y,
    output wire [23:0] px_rgb,
    output wire        spk,
    output wire        window_tick
);
    wire [8:0]   str;
    wire [9:0]   seg;
    wire [197:0] levels;

    b6100_cpu cpu (
        .clk(clk), .rst_n(rst_n), .ce(ce),
        .rom_addr(rom_addr), .rom_data(rom_data),
        .kb(kb), .din(din),
        .str(str), .seg(seg), .spk(spk),
        .dbg_pc(), .dbg_a(), .dbg_bl(), .dbg_bu(), .dbg_b(),
        .dbg_c(), .dbg_s(), .dbg_skip(), .dbg_illegal()
    );

    led_capture cap (
        .clk(clk), .rst_n(rst_n), .ce(ce),
        .str(str), .seg(seg), .dp_in(score_btn),
        .levels(levels), .window_tick(window_tick)
    );

    video_renderer rend (
        .x(px_x), .y(px_y), .levels(levels), .rgb(px_rgb)
    );
endmodule
```

Lint note: the intentionally-unconnected `.dbg_*()` ports may trip `-Wall` (PINCONNECTEMPTY). If so, connect them to a `wire [9:0] dbg_nc_pc;` etc. bundle wrapped in `/* verilator lint_off UNUSEDSIGNAL */ ... lint_on`, or add `/* verilator lint_off PINCONNECTEMPTY */` around just the instantiation — never disable warnings globally.

- [ ] **Step 2: Write the frame-dump harness**

Create `sim/football_system_tb.cpp`:

```cpp
// Runs the real ROM through the whole system and dumps one PPM per display
// window plus the raw speaker bitstream as a WAV. Automated assertions cover
// the brightness-class invariants; the frames themselves are for human eyes.
#include "Vfootball_system.h"
#include "verilated.h"
#include "ppm.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

static void write_wav(const char* path, const std::vector<uint8_t>& samples, uint32_t rate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    uint32_t n = samples.size(), data = n, riff = 36 + data;
    uint16_t ch = 1, bits = 8, align = 1;
    uint32_t brate = rate;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmtlen = 16; uint16_t pcm = 1;
    std::fwrite(&fmtlen, 4, 1, f); std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f);
    std::fwrite(&brate, 4, 1, f); std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    std::fwrite(samples.data(), 1, n, f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    if (argc != 7) {
        std::fprintf(stderr,
            "usage: football_frames <rom.bin> <n_windows> <kb_hex> <din_hex> <settle> <outdir>\n");
        return 2;
    }
    uint8_t rom[1024] = {0};
    {
        FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
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
    const char* outdir = argv[6];
    mkdir(outdir, 0755);

    Vfootball_system d;
    d.ce = 1; d.kb = 0; d.din = din & 0x1; d.score_btn = 0;
    d.px_x = 0; d.px_y = 0;
    d.rst_n = 0; d.clk = 0; d.eval();
    d.clk = 1; d.eval(); d.clk = 0; d.eval();
    d.rst_n = 1;

    std::vector<uint8_t> spk_samples;
    long tick = 0;
    int frames = 0;
    bool saw_bright_dash = false, saw_dim_dash = false, saw_digit = false;

    while (frames < n_windows) {
        if (tick == settle) { d.kb = kb; d.din = din; }
        d.rom_data = rom[d.rom_addr & 0x3ff];
        d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        spk_samples.push_back(d.spk ? 220 : 35);
        tick++;

        if (d.window_tick) {
            std::vector<uint8_t> buf(400 * 360 * 3);
            for (int y = 0; y < 360; y++)
                for (int x = 0; x < 400; x++) {
                    d.px_x = x; d.px_y = y; d.eval();
                    uint32_t p = d.px_rgb;
                    size_t i = ((size_t)y * 400 + x) * 3;
                    buf[i] = p >> 16; buf[i+1] = p >> 8; buf[i+2] = p;
                }
            char path[512];
            std::snprintf(path, sizeof path, "%s/frame_%03d.ppm", outdir, frames);
            write_ppm(path, 400, 360, buf.data());
            // classify content by scanning dash and digit pixel centers
            for (int col = 0; col < 9; col++)
                for (int row = 0; row < 3; row++) {
                    int cy = (row == 0) ? 163 : (row == 1) ? 223 : 283;
                    d.px_x = 30 + 38 * col + 10; d.px_y = cy; d.eval();
                    if (d.px_rgb == 0xFF2020) saw_bright_dash = true;
                    if (d.px_rgb == 0x801414) saw_dim_dash = true;
                }
            static const int dx[7] = {40, 72, 136, 168, 200, 264, 296};
            for (int dg = 0; dg < 7; dg++) {
                d.px_x = dx[dg] + 12; d.px_y = 40 + 2; d.eval();  // segment a
                if (d.px_rgb != 0x1A0505 && d.px_rgb != 0x000000) saw_digit = true;
            }
            frames++;
        }
    }

    std::string wav = std::string(outdir) + "/spk.wav";
    write_wav(wav.c_str(), spk_samples, 70000);

    long spk_toggles = 0;
    for (size_t i = 1; i < spk_samples.size(); i++)
        if (spk_samples[i] != spk_samples[i-1]) spk_toggles++;
    std::printf("frames=%d spk_toggles=%ld bright_dash=%d dim_dash=%d digit=%d\n",
                frames, spk_toggles, saw_bright_dash, saw_dim_dash, saw_digit);

    // Display invariants over real gameplay: the game must produce both
    // brightness classes on the dash field (player bright, defenders dim)
    // and light digit segments at some point.
    if (!saw_bright_dash || !saw_dim_dash || !saw_digit) {
        std::printf("FAIL: expected bright+dim dashes and lit digits\n");
        return 1;
    }
    std::printf("PASS: football_frames\n");
    return 0;
}
```

- [ ] **Step 3: Add the `frames` target and gitignore entries**

Append to `.gitignore`: `sim/frames/`, `sim/*.wav`, `sim/*.ppm` (if not already added in Task 2). Append to `Makefile`:

```make
.PHONY: frames
frames:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_frames --top-module football_system \
		-o football_frames sim/football_system_tb.cpp \
		src/football_system.v src/b6100_cpu.v src/led_capture.v src/video_renderer.v
	sim/obj_dir_frames/football_frames $(ROM) 180 2 1 1000 sim/frames
```

(180 windows = 3 seconds of game time with Forward held from tick 1000 — long enough for the pre-game display, game start, and player movement.)

- [ ] **Step 4: Run and verify**

Run: `make frames`
Expected: `PASS: football_frames` with `bright_dash=1 dim_dash=1 digit=1` and a nonzero `spk_toggles` printed (informational — the WAV is for ears, not assertions). 180 PPMs land in `sim/frames/`. If the brightness-class assertion fails, debug with MAME as the visual reference: run `mame mfootb` interactively and compare what its display shows during the same boot+Forward sequence before touching thresholds or capture logic.

- [ ] **Step 5: CHECKPOINT — human eyeballs the frames**

Stop and ask the human partner to look at a few of `sim/frames/frame_*.ppm` (e.g. 000, 060, 120, 179) and play `sim/frames/spk.wav`, comparing against MAME (`mame mfootb`) for: score panel showing plausible 7-seg content, one bright dash among dim ones, sensible player movement over time, and beep-like audio. Do not proceed to commit until the human confirms the frames look right.

- [ ] **Step 6: Commit**

```bash
git add src/football_system.v sim/football_system_tb.cpp Makefile .gitignore
git commit -m "feat: football_system — whole-system sim dumps real-gameplay frames + speaker WAV

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Done criteria (Milestone 4)

- `make sim` passes with the two new unit testbenches (thresholds, line mapping, coincidence/hold, ce gating; pixel-exact dash/digit/dp geometry), pristine `-Wall`.
- `make frames` renders 180 windows of real gameplay: automated brightness-class invariants pass, and the human partner has eyeballed frames + WAV against MAME.

## Out of scope (later plans)

- APF integration, BRAM ROM, real 280 kHz `ce` generation, 48 kHz I²S audio (`audio.v`) — Plan 4 (which also opens with the deferred TRA/RET corner + ce-gap CPU tests).
- Bezel artwork, settings menu, presentation toggle, packaging — Plan 5.
