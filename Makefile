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
