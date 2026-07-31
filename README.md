# Mattel Football — Analogue Pocket openFPGA core

> Note: As shown in the contributors section, Claude was used to create
> this project - in fact, it wrote the whole thing. So, apologies in
> advance for any norms or standards violated. We're both new to
> OpenFPGA development :)

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
   category Handheld). Controls: D-pad Up/Down and Left-or-Right move,
   the left or right face button (Y/A) kicks, the top face button (X)
   shows score/time, the bottom face button (B) shows down-and-distance
   (Start/Select also do the score/down-and-distance functions). In the
   core's options menu (long-press the Pocket's menu button while the core
   is running): "Overlay" toggles the bezel artwork on/off, "PRO 2 (Hard)"
   switches the game's difficulty (unchecked is PRO 1, the default), and
   "Original Controls" swaps the D-pad and face buttons' roles so buttons
   are on the left and movement on the right, matching the original
   device's layout.

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

## License

This project's own code (RTL, tooling, docs) is MIT-licensed — see
`LICENSE`. It vendors `open-fpga/core-template`, which ships no license of
its own and is governed by Analogue's openFPGA developer program terms
instead (see `docs/template-notes.md`). The Mattel Electronic Football ROM
is not included and is not covered by this license — you must supply your
own legally obtained dump.
