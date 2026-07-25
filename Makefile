# Mattel Football openFPGA core — build entry points
# make sim       — build + run all Verilator testbenches (native, fast)
# make bitstream — compile the Quartus project in Docker (Task 3 adds this)
# make package   — bit-reverse + stage the bitstream for the Pocket (Task 4)

VERILATOR ?= verilator
VFLAGS    := -Wall --cc --exe --build -j 0

# One entry per testbench: <name> builds sim/<name>_tb.cpp against src/<name>.v
SIM_TESTS := b6100_cpu

.PHONY: sim clean sim-python
sim: $(SIM_TESTS:%=sim-%) sim-python

sim-%:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_$* --top-module $* \
		-o $*_tb sim/$*_tb.cpp src/$*.v
	sim/obj_dir_$*/$*_tb

sim-python:
	python3 sim/test_reverse_rbf.py

clean:
	rm -rf sim/obj_dir_*

QUARTUS_IMAGE ?= didiermalenfant/quartus:22.1-apple-silicon
QPF           ?= ap_core.qpf

.PHONY: bitstream
bitstream:
	docker run --platform linux/amd64 --rm -t \
		-v $(PWD)/src/fpga:/build -w /build \
		$(QUARTUS_IMAGE) quartus_sh --flow compile $(QPF)

RBF        ?= src/fpga/output_files/ap_core.rbf
RBF_R_DEST ?= dist/Cores/Developer.Core Template/bitstream.rbf_r

.PHONY: package
package:
	python3 tools/reverse_rbf.py "$(RBF)" "$(RBF_R_DEST)"
	@echo "Staged: $(RBF_R_DEST)"
	@echo "Copy the contents of dist/ onto the Pocket SD card root."

ROM ?= sim/roms/mfootb.bin

.PHONY: tracegen smoke
tracegen:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_trace --top-module b6100_cpu \
		-o b6100_trace sim/b6100_cpu_trace.cpp src/b6100_cpu.v

smoke: tracegen
	sim/obj_dir_trace/b6100_trace $(ROM) 1000000 0 1 sim/smoke.csv
	@wc -l sim/smoke.csv

MAME ?= mame
GOLDEN_N ?= 2000000
# Cycles to hold momentary-button inputs (kb, and DIN's Score bit) at their
# idle value before switching to the scenario's real value. Models MAME's
# ioport digital fields, which only latch into their live value once per
# (emulated) 60Hz screenless video frame -- not from cycle 0 -- so a field
# held from machine start via tools/golden/hold_input.lua does not reach
# read_kb()/read_din() until that first frame boundary. Empirically the
# real MAME edge falls within [~600,~1480] cycles (bisected against a 2s
# golden capture); 1000 sits in the middle of that safe window. See
# docs/verification.md for the derivation.
GOLDEN_SETTLE ?= 1000

# scenario table: name -> kb, din, port, field (constant-from-boot inputs)
# idle:  kb=0 din=1 (difficulty PRO1 is DIN bit0, default on)
# fwd:   kb=2 din=1 hold ":IN.0"/"Forward"
# kick:  kb=8 din=1 hold ":IN.0"/"Kick"
# score: kb=0 din=3 hold ":IN.1"/"Score"

.PHONY: golden
golden: tracegen
	@case "$(SCENARIO)" in \
	  idle)  KB=0; DIN=1; PORT=;      FIELD=;;        \
	  fwd)   KB=2; DIN=1; PORT=:IN.0; FIELD=Forward;; \
	  kick)  KB=8; DIN=1; PORT=:IN.0; FIELD=Kick;;    \
	  score) KB=0; DIN=3; PORT=:IN.1; FIELD=Score;;   \
	  *) echo "usage: make golden SCENARIO=idle|fwd|kick|score"; exit 2;; \
	esac; \
	rm -f golden.tr; \
	GOLDEN_PORT=$$PORT GOLDEN_FIELD=$$FIELD \
	  $(MAME) mfootb -debug -debugscript tools/golden/trace.debugscript \
	  -autoboot_script tools/golden/hold_input.lua \
	  -video none -sound none -nothrottle -seconds_to_run 45; \
	sim/obj_dir_trace/b6100_trace $(ROM) $(GOLDEN_N) $$KB $$DIN ours.csv $(GOLDEN_SETTLE); \
	python3 tools/golden/trace_diff.py golden.tr ours.csv
