# B6100 CPU verification: the MAME golden-trace gate

This is the final, strongest check on `src/b6100_cpu.v`: it proves our CPU is
*instruction-for-instruction identical* to MAME's `rw5000`-family emulation
of the real B6100 chip, over millions of executed instructions, across four
input scenarios.

## Trace format and observation point

Both sides emit one line per **non-skipped** instruction, immediately
*before* it executes:

```
T,%03X,%X,%X,%02X,%03X\n   =>  T,<pc>,<a>,<c>,<b>,<s>
```

- `pc` — 10-bit program counter (LFSR-encoded address, per rw5000base)
- `a` — 4-bit accumulator
- `c` — 1-bit carry
- `b` — 6-bit B register (page/offset), printed as 2 hex digits
- `s` — 10-bit subroutine-return stack top

This is exactly MAME's debugger instruction-hook observation point
(`rw5000base` calls the hook right before `execute_one()`), and exactly our
Verilator trace generator's observation point (state sampled right before
each clock edge that retires a non-skipped instruction). Only lines
starting with `T,` are compared; both traces also contain other text
(MAME's disassembly, our tool's stderr) which the diff ignores.

## The two producers

- **MAME**: `mame mfootb -debug -debugscript tools/golden/trace.debugscript
  ...` — the debugscript installs a `trace` action that calls
  `tracelog` with the format string above, then `go`.
- **Ours**: `sim/obj_dir_trace/b6100_trace <rom.bin> <n_instr> <kb_hex>
  <din_hex> <out.csv> [settle_cycles]` (built by `make tracegen` from
  `sim/b6100_cpu_trace.cpp` + `src/b6100_cpu.v`).

`tools/golden/trace_diff.py mame.tr ours.csv` filters both inputs to `T,`
lines and does a prefix compare: since `ours.csv` is always the shorter
side (`GOLDEN_N` instructions vs. MAME's `-seconds_to_run 45` at
~280 kHz ≈ 3M+ retired instructions), a clean PASS means every one of our
instructions matched MAME's exactly, in order, from boot.

## The working MAME invocation

```bash
GOLDEN_PORT=:IN.0 GOLDEN_FIELD=Forward \
  mame mfootb -debug -debugscript tools/golden/trace.debugscript \
  -autoboot_script tools/golden/hold_input.lua \
  -video none -sound none -nothrottle -seconds_to_run 45
```

Run from the repo root — MAME writes `golden.tr` to its CWD. Adaptations
made to the brief's starting-point flags, against installed MAME 0.288:

- **`trace golden.tr,maincpu,noloop,{...}`** — MAME's debugger `trace`
  command has a *loop-collapsing* feature on by default: when it detects a
  repeated instruction+state sequence it prints a placeholder
  (`(loops for N instructions)`) and skips re-invoking the trace action —
  including our custom `tracelog`. This silently drops `T,` lines for any
  repeated loop body (e.g. mfootb's idle poll loop), corrupting the
  comparison. The debugger's `noloop` trace option disables this
  compression so every retired instruction gets its `T,` line. This was
  the single biggest fix — without it, `make golden SCENARIO=idle` failed
  within the first ~10 instructions purely from missing collapsed lines,
  not a real CPU divergence.
- **`emu.register_start` doesn't exist in MAME 0.288's Lua API** (the brief
  used it as a placeholder). The closest analog, `emu.register_prestart`,
  never fires either: `-autoboot_script` runs after the machine's initial
  `MACHINE_NOTIFY_RESET` (which is what drives prestart) has already gone
  by, so the callback attaches to an event that already happened.
  `tools/golden/hold_input.lua` instead calls
  `manager.machine.ioport.ports[port].fields[field]:set_value(1)` directly
  at the script's top level, which works immediately (confirmed via a
  `print_info` probe).
- `-debug` alone was sufficient — no debugger window ever blocked or
  awaited input; `-video none -sound none -nothrottle` combined with
  `-debugscript`'s trailing `go` ran the whole 45 emulated seconds
  unattended.

No `-debugger none` / `-debugger_font` workaround was needed on this MAME
build.

## Two real divergences found and fixed (both in the *test harness*, not
## in `src/b6100_cpu.v`)

### 1. Reset-time instruction offset

