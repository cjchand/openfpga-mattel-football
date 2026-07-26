# Mattel Football — Analogue Pocket openFPGA core

Recreates the original 1977 Mattel Electronic Football by emulating its
Rockwell B6100 CPU running the real ROM (user-supplied, not included).

Design spec: `docs/superpowers/specs/2026-07-25-mattel-football-core-design.md`

## Installing on your Analogue Pocket

No building required — this repo ships a ready-to-use core in the `dist/`
folder. All you need is the repo and your own dump of the game ROM.

1. **Get this repo onto your computer.** Either `git clone` it, or on GitHub
   click the green "Code" button → "Download ZIP" and unzip it.
2. **Copy `dist/` onto your Pocket's SD card.** Open the `dist` folder from
   this repo, and copy everything inside it (the `Cores` and `Platforms`
   folders) onto the root of your Pocket's SD card, merging into the SD
   card's existing `Cores/` and `Platforms/` folders (say "merge"/"yes to
   all" if your OS asks — don't replace the whole folder). Don't rename
   anything; the core's folder must stay named exactly
   `Cores/cjchand.Mattel Football` or the Pocket won't boot it.
3. **Add the game ROM yourself — it's not included.** This repo can't ship
   the original Mattel Electronic Football ROM (it's copyrighted), so
   you'll need to supply your own dump: 896 bytes, CRC32 `5b27620f`, the
   `b6100eb` file from the MAME `mfootb` romset. Rename it to `mfootb.bin`
   and place it on the SD card at:
   `Assets/mattel_football/common/mfootb.bin`
   (create the `Assets`, `mattel_football`, and `common` folders if they
   don't already exist).
4. **Eject the SD card, put it back in your Pocket, and boot the core**
   from the core list (it'll show up under the "Mattel Football" platform,
   category Handheld). Controls: D-pad Up/Down/Right move, A kicks, Start
   shows score/time, Select shows down-and-distance. In the core's options
   menu (long-press the Pocket's menu button while the core is running):
   "Overlay" toggles the bezel artwork on/off, and "PRO 2 (Hard)" switches
   the game's difficulty (unchecked is PRO 1, the default).

## Building it yourself (optional)

Only needed if you want to modify the core. Prerequisites:

- macOS: `brew install verilator` (simulation)
- Docker Desktop with Rosetta x86 emulation enabled (Quartus synthesis)

Commands:

- `make sim` — run all simulation testbenches
- `make bitstream` — compile the FPGA bitstream (Docker, slow)
- `make package` — stage the rebuilt core into `dist/`

Simulation (`make smoke` / `make golden` / `make frames`) also needs the ROM,
at `sim/roms/mfootb.bin` (gitignored) — see step 3 above for how to get it.
