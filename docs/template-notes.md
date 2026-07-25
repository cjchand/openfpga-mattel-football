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
  with the real Mattel Football core/platform manifests. No `Cores/...` or
  `Platforms/...` (Pocket Cores-folder-style) subtree exists in the template's
  `dist/` — it only has `platforms/` (lowercase, singular naming as shown
  above) plus `assets/` and `icon.bin`.

Do not upgrade Quartus device settings in the .qsf — preconfigured for the
Pocket's Cyclone V.
