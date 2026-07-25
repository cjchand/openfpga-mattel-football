# Project Brief: Mattel Electronic Football — Analogue Pocket openFPGA Core

## Goal
Build an openFPGA core for the Analogue Pocket that recreates the original 1977 Mattel Electronic Football handheld.

## Target hardware: the 1977 original, not the 2000/2001 reissue
Two versions exist and they are **not** the same hardware:

| | Original (1977) | Reissue "Classic Football" (2000) |
|---|---|---|
| CPU | Rockwell B6100-15 (modified calculator chip, known part #) | Unmarked "glob top" chip on board, no public part number |
| Display | True LED matrix (9x3 field + 7-segment score) | Backlit LCD simulating the LED look (6 red LEDs behind an LCD panel) |
| Power | 9V battery | 2x AA |
| Gameplay | Original ruleset | Reissue and "Classic Football 2" added passing/kicking/backward movement — different game logic |

**Decision: target the 1977 original.** It has a known, named chip and an existing reverse-engineering trail. The reissue is an unmarked, undocumented chip with no public dumps and different gameplay — a much harder and less interesting target.

## Key technical facts
- CPU: Rockwell B6100-15, part of a family of modified Rockwell calculator chips used across early Mattel handhelds (B6000 = Auto Race, B6001 = Space Alert, B6100 = Football, B6101 = Baseball, B6102 = Gravity). All are in 42-pin QIP packages.
- These are **not** standard off-the-shelf microcontrollers — Rockwell customized calculator silicon for Mattel, unlike contemporaries.
- **Clones used standard, well-documented MCUs instead**: a Hong Kong company called Conic cloned Football using a Texas Instruments TMS1000, a widely known and well-documented 4-bit microcontroller family.
- **MAME already emulates the Conic TMS1000-based clone.** This means known-good ROM contents and verified game logic already exist in open source, even if it's the clone rather than the original chip.
- **Sean Riddle (seanriddle.com/firstmattelledgames.html) has done direct reverse-engineering on the original Rockwell chips**, including decapping and reconstructing ROM contents for the Football B6100 by cross-referencing patent filings and an object-code listing. This may mean a verified ROM dump for the *actual original chip* (not just the clone) already exists or is obtainable — worth reaching out to him directly.
- Original patents: US 4,162,792 (gameplay) and US 4,344,622 (display technology) — both public and may contain useful logic-level detail (state machine descriptions, timing) beyond what's in the ROM alone.

## Platform: Analogue Pocket openFPGA
- Pocket uses an Intel/Altera Cyclone V FPGA (49K logic elements, 3.4Mbit BRAM).
- Cores are Quartus-built bitstreams packaged with JSON definition files (the Analogue Platform Framework / APF handles core loading, I/O, etc.)
- Dev docs: https://www.analogue.co/developer/docs/overview
- Existing simple arcade-style Pocket cores worth studying for project structure/boilerplate (JSON manifest format, I/O mapping, save states) — e.g. openfpga-asteroids by ericlewis, existing Q*Bert and Galaga ports. Browse https://openfpga-library.github.io/analogue-pocket/ for current examples.
- Cores are typically written in Verilog, SystemVerilog, or VHDL.

## Two possible implementation paths
1. **Emulate the TMS1000 core in HDL and run the Conic clone ROM.** Lower risk — TMS1000 is extremely well documented, an HDL implementation may already exist open source, and MAME's driver is a working reference for correctness. Downside: it's technically a clone's logic, not the original chip's. 
2. **Emulate the actual Rockwell B6100 if/when a verified ROM + logic description is available** (pending Sean Riddle contact or existing dumps). More authentic, but depends on whether that reverse-engineering has actually reached a state usable for reimplementation (vs. Riddle's own private notes).

Recommendation: start with path 1 as a working proof of concept, since the ROM and reference emulation already exist, then evaluate switching to path 2 depending on what turns up.

## Immediate next steps for Claude Code
1. Search MAME's source tree (`src/mame/`) for the Conic Football / TMS1000 driver and extract the ROM reference and machine config. You can find the MAME source for the TMS1000 here:  ~/Projects/mame/mame/src/devices/cpu/tms1000/tms1000.cpp
2. Search GitHub for existing open-source TMS1000 HDL (Verilog/VHDL) cores — check if one already exists before writing one from scratch.
3. Pull the JSON manifest structure and directory layout from 1-2 existing simple Analogue Pocket cores (e.g. openfpga-asteroids) as a template.
4. Set up local Quartus + openFPGA SDK toolchain per Analogue's developer docs.
5. Scope out display handling: original game is a physical LED grid, not a raster display — decide how to represent this on the Pocket's LCD (dimming/brightening dot simulation is the common approach used by similar VFD/LED-handheld cores).
6. Optional/parallel: reach out to Sean Riddle re: status of any existing B6100 ROM dump for the original (non-clone) chip.