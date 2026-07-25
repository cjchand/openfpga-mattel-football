# Toolchain + Repo Scaffold Implementation Plan (Plan 1 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the entire build/test pipeline end-to-end: Verilator simulation runs natively on macOS, Docker Quartus produces a Pocket bitstream from the official openFPGA template, and the packaged core boots on real hardware.

**Architecture:** Two independent toolchains meeting at a Makefile: (1) native Verilator + C++ testbenches for fast simulation (where nearly all later development happens), proven here with a throwaway `blink` module; (2) an x86 Quartus container that compiles the vendored `open-fpga/core-template` Quartus project into an RBF, which a small Python tool bit-reverses into the RBF_R format the Pocket requires.

**Tech Stack:** Verilog-2001, Verilator (Homebrew), C++17 testbenches, Docker (x86 emulation via Rosetta), Quartus Prime in `didiermalenfant/quartus:22.1-apple-silicon`, Python 3 for packaging tools, GNU Make.

**Spec:** `docs/superpowers/specs/2026-07-25-mattel-football-core-design.md` (this plan covers Milestone 1).

## Global Constraints

- Host is macOS (Apple Silicon); Quartus runs only inside Docker with `--platform linux/amd64`.
- Quartus image: `didiermalenfant/quartus:22.1-apple-silicon`. Fallback if the Pocket rejects the bitstream: `raetro/quartus:18.1` (Analogue's docs specify Quartus Lite 18.x+ with Cyclone V support).
- Do not change the FPGA device/part settings in the template's `.qpf`/`.qsf` — the template is preconfigured for the Pocket's Cyclone V.
- The Pocket requires RBF_R: every byte of the RBF bit-reversed (Analogue "Packaging a Core" docs).
- Repo layout (from spec): `src/` HDL, `sim/` testbenches + tools, `dist/` APF JSON/packaging, `docs/`, root `Makefile`. The vendored Quartus project lives under `src/fpga/` (template convention).
- Every commit message ends with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- `.superpowers/` and `.DS_Store` are gitignored (already committed).

---

### Task 1: Verilator toolchain proof (`blink` module + testbench + `make sim`)

**Files:**
- Create: `src/blink.v`
- Create: `sim/blink_tb.cpp`
- Create: `Makefile`
- Create: `README.md`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `make sim` — builds and runs every C++ testbench listed in the Makefile `SIM_TESTS` variable; exits nonzero on failure. Plan 2 replaces `blink` with real modules but keeps the `make sim` contract and the `sim/*_tb.cpp` naming convention.
- Note: `blink.v`/`blink_tb.cpp` are throwaway toolchain proofs; Plan 2 deletes them when the first real module lands.

- [ ] **Step 1: Verify/install Verilator**

Run: `verilator --version || brew install verilator`
Expected: `Verilator 5.x` (any 5.x is fine).

- [ ] **Step 2: Write the failing testbench first**

Create `sim/blink_tb.cpp`:

```cpp
// Toolchain smoke test: drives the throwaway blink counter and checks its LED
// output. Deleted in Plan 2 when the first real module replaces it.
#include "Vblink.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vblink& dut) {
    dut.clk = 1; dut.eval();
    dut.clk = 0; dut.eval();
}

int main(int argc, char** argv) {
    VerilatedContext ctx;
    ctx.commandArgs(argc, argv);
    Vblink dut;

    // Hold reset for one cycle
    dut.rst_n = 0; dut.clk = 0; dut.eval();
    tick(dut);
    dut.rst_n = 1;

    if (dut.led != 0) { std::printf("FAIL: led not 0 after reset\n"); return 1; }

    // Counter is 4 bits wide, led = bit 3: high after 8 posedges
    for (int i = 0; i < 8; i++) tick(dut);
    if (dut.led != 1) { std::printf("FAIL: led not 1 after 8 clocks\n"); return 1; }

    // ...and low again 8 clocks later (wraps)
    for (int i = 0; i < 8; i++) tick(dut);
    if (dut.led != 0) { std::printf("FAIL: led did not wrap\n"); return 1; }

    std::printf("PASS: blink_tb\n");
    return 0;
}
```

- [ ] **Step 3: Write the Makefile**

Create `Makefile`:

```make
# Mattel Football openFPGA core — build entry points
# make sim       — build + run all Verilator testbenches (native, fast)
# make bitstream — compile the Quartus project in Docker (Task 3 adds this)
# make package   — bit-reverse + stage the bitstream for the Pocket (Task 4)

VERILATOR ?= verilator
VFLAGS    := -Wall --cc --exe --build -j 0

# One entry per testbench: <name> builds sim/<name>_tb.cpp against src/<name>.v
SIM_TESTS := blink

.PHONY: sim clean
sim: $(SIM_TESTS:%=sim-%)

sim-%:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_$* --top-module $* \
		-o $*_tb sim/$*_tb.cpp src/$*.v
	sim/obj_dir_$*/$*_tb

clean:
	rm -rf sim/obj_dir_*
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `make sim`
Expected: FAIL — Verilator error `Cannot find file containing module: 'src/blink.v'` (module not written yet).

- [ ] **Step 5: Write the module**

Create `src/blink.v`:

```verilog
// Toolchain smoke test module — deleted in Plan 2.
module blink (
    input  wire clk,
    input  wire rst_n,
    output wire led
);
    reg [3:0] count;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            count <= 4'd0;
        else
            count <= count + 4'd1;
    end

    assign led = count[3];
endmodule
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `make sim`
Expected: prints `PASS: blink_tb`, exit code 0.

- [ ] **Step 7: Write the README stub**

Create `README.md`:

```markdown
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
```

- [ ] **Step 8: Commit**

```bash
git add Makefile README.md src/blink.v sim/blink_tb.cpp
git commit -m "feat: Verilator sim toolchain with blink smoke test

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Docker Quartus toolchain proof

**Files:**
- Modify: `README.md` (troubleshooting note only if a workaround is needed)

**Interfaces:**
- Consumes: nothing.
- Produces: a verified local `didiermalenfant/quartus:22.1-apple-silicon` image; the exact `docker run` incantation reused by Task 3's Makefile target.

- [ ] **Step 1: Verify Docker is installed and running**

Run: `docker info --format '{{.Architecture}} {{.OperatingSystem}}'`
Expected: prints architecture + "Docker Desktop" without error. If the daemon isn't running, start Docker Desktop first. In Docker Desktop settings, confirm "Use Rosetta for x86_64/amd64 emulation" is enabled (Settings → General).

- [ ] **Step 2: Pull the Quartus image**

Run: `docker pull --platform linux/amd64 didiermalenfant/quartus:22.1-apple-silicon`
Expected: pull completes (multi-GB image; takes a while on first pull).

- [ ] **Step 3: Smoke-test Quartus inside the container**

Run:
```bash
docker run --platform linux/amd64 --rm didiermalenfant/quartus:22.1-apple-silicon quartus_sh --version
```
Expected: output containing `Quartus Prime Shell` and version `22.1`. If this segfaults or hangs, re-check Rosetta emulation; as a fallback, substitute image `raetro/quartus:18.1` here and in later tasks (same CLI).

- [ ] **Step 4: Commit (only if README changed)**

If a workaround was needed, document it in README's Prerequisites section, then:

```bash
git add README.md
git commit -m "docs: note Docker/Rosetta requirement for Quartus builds

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

If no changes: no commit (nothing to record).

---

### Task 3: Vendor the official core-template and build a bitstream

**Files:**
- Create: `src/fpga/` (vendored from `open-fpga/core-template`, subdirectory `src/fpga` of that repo)
- Create: `dist/` (vendored from the template's `dist/` — APF JSON manifests)
- Create: `docs/template-notes.md`
- Modify: `Makefile` (add `bitstream` target)

**Interfaces:**
- Consumes: Docker image proven in Task 2.
- Produces: `make bitstream` — compiles the Quartus project and leaves an RBF under `src/fpga/output_files/`; the RBF path is recorded in `docs/template-notes.md` for Task 4. The vendored `src/fpga/core/` directory is where all Plan 2+ HDL gets instantiated.

- [ ] **Step 1: Clone and vendor the template**

Run:
```bash
git clone --depth 1 https://github.com/open-fpga/core-template /tmp/core-template
cp -R /tmp/core-template/src/fpga src/fpga
cp -R /tmp/core-template/dist dist
cp /tmp/core-template/LICENSE docs/CORE_TEMPLATE_LICENSE
ls src/fpga
```
Expected: `src/fpga` now contains a Quartus project — exactly one `*.qpf` file (the template names it `ap_core.qpf`), a matching `.qsf`, an `apf/` framework directory, and a `core/` directory for user logic. If the template's layout differs from this expectation, stop and record the actual layout in `docs/template-notes.md` before proceeding — later steps refer to the `.qpf` by discovered path.

- [ ] **Step 2: Record template facts**

Create `docs/template-notes.md`:

```markdown
# core-template vendoring notes

- Vendored from: https://github.com/open-fpga/core-template (commit: <fill from
  `git -C /tmp/core-template rev-parse HEAD` output — an exact hash, recorded at
  vendor time>)
- License: see docs/CORE_TEMPLATE_LICENSE
- Quartus project file: <actual path, e.g. src/fpga/ap_core.qpf>
- RBF output after compile: <actual path under src/fpga/output_files/>
- dist/ layout: <actual Cores/... and Platforms/... paths from the template>

Do not upgrade Quartus device settings in the .qsf — preconfigured for the
Pocket's Cyclone V.
```

Fill every `<...>` with the real observed values in this step — this file is the single source of truth for paths used by `make package`.

- [ ] **Step 3: Add the `bitstream` Makefile target**

Add to `Makefile` (below the `sim` rules; adjust `QPF` if the discovered name differs):

```make
QUARTUS_IMAGE ?= didiermalenfant/quartus:22.1-apple-silicon
QPF           ?= ap_core.qpf

.PHONY: bitstream
bitstream:
	docker run --platform linux/amd64 --rm -t \
		-v $(PWD)/src/fpga:/build -w /build \
		$(QUARTUS_IMAGE) quartus_sh --flow compile $(QPF)
```

- [ ] **Step 4: Build the bitstream**

Run: `make bitstream`
Expected: Quartus flow runs fit/asm and finishes with `Quartus Prime Assembler was successful`; an `.rbf` appears under `src/fpga/output_files/`. This is slow under emulation (tens of minutes) — normal. Record the exact RBF path in `docs/template-notes.md`.

- [ ] **Step 5: Commit**

```bash
git add src/fpga dist docs/template-notes.md docs/CORE_TEMPLATE_LICENSE Makefile
git commit -m "feat: vendor open-fpga core-template; make bitstream builds RBF in Docker

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: RBF reversal, packaging, and on-Pocket boot test

**Files:**
- Create: `tools/reverse_rbf.py`
- Create: `sim/test_reverse_rbf.py`
- Modify: `Makefile` (add `package` target)

**Interfaces:**
- Consumes: RBF path recorded in `docs/template-notes.md` (Task 3); `dist/` layout from the template.
- Produces: `make package` — stages `dist/` with the reversed bitstream in place, ready to copy onto a Pocket SD card. `tools/reverse_rbf.py <in.rbf> <out.rbf_r>` is reused unchanged for the life of the project.

- [ ] **Step 1: Write the failing test for the reversal tool**

Create `sim/test_reverse_rbf.py`:

```python
"""Bit-reversal must be an involution and match known byte mappings."""
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL = Path(__file__).resolve().parent.parent / "tools" / "reverse_rbf.py"

def run(data: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "in.rbf"
        dst = Path(d) / "out.rbf_r"
        src.write_bytes(data)
        subprocess.run([sys.executable, str(TOOL), str(src), str(dst)], check=True)
        return dst.read_bytes()

def main() -> int:
    # Known mappings: 0x01 -> 0x80, 0xA5 -> 0xA5, 0xF0 -> 0x0F, 0x00 -> 0x00
    assert run(bytes([0x01, 0xA5, 0xF0, 0x00])) == bytes([0x80, 0xA5, 0x0F, 0x00]), "byte map"
    # Involution: reversing twice returns the original
    sample = bytes(range(256))
    assert run(run(sample)) == sample, "involution"
    print("PASS: test_reverse_rbf")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 sim/test_reverse_rbf.py`
Expected: FAIL — `FileNotFoundError` / CalledProcessError (tool doesn't exist yet).

- [ ] **Step 3: Write the tool**

Create `tools/reverse_rbf.py`:

```python
#!/usr/bin/env python3
"""Convert an RBF to the Pocket's RBF_R format: bit-reverse every byte.

Usage: reverse_rbf.py <input.rbf> <output.rbf_r>
"""
import sys

TABLE = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))

def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(data.translate(TABLE))
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 sim/test_reverse_rbf.py`
Expected: `PASS: test_reverse_rbf`, exit 0.

- [ ] **Step 5: Add the `package` target**

Add to `Makefile`. `RBF` and `RBF_R_DEST` come from the paths recorded in `docs/template-notes.md` — set the actual values here, not these examples:

```make
RBF        ?= src/fpga/output_files/ap_core.rbf
RBF_R_DEST ?= $(shell find dist/Cores -name '*.rbf_r' | head -1)

.PHONY: package
package:
	python3 tools/reverse_rbf.py $(RBF) $(RBF_R_DEST)
	@echo "Staged: $(RBF_R_DEST)"
	@echo "Copy the contents of dist/ onto the Pocket SD card root."
```

(If the template's `dist/` ships without a placeholder `.rbf_r`, set `RBF_R_DEST` explicitly to the bitstream path named in the template's `core.json` and update `docs/template-notes.md`.)

- [ ] **Step 6: Package and boot on hardware — CHECKPOINT (human required)**

Run: `make package`, then copy `dist/`'s contents onto the Pocket SD card root (merging `Cores/`, `Platforms/`, etc.), insert, power on.
Expected: the template core appears in the Pocket's openFPGA menu and launches to the template's output (a static screen — exact content per template README) without an error dialog. **This step requires the human partner and the physical device — stop and ask before proceeding.**

- [ ] **Step 7: Commit**

```bash
git add tools/reverse_rbf.py sim/test_reverse_rbf.py Makefile
git commit -m "feat: RBF_R packaging; template core verified booting on Pocket

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Done criteria (Milestone 1)

- `make sim` runs a native Verilator testbench and passes.
- `make bitstream` produces an RBF via Docker Quartus.
- `make package` stages a Pocket-installable `dist/` with a bit-reversed bitstream.
- The template core boots on the physical Pocket.

## Out of scope (later plans)

- Any B6100/game HDL (Plan 2), display/audio pipeline (Plan 3), APF data
  slots/ROM loading and input mapping (Plan 4), artwork/settings/release
  packaging (Plan 5).