MAME's `-debug` halts the CPU at the reset vector *before* executing the
first instruction; `-debugscript`'s `trace` command is then installed and
`go` resumes. The trace hook only starts firing on the *next* instruction
fetch, so MAME's golden trace never contains a `T,` line for the very
first (reset-time) instruction — the one already "current" when the debug
break happened. Our tracegen originally logged that state too (pc=0,
a=0, c=0, b=00, s=000 — the pure post-reset state). Fix: `sim/b6100_cpu_trace.cpp`
now skips printing for loop iteration `i == 0`, so our first emitted line
is the state after the first instruction retires — exactly matching
MAME's first line.

### 2. Momentary-button input latency (MAME ioport frame polling)

mfootb's kb/din fields are read directly from ioports
(`m_maincpu->read_kb().set_ioport("IN.0")`, similarly for DIN — no digit
multiplexing in this driver). But MAME only samples **digital** ioport
fields into their live value once per *emulated video frame*
(`ioport_manager::frame_update()`, hooked to `MACHINE_NOTIFY_FRAME`). For
this screenless driver that's `video`'s default 60 Hz screenless timer
(`screen_device::DEFAULT_FRAME_PERIOD`), **not every CPU cycle**. A field
held from machine start via our Lua script therefore reads as 0 for the
first ~1/60 s of emulated time (a real, deterministic modeling detail of
MAME's own ioport code, confirmed reproducible run-to-run), and only
becomes visible to `read_kb()`/`read_din()` after that first frame
boundary elapses.

Static/CONFNAME-style bits (e.g. DIN bit 0, Difficulty, default PRO1) are
baked into the port's default live value at machine reset and need no such
settling — this is exactly why `SCENARIO=idle` (which relies only on that
default bit, no held field) passed on the very first try with no timing
model at all.

Fix: `b6100_trace` gained an optional `[settle_cycles]` argument. For the
first `settle_cycles` clock cycles it forces `kb=0` and masks `din` down to
just the Difficulty bit (`din & 0x1`); from `settle_cycles` onward it uses
the full requested `kb`/`din`. This mirrors MAME's own first-frame input
latency in the test harness only — `src/b6100_cpu.v` itself (and any
real-hardware wiring in later plans) still reads `kb`/`din` live every
cycle, as real silicon does.

`GOLDEN_SETTLE` (Makefile, default `1000`) was found by bisection against
a 2-second MAME capture (`fwd` scenario): values from ~600 to ~1480 cycles
all reproduce MAME's actual activation edge exactly (i.e. the real edge
falls inside that window and our tracegen's cycle-granularity model can't
distinguish further without also modeling MAME's multi-cycle instruction
timing) — 1000 was picked as the middle of that safe range. All four
scenarios pass with this same constant, confirming the edge is a fixed
property of the 60 Hz timer, independent of which field/port is held.

## Scenario table

| scenario | kb  | din | held field (via `tools/golden/hold_input.lua`) |
|----------|-----|-----|--------------------------------------------------|
| idle     | 0   | 1   | none (DIN bit0 Difficulty=PRO1 is the static default) |
| fwd      | 2   | 1   | `:IN.0` / `Forward`                               |
| kick     | 8   | 1   | `:IN.0` / `Kick`                                  |
| score    | 0   | 3   | `:IN.1` / `Score`                                 |

## Results (this run, `GOLDEN_N=2000000`, `-seconds_to_run 45`)

| scenario | instructions matched | mame lines available | ours lines |
|----------|----------------------|-----------------------|------------|
| idle     | 1,381,232            | 2,176,223             | 1,381,232  |
| fwd      | 1,529,840            | 2,431,108             | 1,529,840  |
| kick     | 1,493,559            | 2,394,828             | 1,493,559  |
| score    | 1,446,397            | 2,278,936             | 1,446,397  |

All four: **PASS** — every one of our retired instructions matches MAME's
corresponding instruction exactly (pc, a, c, b, s), from boot through
`GOLDEN_N` instructions.

## How to re-run

```bash
make golden SCENARIO=idle
make golden SCENARIO=fwd
make golden SCENARIO=kick
make golden SCENARIO=score
```

Each target builds `tracegen` if needed, runs MAME headless to produce
`golden.tr`, runs our Verilator model to produce `ours.csv`, and diffs
them. `golden.tr` and `ours.csv` are gitignored scratch output in the repo
root; re-running regenerates them from scratch each time (`rm -f
golden.tr` happens automatically; `ours.csv` is overwritten).

Override `GOLDEN_N` (instructions to run on our side) or `GOLDEN_SETTLE`
(button-hold settle cycles) if needed, e.g.:

```bash
make golden SCENARIO=fwd GOLDEN_N=500000
```

## Hardware bring-up (Plan 4)

