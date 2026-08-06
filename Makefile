# Mattel Football openFPGA core — build entry points
# make sim       — build + run all Verilator testbenches (native, fast)
# make bitstream — compile the Quartus project in Docker (Task 3 adds this)
# make package   — bit-reverse + stage the bitstream for the Pocket (Task 4)

VERILATOR ?= verilator
VFLAGS    := -Wall --cc --exe --build -j 0

# One entry per testbench: <name> builds sim/<name>_tb.cpp against src/<name>.v
SIM_TESTS := b6100_cpu led_capture video_renderer ce_gen rom_loader audio_i2s label_rom field_rom

.PHONY: sim clean sim-python
sim: $(SIM_TESTS:%=sim-%) sim-python

sim-%:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_$* --top-module $* \
		-o $*_tb sim/$*_tb.cpp src/$*.v
	sim/obj_dir_$*/$*_tb

# label_rom.v/field_rom.v's $readmemh calls use bare filenames (e.g.
# "label_bitmap.mem"), which Verilator resolves relative to the process's
# working directory at run time (not compile time) -- so, unlike the other
# sim-% targets, the built binary must be run with cwd set to src/. These
# explicit rules override the sim-% pattern rule above.
sim-label_rom:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_label_rom --top-module label_rom \
		-o label_rom_tb sim/label_rom_tb.cpp src/label_rom.v
	cd src && ../sim/obj_dir_label_rom/label_rom_tb

sim-field_rom:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_field_rom --top-module field_rom \
		-o field_rom_tb sim/field_rom_tb.cpp src/field_rom.v
	cd src && ../sim/obj_dir_field_rom/field_rom_tb

# video_renderer.v instantiates label_rom and field_rom (both need the same
# cwd=src/ $readmemh handling as above), so it also needs an explicit
# override: extra source files, and run from src/.
sim-video_renderer:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_video_renderer --top-module video_renderer \
		-o video_renderer_tb sim/video_renderer_tb.cpp src/video_renderer.v src/label_rom.v src/field_rom.v
	cd src && ../sim/obj_dir_video_renderer/video_renderer_tb

sim-python:
	python3 sim/test_reverse_rbf.py
	python3 sim/test_trace_diff.py
	python3 sim/test_platform_icon.py

clean:
	rm -rf sim/obj_dir_*

QUARTUS_IMAGE ?= didiermalenfant/quartus:22.1-apple-silicon
QPF           ?= ap_core.qpf

.PHONY: bitstream
bitstream:
	docker run --platform linux/amd64 --rm -t \
		-v $(PWD)/src:/build/src -w /build/src/fpga \
		$(QUARTUS_IMAGE) quartus_sh --flow compile $(QPF)

RBF        ?= src/fpga/output_files/ap_core.rbf
RBF_R_DEST ?= dist/Cores/cjchand.Mattel Football/bitstream.rbf_r

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

.PHONY: frames
frames:
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_frames --top-module football_system \
		-o football_frames sim/football_system_tb.cpp \
		src/football_system.v src/b6100_cpu.v src/led_capture.v src/video_renderer.v src/label_rom.v src/field_rom.v
	# The dash field (ball movement) and the digit readout (down/field
	# position/yards to go) are mutually-exclusive display modes on real
	# hardware -- confirmed against MAME (mfootb, hh_rw5000.cpp): holding
	# Status blanks the dash field for as long as it's held, and the digits
	# never light without it. So this target runs the harness twice: once
	# in "game" mode (kb=2 Forward, din=1, asserting only the dash
	# brightness classes) for a real gameplay/movement capture, and once in
	# "status" mode (kb=0, din=5 = difficulty + Status, asserting only the
	# digit readout) for a short digit-readout capture. Both must exit 0.
	mkdir -p sim/frames/game sim/frames/status
	# football_system.v now instantiates video_renderer, which instantiates
	# label_rom -- its $$readmemh calls resolve relative to cwd at run time
	# (see Makefile's sim-label_rom/sim-video_renderer comments), so this
	# binary must also run with cwd=src/; use absolute paths for the ROM
	# and outdirs since they're no longer relative to the repo root.
	cd src && \
	../sim/obj_dir_frames/football_frames $(CURDIR)/$(ROM) 180 2 1 1000 $(CURDIR)/sim/frames/game game && \
	../sim/obj_dir_frames/football_frames $(CURDIR)/$(ROM) 60 0 5 1000 $(CURDIR)/sim/frames/status status

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
	sim/obj_dir_trace/b6100_trace $(ROM) $(GOLDEN_N) $$KB $$DIN ours.csv $(GOLDEN_SETTLE) && \
	python3 tools/golden/trace_diff.py golden.tr ours.csv --min $$(( $(GOLDEN_N) / 2 ))

