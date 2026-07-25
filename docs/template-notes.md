# core-template vendoring notes

- Vendored from: https://github.com/open-fpga/core-template (commit:
  `da3a021b1eaf742604d86d8dc9b33a6666263e6a`, the HEAD of the default branch at
  vendor time, 2026-07-25; original commit dated 2023-05-03).
- License: the upstream `open-fpga/core-template` repository has **no LICENSE
  file** (confirmed both by directory listing of the clone and by
  `https://api.github.com/repos/open-fpga/core-template` reporting
  `"license": null`). The brief's Step 1 assumed a top-level `LICENSE` file to
  copy to `docs/CORE_TEMPLATE_LICENSE`; that file does not exist upstream, so
  it was not vendored. `docs/CORE_TEMPLATE_LICENSE` is absent from this repo.
  Analogue's developer program terms (see the template's README "Legal"
  section) govern use of the vendored code instead of an OSS license.
- Quartus project file: `src/fpga/ap_core.qpf` (matches the brief's example
  exactly — exactly one `.qpf`, plus a matching `src/fpga/ap_core.qsf`, an
  `src/fpga/apf/` framework directory, and an `src/fpga/core/` directory for
  user logic).
- RBF output after compile: `src/fpga/output_files/ap_core.rbf`.
  Note: the vendored template ships this file already built and checked into
  its own git history (787,952 bytes at vendor time, alongside `ap_core.sof`
  and `ap_core.jdi`). Running `make bitstream` recompiles the project from
  scratch in the Docker Quartus image and overwrites this file with a
  freshly-built RBF. Confirmed by local build on 2026-07-25: `make bitstream`
  ran `quartus_sh --flow compile ap_core.qpf` in
  `didiermalenfant/quartus:22.1-apple-silicon`, took about 1m28s wall time
  (Quartus-reported), and finished with `Quartus Prime Full Compilation was
  successful. 0 errors, 192 warnings` — including the individual stage lines
  `Quartus Prime Analysis & Synthesis was successful`, `Quartus Prime Fitter
  was successful`, and `Quartus Prime Assembler was successful` (all 0
  errors). The resulting `src/fpga/output_files/ap_core.rbf` is 786,964 bytes
  (nonempty, and its MD5 differs from the template's pre-built copy,
  confirming it was freshly compiled rather than left over from vendoring).
  Task 4 should read the RBF from this path: `src/fpga/output_files/ap_core.rbf`.
- dist/ layout (top-level `dist/`, sibling of `src/`, vendored from the
  template's own top-level `dist/`):
  - `dist/.gitkeep`
  - `dist/icon.bin`
  - `dist/assets/.keep`
  - `dist/platforms/ex_platform.json`
  - `dist/platforms/_images/ex_platform.bin`
  This is example/placeholder content (an example platform JSON + image, an
  example icon, and an empty assets dir) — Task 4 will replace/extend these
  with the real Mattel Football core/platform manifests.

- **Correction (Task 4):** the Task 3 vendor step copied the template's
  `src/fpga/` and `dist/` but missed that upstream `open-fpga/core-template`
  keeps its APF core manifests at the **repository root**, not under `dist/`:
  `core.json`, `video.json`, `audio.json`, `data.json`, `input.json`,
  `interact.json`, `variants.json`, and `info.txt` all live at the repo root
  in the upstream clone (verified against `/tmp/core-template`, whose
  `git rev-parse HEAD` was re-confirmed to still match the vendored commit
  `da3a021b1eaf742604d86d8dc9b33a6666263e6a` before copying). There is no
  `Cores/` folder or `core.json` anywhere upstream in `dist/` itself — the
  `Cores/<Author>.<Shortname>/` staging directory is something the *packager*
  (this project, at package time) must construct by placing those root-level
  manifest files alongside the bitstream, not something the template ships
  pre-built.
- `dist/Cores/Developer.Core Template/` was created and populated with the
  eight root-level manifest files above, copied verbatim from
  `/tmp/core-template`. The upstream example identity (`"author": "Developer"`,
  `"shortname": "Core Template"`, per its `core.json`) was kept as-is —
  renaming to a real Mattel Football identity is deliberately deferred to
  Plan 5 (artwork/settings/release packaging), which is out of scope here.
  `core.json`'s `"cores"` entry declares `"filename": "bitstream.rbf_r"`, so
  the reversed bitstream must be staged at
  `dist/Cores/Developer.Core Template/bitstream.rbf_r` — this is the value
  `Makefile`'s `RBF_R_DEST` is set to (explicitly, not `find`-derived, since
  no placeholder `.rbf_r` ships in the template to search for).
- SD-card mapping for `make package`'s output (per Analogue's Pocket
  packaging docs): copy `dist/`'s contents onto the SD card root such that
  `dist/Cores/*` merges into the card's `/Cores/` folder, and
  `dist/platforms/ex_platform.json` + `dist/platforms/_images/` merge into
  the card's `/Platforms/` folder (Pocket folder names are capitalized;
  the vendored `dist/` uses lowercase `platforms/` as its local staging name
  for that same content — Analogue's SD-card side folder is `Platforms/`).

Do not upgrade Quartus device settings in the .qsf — preconfigured for the
Pocket's Cyclone V.

## Hardware boot test findings (2026-07-25, firmware 2.5)

- The SD folder under `/Cores/` MUST be named exactly `<author>.<shortname>`
  from core.json (`Developer.Core Template` for the template). A mismatched
  folder name produces "Load error in 'core': General Error" + "Error in
  core setup". Spaces in the folder name are fine.
- Our Quartus 22.1-built bitstream (786,964 bytes) boots correctly. The
  upstream prebuilt (787,952 bytes, older Quartus) also boots — RBF size
  varies across Quartus versions and both are valid.
- `tools/reverse_rbf.py` validated byte-for-byte against upstream's
  known-good rbf/rbf_r pair.