`core_top.v` wires the real B6100 CPU, display renderer, audio, and
APF-bridge ROM loader onto a physical Analogue Pocket (firmware 2.5). Boot
worked on the first flash (no "General Error"/"Error in core setup"), but
the game was completely dead — no lit LEDs, no sound, no response to
input — despite everything looking correct in simulation. Two real,
hardware-only bugs were found and fixed via an on-screen diagnostic overlay
(temporary colored status bars rendered over the game area, added and later
fully removed from `core_top.v`/`football_system.v`/`rom_loader.v`), since
neither is reachable from `make sim`'s testbenches:

### 1. ROM data slot was never actually requested from APF

`core_top.v`'s template scaffolding declares `target_dataslot_read` (and
its id/offset/bridge-address/length parameters) but never drives them.
`"required": true` in `data.json` only makes APF refuse to boot without the
file present — it does **not** auto-transfer the file's bytes over the
bridge. That transfer only happens if the core itself issues a
`target_dataslot_read` target-command (`core_bridge_cmd.v` is purely a
host/target command relay; nothing in the vendored template fires this on
its own). Without it, `rom_loader`'s BRAM stayed at its power-on-zero
state, so the CPU executed opcode `0x00` (NOP) forever — silent, no lit
LEDs, unresponsive to input, yet the CPU was technically "alive" (clock
enable pulsing, PC nominally static at one address).

Fix: `core_top.v` now fires `target_dataslot_read` once, for data slot id
0, on the rising edge of `status_setup_done` (per `core_bridge_cmd.v`'s own
comment that this is the intended trigger point), requesting the full
896-byte ROM at bridge address `0x10000000` (matching `data.json`).

### 2. ROM bytes arrived byte-reversed within each 32-bit word

Even after the transfer started firing, the game still didn't run. A debug
readback of the literal first word `rom_loader` stored showed `0x1D` in the
byte position expected to hold `mfootb.bin`'s real first byte (`0x0C`) —
`0x1D` is actually the file's **fourth** byte. APF packs each 32-bit bridge
write **big-endian** (file byte 0 -> bits[31:24]), but `rom_loader.v`'s
`byte_sel*8` extraction assumed little-endian (byte_sel 0 -> bits[7:0]),
so every 4-byte group was read back in reverse order. This is why the CPU
appeared briefly "alive" driving `str`/`seg`/`spk` right after boot (byte-
reversed opcodes are still valid 8-bit values, just not the real program)
but never ran actual game logic afterward.

Fix: `rom_loader.v` now selects from the high end of the word
(`byte_sel_rev = ~byte_sel`) to match APF's actual packing. The
`rom_loader_tb.cpp` sim testbench's `load()` helper — which had the same
unverified little-endian assumption, since no APF simulation exists to
catch this before real hardware — was corrected to match.

Neither bug was reachable by any existing sim testbench: Plan 3's
human-reviewed gameplay frames (`football_system_tb.cpp`) feed ROM bytes
directly from the file via `fread()`, bypassing `rom_loader`/the APF bridge
entirely — that path only gets its first real exercise on physical
hardware, which is exactly why both bugs surfaced only at this stage.

### Confirmed working (human, on physical Pocket, 2026-07-26)

Score/time display (Start button), down-and-distance display (Select
button), gameplay, and audio all confirmed working as expected. D-pad
Right maps to "Forward" regardless of which side is on offense (a known
UX quirk of the original real-cabinet control scheme, not a bug) — a
possible "flip controls for player 2" option is deferred to a later plan.

## Bezel overlay hardware bring-up (Plan 5)

`video_renderer.v` was extended with a layered bezel compositor (LED
segments/dashes, a label-text ROM, and a procedural background) per
`docs/superpowers/specs/2026-07-26-bezel-overlay-design.md`. Implemented via
`superpowers:subagent-driven-development` for Tasks 1-2 (label ROM,
renderer rewrite); Task 3 (core_top.v wiring) was built and packaged by a
subagent, but the resulting hardware bugs below were found and fixed via
direct, hands-on hardware debugging (tight iterative loop against real
photos from the human, not a fit for the subagent dispatch/report cycle).

### Bug: zero horizontal front porch (canvas 502 wide)

