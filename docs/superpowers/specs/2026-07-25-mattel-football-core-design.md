# Design: Mattel Electronic Football (1977) — Analogue Pocket openFPGA Core

**Date:** 2026-07-25
**Status:** Approved design, pre-implementation
**Supersedes:** `docs/initial-plan.md` (research brief; key assumptions updated below)

## Goal

An openFPGA core for the Analogue Pocket that recreates the original 1977 Mattel
Electronic Football (model 2024) by emulating its actual CPU — the Rockwell
B6100 — running the real, verified ROM dump.

Project intent: a learning-first HDL project (developer is new to HDL) that
grows into a polished public release. Design choices favor small, well-bounded,
heavily verified modules over cleverness.

## Key finding that reshaped the initial plan

`docs/initial-plan.md` assumed the authentic-chip path depended on private
reverse-engineering (Sean Riddle) and recommended starting with the Conic
TMS1000 clone. That is out of date: **MAME fully emulates the original B6100
and the `mfootb` machine**, in the local checkout at:

- CPU family: `~/Projects/mame/mame/src/devices/cpu/rw5000/` (~950 lines C++
  total; `b5000.cpp`, `b5000op.cpp`, `b6000.cpp`, `b6100.cpp`, `rw5000base.cpp`)
- Driver: `~/Projects/mame/mame/src/mame/handheld/hh_rw5000.cpp` (`mfootb`)
- ROM: `b6100eb`, 896 bytes, CRC32 `5b27620f`,
  SHA1 `667ff6cabced89ef4ad848b73d66a06526edc5e6`

No public B6100 HDL exists (searched 2026-07-25), so the CPU is written from
scratch with MAME as the executable specification and test oracle.

### Approaches considered

- **A. B6100 in Verilog from scratch (chosen):** authentic chip + real ROM;
  MAME provides an instruction-level oracle; the CPU is among the simplest that
  exist; same family unlocks four more Mattel games later; a community first.
- **B. Adapt existing TMS1000 HDL (mikeakohn/tms1000_fpga) + Conic clone ROM:**
  rejected — iCE40-targeted, deliberately not cycle-accurate (3 vs 6
  cycles/instruction), emulates the clone's logic, and porting ≈ rewriting.
- **C. Native HDL game-logic recreation (no CPU/ROM):** rejected — accuracy
  becomes unverifiable, and it teaches less that transfers to future cores.

## Target machine facts (from MAME)

- Rockwell B6100 (label B6100EB/-15), 42-pin QIP.
- 896×8 ROM with an address hole: bytes load at 0x000–0x2FF and 0x380–0x3FF;
  0x300–0x37F is unmapped. The ROM file is contiguous 896 bytes; the loader
  performs this mapping.
- 48×4 RAM. Registers: A (4-bit accumulator), BL/BU (RAM address pointer),
  C (carry), S (single-level subroutine return), 10-bit PC with the family's
  page/increment quirks, SEG output latch, skip flag.
- Timing: 4-phase clock ≈ 280 kHz; 1 instruction per 4 clocks ≈ 70k
  instructions/sec. MAME's cycle model is 1 cycle/instruction at clock/4; we
  match at instruction granularity (the internal 4-phase detail is not
  observable and not modeled).
- I/O: 9 strobe outputs × 11 segment outputs (multiplexed LED matrix: 7-seg
  digits + discrete dash LEDs); 4 KB button inputs (Up, Down, Forward, Kick);
  4 DIN inputs (PRO 1/PRO 2 difficulty, Score, Status, factory test); 1-bit
  speaker (SPK, aka SEG0).
- Brightness is duty-cycle based: the player's dash is driven at a much higher
  duty cycle than defenders (MAME approximates 0.2 vs 0.02).

## Architecture

Two halves joined by one narrow interface — the LED display state:

```text
EMULATED MACHINE                      PRESENTATION LAYER

b6100_cpu.v  <--- rom.v (896x8, user asset)
    |        <--> ram.v (48x4)
    |
    | strobe/segment outputs
    v
led_matrix.v  ----------------->  led_intensity.v
 (9x11 "currently lit" grid)       (duty cycle -> brightness + decay)
                                        |
buttons  <-- APF input mapping          v
speaker  --> audio.v --> APF i2s   video_renderer.v
                                    (background art + LED glow + 7-seg)
                                        |
                                   APF video out

apf_top.v: clocks, video/audio buses, input mapping, asset loading
```

Everything left of `led_matrix` is verifiable in pure simulation against MAME
with no Pocket involvement; everything right of it is presentation that can
iterate freely without touching emulation correctness.

### Modules

