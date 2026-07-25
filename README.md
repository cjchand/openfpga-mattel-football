# Mattel Football — Analogue Pocket openFPGA core

Recreates the original 1977 Mattel Electronic Football by emulating its
Rockwell B6100 CPU running the real ROM (user-supplied, not included).

Design spec: `docs/superpowers/specs/2026-07-25-mattel-football-core-design.md`

## Prerequisites

- macOS: `brew install verilator` (simulation)
- Docker Desktop with Rosetta x86 emulation enabled (Quartus synthesis)

## Building

- `make sim` — run all simulation testbenches
- `make bitstream` — compile the FPGA bitstream (Docker, slow)
- `make package` — stage a Pocket-installable core under `dist/`

## Game ROM

Simulation and (later) the core itself require the original Mattel Football
ROM — 896 bytes, CRC32 5b27620f (the `b6100eb` file from the MAME `mfootb`
romset). It is copyrighted and never distributed with this repository. Place
it at `sim/roms/mfootb.bin` (gitignored) for `make smoke` / `make golden`.
