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

Simulation and the core itself require the original Mattel Football
ROM — 896 bytes, CRC32 5b27620f (the `b6100eb` file from the MAME `mfootb`
romset). It is copyrighted and never distributed with this repository. Place
it at `sim/roms/mfootb.bin` (gitignored) for `make smoke` / `make golden` /
`make frames`.

## Running on your Analogue Pocket

1. Build and stage the core: `make bitstream && make package`. This
   produces `dist/Cores/cjchand.Mattel Football/` and `dist/platforms/`.
2. Copy the contents of `dist/` onto your Pocket's SD card root, merging
   into the existing `Cores/` and `Platforms/` folders (the folder is
   still named `Cores/cjchand.Mattel Football` — don't rename it, the
   core won't boot if it doesn't match `core.json`'s declared identity).
3. Supply your own dump of the ROM (see "Game ROM" above) at
   `Assets/ex_platform/common/mfootb.bin` on the SD card, creating those
   folders if they don't already exist.
4. Boot the core from the Pocket's core list. Controls: D-pad Up/Down/Right
   move, A kicks, Start shows score/time, Select shows down-and-distance.
   The "Presentation" setting (in the core's options menu) toggles the
   bezel artwork on/off.

If you'd rather not build it yourself, ask whoever shared this repo with
you for a copy of `dist/` (steps 2-4 above still apply) — the ROM is never
included either way.
