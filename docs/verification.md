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