The initial design widened the canvas from 400 to 502px to better match the
overlay art's native aspect ratio, reasoning that `H_TOTAL=512` minus the
existing 10px back porch left 502px of budget. This was wrong: it left
`H_TOTAL - H_ACTIVE - H_BPORCH = 512 - 502 - 10 = 0` cycles of horizontal
**front** porch, versus the original 400-wide config's 102 cycles. On real
hardware this produced whole-screen banding/ghosting and repeated,
overlapping content — visible even with the bezel disabled, confirming a
core video-timing bug rather than a bezel-rendering one. Fixed by reverting
to the exactly-400-wide configuration already validated across every prior
hardware session in this project (zero new timing risk), rather than
picking another untested width.

### Bug: dash marks not centered between field dividers

Independent of the timing bug: `dash_x(col)` was computed flush against the
left divider of each field column (`4 + 55*col`) instead of centered in the
42px gap between dividers. Fixed with the correct centering margin
(`16 + 44*col` at the reverted 400-wide geometry). Found by code inspection
after the human reported "LEDs not centered between yardlines," not by
guessing.

### Config-only fix: aspect ratio / letterboxing

The Pocket's `video.json` `aspect_w`/`aspect_h` declares the *display*
aspect ratio; when it differs from the pixel buffer's own `width:height`
ratio, the scaler stretches the buffer to fill it non-uniformly. Setting
`aspect_w:aspect_h` to the overlay's true 320:201 ratio (vs. the buffer's
400:360) caused visible horizontal stretching of all content, including the
digit displays — reported by the human as "numbers seem stretched too
wide" and "font weight too heavy." Correct fix (in progress): bake true,
undistorted-aspect content into the fixed 400x360 buffer directly (uniform
scale to fit width, black bars top/bottom for the remainder), and declare
`aspect_w:aspect_h` matching the buffer's own 400:360 ratio so the scaler
applies no further distortion — letterboxing happens inside our own pixel
buffer, not via scaler stretching.

### Confirmed working (human, on physical Pocket, 2026-07-26)

Bezel on/off toggle (`interact.json`'s "Presentation" setting), label text
legibility, digit/dash placement, and no video-timing artifacts, all
confirmed at the reverted 400-wide canvas. Aspect-ratio letterboxing and
font-weight refinement were still in progress as of this writing (see
above) — this section will be updated once that lands.

---

# Display verification: the MAME display-parity gate

The golden-trace gate above proves the CPU matches MAME instruction for
instruction. It says nothing about what reaches the screen, because
everything downstream of `str`/`seg` — the duty measurement in
`led_capture.v` and the level→colour mapping in `video_renderer.v` — is ours,
not the CPU's. `make display-parity` closes that gap.

Ported from the Football II core's plan (`docs/flicker-port-plan.md` on the
`docs/flicker-port-plan` branch, written by the agent that built FB2).

## What the display model is

MAME's `mfootb` (`src/mame/handheld/hh_rw5000.cpp`, a Rockwell B6100)
configures:

```cpp
PWM_DISPLAY(config, m_display).set_size(9, 11);
m_display->set_segmask(0x08, 0xff);      // only one digit has a DP
m_display->set_bri_levels(0.02, 0.2);    // "player led is brighter"
```

and inherits `pwm_display_device`'s default `set_interpolation(0.5)`. Per
cell, per 60 Hz frame, `pwm.cpp`'s `frame_tick` does:

```cpp
double bri = m_bri[y][x] * factor1 + (m_acc[y][x].as_double() / frame_time) * factor0;
```

with `factor0 = 0.5` — an IIR at alpha = 1/2 — and *then* classifies the
smoothed value against the two levels. `led_capture.v` mirrors all of it:
`WINDOW = 1167` ce ticks (280 kHz / 4 instruction rate / 60 Hz = 59.98 Hz),
thresholds at 2% and 20%, and a per-cell `smooth[]` folded at alpha = 1/2.

Two deliberate deviations, both commented in the RTL:

- **`pwm.cpp`'s cutoff clamp is not modelled.** It clamps `bri` to
  `4 × 0.2 = 0.8` before storing. With 9 columns multiplexed no cell
  approaches 80% duty, so the clamp is unreachable.
- **The `+1` rounding offset** on the IIR is ours, not MAME's (which works
  in doubles). It makes `smooth == cnt` an exact fixed point, so a cell held
  at exactly a threshold settles on it instead of creeping one count short
  forever. `test_steady_state_is_exact` in `sim/led_capture_tb.cpp` is the
  guard.

## Constants that must not be copied from FB2

FB2's core looks similar and its numbers are all wrong here. Copying them
produces a display that still looks plausible, which is what makes it
dangerous.