# Display parity against MAME. `golden` above proves the CPU matches
# instruction-for-instruction; this proves the DISPLAY matches -- the duty
# measurement and the alpha=1/2 interpolation in led_capture.v, checked
# against the pwm_display_device mfootb actually configures rather than
# against a C++ twin of our own RTL (which would share any mistake by
# construction).
#
# Both sides discard a settling stretch first: DISPLAY_SKIP frames of MAME,
# and the equivalent windows on our side, so neither capture includes the
# boot-time ramp. See tools/golden/display_diff.py for why the comparison is
# per-cell steady behaviour rather than frame-for-frame.
# 420 frames (7s) rather than a shorter run: the two captures sit about one
# frame apart in phase, so a window that happens to END inside a transient
# shows one side a step further along the same ramp than the other. At 180
# frames the fwd scenario cut exactly into the ball carrier's decay and
# reported a difference that a longer window shows is only that offset.
DISPLAY_N     ?= 420
DISPLAY_SKIP  ?= 120
DISPLAY_SCENARIO ?= idle

.PHONY: display-parity
display-parity:
	@case "$(DISPLAY_SCENARIO)" in \
	  idle)  KB=0; DIN=1; PORT=;      FIELD=;;        \
	  fwd)   KB=2; DIN=1; PORT=:IN.0; FIELD=Forward;; \
	  kick)  KB=8; DIN=1; PORT=:IN.0; FIELD=Kick;;    \
	  score) KB=0; DIN=3; PORT=:IN.1; FIELD=Score;;   \
	  *) echo "usage: make display-parity DISPLAY_SCENARIO=idle|fwd|kick|score"; exit 2;; \
	esac; \
	$(VERILATOR) $(VFLAGS) --Mdir sim/obj_dir_display_parity --top-module football_system \
	  -o display_parity sim/display_parity_tb.cpp \
	  src/football_system.v src/b6100_cpu.v src/led_capture.v src/video_renderer.v \
	  src/label_rom.v src/field_rom.v && \
	rm -f sim/mame_display.csv sim/ours_display.csv && \
	GOLDEN_PORT=$$PORT GOLDEN_FIELD=$$FIELD \
	GOLDEN_DISPLAY_OUT=$(CURDIR)/sim/mame_display.csv \
	GOLDEN_DISPLAY_SKIP=$(DISPLAY_SKIP) \
	  $(MAME) mfootb -autoboot_script tools/golden/dump_display.lua \
	  -video none -sound none -nothrottle \
	  -seconds_to_run $$(( ($(DISPLAY_N) + $(DISPLAY_SKIP)) / 60 + 2 )) && \
	cd src && ../sim/obj_dir_display_parity/display_parity \
	  $(CURDIR)/$(ROM) $$(( $(DISPLAY_N) + $(DISPLAY_SKIP) )) $$KB $$DIN $(GOLDEN_SETTLE) \
	  $(CURDIR)/sim/ours_display.csv && \
	cd $(CURDIR) && python3 tools/golden/display_diff.py \
	  sim/mame_display.csv sim/ours_display.csv --skip-ours $(DISPLAY_SKIP)

# Run every scenario. Not optional dressing: no single scenario catches every
# way the display model can be wrong. Removing the interpolation entirely is
# only visible in `score`; FB2's alpha=1/8 shows in `fwd` and `score`; FB2's
# WINDOW=1583 shows in `fwd` and `kick`. `idle` catches none of the three on
# its own -- it is here because it is the attract state the core boots into,
# not because it is load-bearing.
.PHONY: display-parity-all
display-parity-all:
	@for s in idle fwd kick score; do \
	  echo "=== display-parity: $$s ==="; \
	  $(MAKE) --no-print-directory display-parity DISPLAY_SCENARIO=$$s || exit 1; \
	done