| Module | Responsibility |
|---|---|
| `b6100_cpu` | Fetch/decode/execute, 1 instruction per cycle at a 280 kHz enable; structured to mirror MAME's rw5000 code for side-by-side comparison; one comment per opcode citing the MAME handler it mirrors. Implements the B6100's final behavior directly (no replication of MAME's C++ inheritance chain). |
| `rom` | 896×8 BRAM, initialized from the user-supplied dump via APF data bus at boot, including the 0x300–0x37F hole mapping. |
| `ram` | 48×4, registers or BRAM. |
| `led_matrix` | Samples strobe/segment lines continuously into a 9×11 grid of currently-lit bits. The interface between the halves. |
| `led_intensity` | Integrates each LED's on-time over a ~1-video-frame sliding window → brightness level, plus a short decay tail. Bright-player/dim-defender emerges from measured duty cycle (not hardcoded). Also prevents flicker/beat artifacts between the game's scan rate and 60 Hz video. |
| `video_renderer` | Composites: static background image (bezel art) → unlit-LED ghosts → glowing dashes/7-seg digits from `led_intensity`. Output at reduced resolution (~400×360, palettized — final numbers set in implementation planning) upscaled by the Pocket's scaler. |
| `audio` | 1-bit speaker line → 48 kHz I²S with DC-blocking filter and volume scale. |
| `apf_top` | Analogue framework wrapper: clocks, buses, input mapping, `interact.json` settings plumbing. |

## Controls

| Pocket | Game |
|---|---|
| D-pad Up / Down / Right | Up / Down / Forward |
| A | Kick |
| Start | Score |
| Select | Status |
| Settings menu | PRO 1 / PRO 2 difficulty, factory test |

Post-v1 settings toggle — "original button layout": face-button diamond as
movement (X=Up, B=Down, A=Forward, Y=Kick), echoing the physical unit's button
placement. D-pad remains default.

## Presentation

Chosen style: **full handheld** — static pre-rendered background of the device
face (green bezel, printed field/labels) with the LED layer composited on top.

- Background art is authored offline as an **original recreation** (vector-style
  redraw, not a photo scan of Mattel's printed graphics) — sharper and avoids
  shipping trade dress.
- **Field-only** style (same rendering minus background) is the built-in
  fallback and a settings toggle; it is strictly a subset, so art quality never
  blocks correctness work or schedule.

## APF integration and ROM handling

- Packaging per Analogue's spec: `core.json`, `video.json`, `input.json`,
  `data.json`, `interact.json` + bitstream; structure modeled on a simple
  existing core (e.g. openfpga-asteroids).
- **ROM is user-supplied, never shipped.** `data.json` declares a required
  896-byte asset (the `b6100eb` dump, CRC `5b27620f`); docs point users to the
  MAME romset name. Loader implements the address-hole mapping (documented
  above so it is never rediscovered as a mystery bug).
- `interact.json` exposes: difficulty (PRO 1/PRO 2), presentation toggle
  (full handheld / field only), original-button-layout toggle (post-v1),
  factory test mode.
- **No save states / sleep-wake in v1** — the original device had no
  persistence at all; revisit only if user demand appears.

## Verification strategy (the heart of the project)

1. **Per-opcode unit tests** in simulation for arithmetic/skip/carry edge
   cases, written before or alongside each opcode's implementation.
2. **Golden-trace diff vs. MAME.** A MAME-side harness (debugger Lua script or
   small patch) dumps per-instruction state — PC, A, BL, BU, C, S, RAM writes,
   SEG/STR outputs — for millions of instructions of `mfootb`, including
   scripted button inputs. A Verilator testbench runs the identical ROM +
   inputs and diffs traces; any divergence pinpoints the exact instruction.
3. **Rendered-frame review:** Verilator dumps renderer output as PNGs for
   visual inspection before hardware.
4. **On-device smoke test** last: controls, sound, display on a real Pocket.

Hardware is never used to discover correctness bugs — only integration ones.

## Toolchain

- **Simulation:** Verilator + C++ testbenches, native on macOS (fast loop;
  ~90% of development).
- **Synthesis:** Quartus Lite (version per Analogue's openFPGA docs, currently
  18.1) in an x86 Linux Docker container (Rosetta emulation on Apple Silicon —
  slow but scriptable). `make bitstream` wraps build + reverse-RBF + packaging.
- **MAME:** local build used as trace oracle and playable reference.
- **Repo layout:** `src/` (one module per file), `sim/` (testbenches, trace
  tools), `dist/` (APF JSON, packaging), `docs/`, root `Makefile`.
  `.superpowers/` is gitignored.

## Milestones (v1)

1. Repo scaffold + toolchain proven: Docker Quartus builds a trivial
   passthrough core that boots on the Pocket.
2. `b6100_cpu` passes per-opcode unit tests in Verilator.
3. Full ROM runs; golden-trace diff vs. MAME is clean over extended play.
4. LED capture + renderer produce correct simulated frames (field-only style).
5. Core boots on Pocket: playable game, sound, controls.
6. Bezel artwork pass (full-handheld style) + settings menu entries.
7. Release prep: README, ROM sourcing instructions, versioned release zip.

## Roadmap (post-v1)

- **Tier 1 — same B6100/rw5000 family, CPU reused nearly verbatim** (all
  dumped and emulated in `hh_rw5000.cpp`): Auto Race (B6000, 1976), Missile
  Attack / Space Alert (1977), Baseball (B6101, 1978), Gravity (B6102, 1980).
- **Tier 2 — Football II (model 1050, 1978):** Rockwell MM77LA, a PPS-4/1
  family MCU with 2-bit sound — emulated in MAME's `hh_pps41.cpp` with dumped
  ROM. Requires a second CPU core but reuses this project's display/audio/APF
  infrastructure and the same MAME-oracle verification method.
- Original button layout toggle (see Controls).

## References

- MAME rw5000 CPU family: `src/devices/cpu/rw5000/`
- MAME driver: `src/mame/handheld/hh_rw5000.cpp` (`mfootb`)
- Football II driver: `src/mame/handheld/hh_pps41.cpp` (`mfootb2`)
- Patents: US 4,162,792 (gameplay), US 4,344,622 (display)
- Sean Riddle's RE notes: seanriddle.com/firstmattelledgames.html
- Analogue openFPGA docs: https://www.analogue.co/developer/docs/overview
- Existing TMS1000 HDL (evaluated, not used):
  https://github.com/mikeakohn/tms1000_fpga