| Constant | FB2 | FB1 (this core) | Why |
|---|---|---|---|
| Window length | 1583 | **1167** | 380 kHz/4 vs 280 kHz/4 |
| Brightness levels | 0.015 / 0.2 | **0.02 / 0.2** | different `set_bri_levels` |
| Matrix size | 10 x 11 | **9 x 11** | different `set_size` |
| Cell count | 110 | **99** | follows from matrix size |

All four are verified by mutation below.

## How the comparison works

`tools/golden/dump_display.lua` runs under MAME and writes one CSV row per
frame of the 9×11 matrix, read from `pwm_display_device`'s `"y.x"` outputs.
`sim/display_parity_tb.cpp` writes the same layout from our `levels[]`,
tapped out of `football_system.v` as `dbg_levels`.
`tools/golden/display_diff.py` compares them.

The two runs are **not** phase-locked — MAME reclassifies on its own 60 Hz
timer, we reclassify every 1167 ce ticks (59.98 Hz), and neither starts its
first window at the same point in the ROM's multiplex loop. Comparing frame
N to frame N is meaningless. So the differ compares, per cell:

- whether it is steady or changing;
- if steady, its level;
- if changing in both, the *set* of levels visited and the fraction of frames
  spent at each, within `--blink-tol` (default 15 points).

That last clause matters. Exempting every cell that changes in both — the
obvious shortcut — would exempt exactly the cells the test exists to check:
on the `fwd` scenario all ten moving dash cells change, including the ball
carrier.

`DISPLAY_N` defaults to 420 frames rather than something shorter because the
two captures sit about one frame apart in phase. A window that *ends* inside
a transient shows one side a step further along the same decay ramp than the
other; at 180 frames the `fwd` scenario cut exactly into the ball carrier's
decay and reported a difference that a longer window shows is only that
offset.

## Results

`make display-parity-all` — **99/99 cells on all four scenarios**
(idle, fwd, kick, score).

For comparison, FB2's equivalent test matches 109 of 110, its one exception
being a blinking cell the two runs cannot phase-lock. The share-based blink
comparison above is what lets this one reach 99/99 rather than needing a
similar carve-out.

## One real bug found

The `score` scenario initially reported 5 mismatching cells, all on line 7 —
the decimal point. MAME's `mfootb_state::update_display()` is:

```cpp
// 4th digit DP is from the SCORE button
u8 dp = (m_inputs[1]->read() & 2) ? 0x80 : 0;
m_display->matrix(m_str, (m_seg << 1 & 0x700) | dp | (m_seg & 0x7f));
```

The DP line is OR'd into **every** strobe column straight from the Score
button, bypassing the CPU's `seg` outputs entirely. Our RTL models this
correctly (`led_capture.v` takes `dp_in` from `score_btn`), but the harness
was driving `din` bit 1 without also driving `score_btn`. They are the same
physical button.

**This is a live constraint on the APF top level:** whatever wires DIN must
wire `score_btn` from DIN bit 1, or the decimal point never lights.

## Proving the tests can fail

Every claim above was checked by reintroducing the bug and watching the test
go red. Note the split — the parity test catches interpolation errors, the
unit test catches threshold errors, and neither covers the other:

| Mutation | `sim-led_capture` | `display-parity` |
|---|---|---|
| alpha = 1 (no interpolation) | FAIL | FAIL (`score` only) |
| alpha = 1/8 (FB2's old over-smoothing) | FAIL | FAIL (`fwd`, `score`) |
| truncate instead of `+1` rounding | FAIL | pass |
| FB2's dim level 0.015 | FAIL | pass |
| FB2's `WINDOW = 1583` | FAIL | FAIL (`fwd`, `kick`) |

No single scenario catches every mutation, which is why
`display-parity-all` rather than `display-parity` is the gate. `idle` catches
none of them on its own; it is kept because it is the attract state the core
boots into, not because it is load-bearing.

### A trap when running these

Verilator's `--build` skips recompiling when the `.v` file's mtime lands in
the same second as the previous build — which a scripted mutate/run/restore
loop does constantly. A "restored" run reported the *previous mutation's*
failures. Delete `sim/obj_dir_<name>/` between mutations; do not trust a
mutation result from an incremental build.

## How to re-run

```sh
make display-parity-all                        # all four scenarios
make display-parity DISPLAY_SCENARIO=fwd       # one scenario
make display-parity DISPLAY_SCENARIO=fwd DISPLAY_N=900   # longer window
```

Needs `mame` on `PATH` with `mfootb` available — the same setup the
golden-trace gate above uses.
